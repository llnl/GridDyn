/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Checkpoint.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../GridComponent.h"
#include "../GridDynSimulation.h"
#include "../GridSubModel.h"
#include "../Link.h"
#include "../events/Event.h"
#include "../events/EventAdapters.h"
#include "../events/EventQueue.h"
#include "../measurement/Collector.h"
#include "../measurement/Recorder.h"
#include "../solvers/SolverInterface.h"
#include "GridDynSimulationFileOps.h"
#include "core/CheckpointArchive.h"
#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include "units/units.hpp"
#include "utilities/Hdf5DataFile.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <span>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _WIN32
#    include <windows.h>
#endif

namespace griddyn {
namespace {
    using Json = nlohmann::json;
    using utilities::Hdf5DataFile;
    using utilities::Hdf5RecordType;
    using utilities::Hdf5RecordView;
    constexpr std::uint32_t componentSource = 2;

    struct Component {
        GridComponent* mObject;
        std::size_t mParent;
    };

    std::vector<Component> components(GridDynSimulation* simulation)
    {
        std::vector<Component> result{{.mObject = simulation, .mParent = 0}};
        std::unordered_set<GridComponent*> visited{simulation};
        for (std::size_t parent = 0; parent < result.size(); ++parent) {
            const auto& subobjects = result[parent].mObject->getSubObjects();
            std::vector<GridComponent*> children(subobjects.begin(), subobjects.end());
            if (const auto* area = dynamic_cast<const GridArea*>(result[parent].mObject)) {
                for (index_t index = 0;; ++index) {
                    auto* primary =
                        dynamic_cast<GridComponent*>(area->getSubObject("object", index));
                    if (primary == nullptr) {
                        break;
                    }
                    children.push_back(primary);
                }
            }
            for (auto* child : children) {
                if (!visited.insert(child).second) {
                    if (child->getParent() != result[parent].mObject) {
                        throw FileOperationError(
                            "checkpoint model hierarchy contains a shared child");
                    }
                    continue;
                }
                result.push_back({.mObject = child, .mParent = parent});
            }
        }
        return result;
    }

    std::filesystem::path uniqueSibling(const std::filesystem::path& manifest,
                                        std::string_view extension)
    {
        static std::atomic<std::uint64_t> sequence{0};
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto token =
                (static_cast<std::uint64_t>(random()) << 32U) ^ random() ^ sequence.fetch_add(1);
            const auto file = manifest.parent_path() /
                (manifest.stem().string() + "." + std::to_string(token) + std::string(extension));
            if (!std::filesystem::exists(file)) {
                return file;
            }
        }
        throw FileOperationError("unable to allocate a checkpoint artifact name");
    }

    void publish(const std::filesystem::path& pending, const std::filesystem::path& manifest)
    {
#ifdef _WIN32
        if (MoveFileExW(pending.c_str(), manifest.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
            throw FileOperationError("unable to publish checkpoint " + manifest.string());
        }
#else
        std::filesystem::rename(pending, manifest);
#endif
    }

    Json adapterState(const EventAdapter& adapter)
    {
        return {{"next_time_ns", adapter.m_nextTime.to_ns().count()},
                {"period_ns", adapter.m_period.to_ns().count()},
                {"part_b_delay_ns", adapter.partBdelay.to_ns().count()},
                {"remove", adapter.m_remove_event},
                {"two_part", adapter.two_part_execute},
                {"part_b_turn", adapter.partB_turn},
                {"part_b_only", adapter.partB_only}};
    }

    CoreTime readTime(const Json& value)
    {
        if (!value.is_number_integer() ||
            (value.is_number_unsigned() &&
             value.get<std::uint64_t>() >
                 static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
            throw FileOperationError("checkpoint clock must contain integer nanoseconds");
        }
        return {std::chrono::nanoseconds(value.get<std::int64_t>())};
    }

    void restoreAdapter(const Json& state, EventAdapter& adapter)
    {
        adapter.m_nextTime = readTime(state.at("next_time_ns"));
        adapter.m_period = readTime(state.at("period_ns"));
        adapter.partBdelay = readTime(state.at("part_b_delay_ns"));
        adapter.m_remove_event = state.at("remove").get<bool>();
        adapter.two_part_execute = state.at("two_part").get<bool>();
        adapter.partB_turn = state.at("part_b_turn").get<bool>();
        adapter.partB_only = state.at("part_b_only").get<bool>();
    }

    Json connections(const std::vector<Component>& objects,
                     const std::unordered_map<CoreObject*, std::size_t>& indices)
    {
        Json result = Json::array();
        for (const auto& component : objects) {
            Json references = Json::array();
            if (const auto* link = dynamic_cast<const Link*>(component.mObject)) {
                for (index_t terminal = 1; terminal <= link->terminalCount(); ++terminal) {
                    if (auto* bus = link->getBus(terminal)) {
                        references.push_back(indices.at(bus));
                    } else {
                        references.push_back(nullptr);
                    }
                }
            }
            if (const auto* generator = dynamic_cast<const Generator*>(component.mObject)) {
                if (auto* remote = generator->getRemoteBus()) {
                    references.push_back(indices.at(remote));
                }
            }
            result.push_back(std::move(references));
        }
        return result;
    }
}  // namespace

/** Coordinates the power-flow persistence profile. Friendship is confined to
 * simulation bookkeeping and event adapters; model fields use virtual hooks.
 */
class CheckpointCoordinator {
    using Assignments = CheckpointArchive::Assignments;

    static void solverFields(CheckpointArchive& archive, SolverInterface& solver)
    {
        archive.model<SolverInterface>("SolverInterface");
        archive.state("mask", solver.maskElements);
        archive.flag("mask_enabled", solver.flags, USE_MASK_FLAG);
        archive.parameter("tolerance", solver.tolerance);
        archive.parameter("max_iterations", solver.max_iterations);
    }

    static void collectorFields(CheckpointArchive& archive, Collector& collector)
    {
        archive.model<Collector>("Collector");
        archive.state("trigger", collector.mTriggerTime);
        archive.state("last_trigger", collector.mLastTriggerTime);
        archive.state("armed", collector.mArmed);
        archive.state("recheck", collector.mRecheck);
        archive.parameter("period", collector.mTimePeriod);
        archive.parameter("requested_period", collector.mRequestedPeriod);
        archive.parameter("start", collector.mStartTime);
        archive.parameter("stop", collector.mStopTime);
        archive.parameter("origin", collector.mInitialTriggerTime);
        archive.parameter("delayed", collector.mDelayProcess);
    }

    static Json captureQueue(GridDynSimulation* simulation,
                             const std::unordered_map<CoreObject*, std::size_t>& indices,
                             bool parameters)
    {
        const auto& queue = *simulation->EvQ;
        const std::scoped_lock lock(queue.queuelock_);
        if (!queue.partB_list.empty()) {
            throw FileOperationError(
                "checkpoint requires an event boundary with no pending part B");
        }
        Json result{{"tolerance_ns", queue.timeTols.to_ns().count()},
                    {"null", adapterState(*queue.nullEvent)},
                    {"events", Json::array()}};
        Assignments unused;
        std::vector<double> values;
        for (const auto& adapter : queue.events) {
            if (adapter == queue.nullEvent) {
                continue;
            }
            Json entry{{"adapter", adapterState(*adapter)}};
            const auto* currentAdapter = adapter.get();
            Event* event = nullptr;
            if (const auto* sharedEvent =
                    dynamic_cast<const EventTypeAdapter<std::shared_ptr<Event>>*>(adapter.get())) {
                event = sharedEvent->getTarget();
            }
            if (event != nullptr) {
                if (typeid(*event) != typeid(Event)) {
                    throw FileOperationError("checkpoint does not support this derived event type");
                }
                entry["kind"] = "event";
                entry["event"] = {{"name", event->getName()},
                                  {"target", indices.at(event->m_obj)},
                                  {"field", event->field},
                                  {"value", event->value},
                                  {"time_ns", event->triggerTime.to_ns().count()},
                                  {"units",
                                   event->unitType == units::defunit ?
                                       "" :
                                       units::to_string(event->unitType)},
                                  {"armed", event->armed},
                                  {"resettable", event->resettable},
                                  {"reversible", event->reversible},
                                  {"initialize", event->initRequired}};
            } else if (const auto* collectorAdapter =
                           dynamic_cast<const EventTypeAdapter<std::shared_ptr<Collector>>*>(
                               adapter.get())) {
                auto* collector = collectorAdapter->getTarget();
                if (typeid(*collector) != typeid(Collector) &&
                    typeid(*collector) != typeid(Recorder)) {
                    throw FileOperationError("checkpoint does not support this collector type");
                }
                const auto& collectors = simulation->getCollectors();
                const auto location = std::find_if(collectors.begin(),
                                                   collectors.end(),
                                                   [collector](const auto& candidate) {
                                                       return candidate.get() == collector;
                                                   });
                if (location == collectors.end()) {
                    throw FileOperationError("checkpoint collector is not owned by the simulation");
                }
                entry["kind"] = "collector";
                entry["index"] = static_cast<std::size_t>(location - collectors.begin());
                CheckpointArchive archive(
                    entry["schedule"], values, true, parameters, unused, unused);
                collectorFields(archive, *collector);
            } else if (const auto* objectAdapter =
                           dynamic_cast<const EventTypeAdapter<CoreObject>*>(adapter.get())) {
                entry["kind"] = "object";
                entry["target"] = indices.at(objectAdapter->getTarget());
            } else if (typeid(*currentAdapter) == typeid(EventAdapter)) {
                entry["kind"] = "empty";
            } else {
                throw FileOperationError(
                    "checkpoint does not support this event adapter or callback");
            }
            result["events"].push_back(std::move(entry));
        }
        return result;
    }

    static std::unique_ptr<EventQueue> prepareQueue(GridDynSimulation* simulation,
                                                    Json& document,
                                                    const std::vector<Component>& objects,
                                                    bool parameters,
                                                    Assignments& stateAssignments,
                                                    Assignments& parameterAssignments)
    {
        auto queue = std::make_unique<EventQueue>();
        queue->timeTols = readTime(document.at("tolerance_ns"));
        restoreAdapter(document.at("null"), *queue->nullEvent);
        const auto target = [&](const Json& index) {
            return objects.at(index.get<std::size_t>()).mObject;
        };
        std::vector<double> values;
        for (auto& entry : document.at("events")) {
            const auto kind = entry.at("kind").get<std::string>();
            std::shared_ptr<EventAdapter> adapter;
            if (kind == "event") {
                const auto& data = entry.at("event");
                auto event = std::make_shared<Event>(data.at("name").get<std::string>());
                event->m_obj = target(data.at("target"));
                event->field = data.at("field").get<std::string>();
                event->value = data.at("value").get<double>();
                event->triggerTime = readTime(data.at("time_ns"));
                const auto text = data.at("units").get<std::string>();
                event->unitType =
                    text.empty() ? units::defunit : unit_cast(units::unit_from_string(text));
                if (!is_valid(event->unitType)) {
                    throw FileOperationError("invalid checkpoint event units");
                }
                event->armed = data.at("armed").get<bool>();
                event->resettable = data.at("resettable").get<bool>();
                event->reversible = data.at("reversible").get<bool>();
                event->initRequired = data.at("initialize").get<bool>();
                adapter = std::make_shared<EventTypeAdapter<std::shared_ptr<Event>>>(event);
            } else if (kind == "collector") {
                const auto index = entry.at("index").get<std::size_t>();
                auto collector = simulation->getCollectors().at(index);
                CheckpointArchive archive(entry.at("schedule"),
                                          values,
                                          false,
                                          parameters,
                                          stateAssignments,
                                          parameterAssignments);
                collectorFields(archive, *collector);
                adapter = std::make_shared<EventTypeAdapter<std::shared_ptr<Collector>>>(collector);
            } else if (kind == "object") {
                adapter =
                    std::make_shared<EventTypeAdapter<CoreObject>>(target(entry.at("target")));
            } else if (kind == "empty") {
                adapter = std::make_shared<EventAdapter>();
            } else {
                throw FileOperationError("unsupported checkpoint event kind");
            }
            restoreAdapter(entry.at("adapter"), *adapter);
            queue->insert(std::move(adapter));
        }
        queue->sort();
        return queue;
    }

    static void requireBoundary(GridDynSimulation* simulation)
    {
        if (simulation->currentProcessState() != GridSimulation::GridState::POWERFLOW_COMPLETE) {
            throw FileOperationError(
                "power-flow checkpoints require a completed power-flow solution");
        }
    }

  public:
    static void
        save(GridDynSimulation* simulation, const std::string& fileName, CheckpointLevel level)
    {
        requireBoundary(simulation);
        if (level != CheckpointLevel::STATE && level != CheckpointLevel::PARAMETERS) {
            throw FileOperationError("unsupported checkpoint level");
        }
        if (fileName.empty()) {
            throw FileOperationError("no checkpoint manifest specified");
        }
        const bool parameters = level == CheckpointLevel::PARAMETERS;
        const auto manifest = std::filesystem::absolute(fileName);
        auto objects = components(simulation);
        std::unordered_map<CoreObject*, std::size_t> indices;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            indices.emplace(objects[index].mObject, index);
        }
        Json document{{"format", "griddyn-checkpoint"},
                      {"version", 1},
                      {"profile", "iterated-powerflow"},
                      {"level", parameters ? "parameters" : "state"},
                      {"capture_time_ns", simulation->getSimulationTime().to_ns().count()},
                      {"model", {{"source", simulation->sourceFile}}},
                      {"objects", Json::array()},
                      {"connections", connections(objects, indices)},
                      {"recording", "schedule-only; output history is retained by the caller"}};
        std::vector<std::vector<double>> runtime(objects.size());
        Assignments unused;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            auto* object = objects[index].mObject;
            Json entry{{"parent", objects[index].mParent},
                       {"slot", object->locIndex},
                       {"name", object->getName()}};
            CheckpointArchive archive(entry, runtime[index], true, parameters, unused, unused);
            archive.referenceContext([&indices](CoreObject* object) { return indices.at(object); },
                                     {});
            object->checkpointFields(archive);
            if (!archive.supports(*object)) {
                throw FileOperationError("checkpoint persistence is unsupported for " +
                                         object->getName());
            }
            document["objects"].push_back(std::move(entry));
        }
        document["events"] = captureQueue(simulation, indices, parameters);
        const auto mode = simulation->getSolverMode("powerflow");
        {
            std::vector<double> values;
            CheckpointArchive archive(document["solver"], values, true, parameters, unused, unused);
            solverFields(archive, *simulation->getSolverInterface(mode));
        }
        stringVec stateNames;
        simulation->getStateName(stateNames, mode);
        const auto payload = uniqueSibling(manifest, ".state.h5");
        const auto pending = uniqueSibling(manifest, ".pending.json");
        try {
            saveStateBinary(simulation, payload.string(), mode, false);
            {
                Hdf5DataFile output(payload.string(), Hdf5DataFile::OpenMode::APPEND);
                std::vector<Hdf5RecordView> records;
                records.reserve(objects.size() + 1);
                for (std::size_t index = 0; index < objects.size(); ++index) {
                    Hdf5RecordView view;
                    view.info.time_ns = simulation->getSimulationTime().to_ns().count();
                    view.info.source = componentSource;
                    view.info.key = index;
                    view.values = runtime[index];
                    records.push_back(std::move(view));
                }
                Hdf5RecordView layout;
                layout.info.time_ns = simulation->getSimulationTime().to_ns().count();
                layout.info.key = mode.offsetIndex;
                layout.info.source = componentSource;
                layout.info.type = Hdf5RecordType::NAMES;
                layout.info.code = 1;
                layout.names = stateNames;
                records.push_back(std::move(layout));
                output.append(records);
            }
            document["state"] = {{"file", payload.filename().generic_string()}, {"frame", 0}};
            {
                std::ofstream output(pending);
                output << document.dump(2) << '\n';
                output.close();
                if (!output) {
                    throw FileOperationError("unable to write checkpoint manifest");
                }
            }
            publish(pending, manifest);
        }
        catch (...) {
            std::error_code ignored;
            std::filesystem::remove(pending, ignored);
            std::filesystem::remove(payload, ignored);
            throw;
        }
        simulation->lastCheckpointFile = manifest.string();
    }

    static void load(GridDynSimulation* simulation, const std::string& fileName)
    {
        const auto targetMode = simulation->getSolverMode("powerflow");
        const auto targetSolver =
            static_cast<const GridDynSimulation*>(simulation)->getSolverInterface(targetMode);
        if (!targetSolver || !targetSolver->isInitialized() ||
            !simulation->getOffsets(targetMode).offsetsLoaded) {
            throw FileOperationError("initialize the compatible power-flow model before restoring");
        }
        if (fileName.empty()) {
            throw FileOperationError("no checkpoint manifest specified");
        }
        const auto manifest = std::filesystem::absolute(fileName);
        Json document;
        {
            std::ifstream input(manifest);
            if (!input.is_open()) {
                throw FileOperationError("unable to open checkpoint " + fileName);
            }
            input >> document;
        }
        if (document.at("format") != "griddyn-checkpoint" || document.at("version") != 1 ||
            document.at("profile") != "iterated-powerflow" ||
            (document.at("level") != "state" && document.at("level") != "parameters")) {
            throw FileOperationError("unsupported checkpoint format, profile or level");
        }
        const bool parameters = document.at("level") == "parameters";
        const auto payload =
            manifest.parent_path() / document.at("state").at("file").get<std::string>();
        const auto saved = readStateBinary(payload.string());
        auto objects = components(simulation);
        auto& entries = document.at("objects");
        if (!entries.is_array() || entries.size() != objects.size()) {
            throw FileOperationError("checkpoint model hierarchy does not match");
        }
        std::unordered_map<CoreObject*, std::size_t> indices;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            indices.emplace(objects[index].mObject, index);
        }
        if (document.at("connections") != connections(objects, indices)) {
            throw FileOperationError("checkpoint network connections do not match");
        }
        const Hdf5DataFile input(payload.string(), Hdf5DataFile::OpenMode::READ_ONLY);
        std::vector<std::vector<double>> runtime(objects.size());
        std::vector<bool> found(objects.size(), false);
        stringVec stateNames;
        bool foundLayout = false;
        for (const auto& record : input.readRecords()) {
            if (record.source == 0 &&
                record.frame != document.at("state").at("frame").get<std::uint64_t>()) {
                throw FileOperationError("checkpoint snapshot frame does not match the manifest");
            }
            if (record.source != componentSource) {
                continue;
            }
            if (record.type == Hdf5RecordType::NAMES && record.code == 1) {
                if (foundLayout) {
                    throw FileOperationError("checkpoint has duplicate state layouts");
                }
                stateNames = input.readNames(record);
                foundLayout = true;
            } else if (record.type == Hdf5RecordType::VECTOR && record.code == 0 &&
                       record.key < objects.size() && !found[record.key]) {
                runtime[record.key] = input.readValues(record);
                found[record.key] = true;
            } else {
                throw FileOperationError("unsupported or duplicate checkpoint runtime record");
            }
        }
        stringVec targetNames;
        const auto mode = simulation->getSolverMode("powerflow");
        simulation->getStateName(targetNames, mode);
        const auto solver = simulation->getSolverInterface(mode);
        if (!foundLayout || stateNames != targetNames ||
            !std::all_of(found.begin(), found.end(), [](bool value) { return value; }) ||
            std::cmp_not_equal(saved.state.size(), solver->size()) || isDynamic(saved.mode) ||
            hasDifferential(saved.mode) || !saved.paired.empty() ||
            saved.mode.approx != mode.approx) {
            throw FileOperationError("checkpoint solver layout does not match");
        }
        Assignments stateAssignments;
        Assignments parameterAssignments;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            auto* object = objects[index].mObject;
            auto& entry = entries.at(index);
            if (entry.at("parent") != objects[index].mParent ||
                entry.at("slot") != object->locIndex ||
                (index != 0 && dynamic_cast<GridSubModel*>(object) == nullptr &&
                 entry.at("name") != object->getName())) {
                throw FileOperationError("checkpoint component identity does not match");
            }
            const auto type = entry.at("type").get<std::string>();
            CheckpointArchive archive(
                entry, runtime[index], false, parameters, stateAssignments, parameterAssignments);
            archive.referenceContext({}, [&objects](std::size_t target) {
                return objects.at(target).mObject;
            });
            object->checkpointFields(archive);
            if (!archive.supports(*object) || archive.modelName() != type) {
                throw FileOperationError("checkpoint model type is unsupported or does not match");
            }
        }
        auto queue = prepareQueue(simulation,
                                  document.at("events"),
                                  objects,
                                  parameters,
                                  stateAssignments,
                                  parameterAssignments);
        std::vector<double> solverValues;
        CheckpointArchive solverArchive(document.at("solver"),
                                        solverValues,
                                        false,
                                        parameters,
                                        stateAssignments,
                                        parameterAssignments);
        solverFields(solverArchive, *solver);
        for (const auto& index :
             document.at("solver").at("runtime").at("SolverInterface").at("mask")) {
            if (!index.is_number_integer() || index.get<std::uint64_t>() >= saved.state.size()) {
                throw FileOperationError("checkpoint solver mask index is out of range");
            }
        }
        // Every file, model field and event has been read before committing.
        validateStateBinary(simulation, saved, mode);
        for (auto& assignment : stateAssignments) {
            assignment();
        }
        for (auto& assignment : parameterAssignments) {
            assignment();
        }
        solver->initialize(saved.time);
        loadStateBinary(simulation, saved, mode);
        simulation->updateLocalCache();
        simulation->EvQ = std::move(queue);
        simulation->lastCheckpointFile = manifest.string();
    }
};

void saveCheckpoint(GridDynSimulation* simulation,
                    const std::string& fileName,
                    CheckpointLevel level)
{
    try {
        CheckpointCoordinator::save(simulation, fileName, level);
    }
    catch (const FileOperationError&) {
        throw;
    }
    catch (const std::exception& error) {
        throw FileOperationError(error.what());
    }
}

void loadCheckpoint(GridDynSimulation* simulation, const std::string& fileName)
{
    try {
        CheckpointCoordinator::load(simulation, fileName);
    }
    catch (const FileOperationError&) {
        throw;
    }
    catch (const std::exception& error) {
        throw FileOperationError(error.what());
    }
}
}  // namespace griddyn
