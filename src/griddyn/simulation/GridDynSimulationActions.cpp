/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../GridBus.h"
#include "../GridDynSimulation.h"
#include "../Link.h"
#include "../Relay.h"
#include "../events/Event.h"
#include "../measurement/Collector.h"
#include "../solvers/SolverInterface.h"
#include "Contingency.h"
#include "Continuation.h"
#include "GridDynSimulationFileOps.h"
#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace griddyn {
// --------------- run the simulation ---------------
struct GridDynSimulation::ActionScript {
    std::vector<std::filesystem::path> mPaths;
};

namespace {
    CoreTime actionTimeOrigin(GridSimulation::GridState state, CoreTime current, CoreTime start)
    {
        if (state < GridSimulation::GridState::INITIALIZED) {
            return start;
        }
        return state < GridSimulation::GridState::DYNAMIC_INITIALIZED ? std::max(start, current) :
                                                                        current;
    }

    CoreTime actionTimeValue(double value, CoreTime fallback, std::string_view operand)
    {
        if (value == kNullVal) {
            return fallback;
        }
        if (!std::isfinite(value) || value <= static_cast<double>(negTime) ||
            value >= static_cast<double>(maxTime)) {
            throw ActionError(ActionError::Kind::SYNTAX, std::format("invalid {}", operand));
        }
        return {value};
    }

    CoreTime actionInterval(double value, CoreTime fallback)
    {
        const auto interval = actionTimeValue(value, fallback, "interval");
        if (interval <= timeZero || interval == maxTime) {
            throw ActionError(ActionError::Kind::SYNTAX, "interval must be positive and finite");
        }
        return interval;
    }

    GridDynAction parseAction(std::string_view text)
    {
        try {
            return {text};
        }
        catch (const std::logic_error& error) {
            throw ActionError(ActionError::Kind::SYNTAX, error.what());
        }
    }

    std::string describeActionError(std::string_view text,
                                    std::string_view reason,
                                    std::string_view source = {})
    {
        std::string message;
        if (!source.empty()) {
            message.append(source).append(": ");
        }
        message.append("action '").append(text).append("' failed: ").append(reason);
        return message;
    }

    std::string formatActionString(std::string_view value)
    {
        std::string output;
        output.reserve(value.size() + 3);
        output.push_back('"');
        for (const char character : value) {
            switch (character) {
                case '"':
                    output.append("\\\"");
                    break;
                case '\\':
                    output.append("\\\\");
                    break;
                case '\t':
                    output.append("\\t");
                    break;
                case '\r':
                    output.append("\\r");
                    break;
                case '\n':
                    output.append("\\n");
                    break;
                default: {
                    const auto byte = static_cast<unsigned char>(character);
                    if (byte < 0x20 || byte == 0x7f) {
                        output.append(std::format("\\u{{{:x}}}", byte));
                    } else {
                        output.push_back(character);
                    }
                    break;
                }
            }
        }
        output.append("\"\n");
        return output;
    }

    ObjectInfo actionField(std::string_view selector, const CoreObject& root)
    {
        const ObjectInfo info(selector, &root);
        if (info.mField.empty()) {
            throw ActionError(ActionError::Kind::SYNTAX, "expected a field name");
        }
        if (!info.mValidUnits) {
            throw ActionError(ActionError::Kind::SYNTAX,
                              std::format("invalid unit in '{}'", selector));
        }
        if (info.mObject == nullptr) {
            throw ActionError(ActionError::Kind::TARGET,
                              std::format("target '{}' was not found", selector));
        }
        return info;
    }

    void setActionFields(GridArea& root,
                         std::string_view type,
                         std::string_view selector,
                         double value)
    {
        if (type != "all" && type != "area" && type != "bus" && type != "link" && type != "relay" &&
            type != "gen" && type != "generator" && type != "load" && type != "secondary") {
            throw ActionError(ActionError::Kind::SYNTAX,
                              std::format("unknown setall type '{}'", type));
        }
        if (selector.find_first_of(":?") != std::string_view::npos) {
            throw ActionError(ActionError::Kind::SYNTAX,
                              "setall requires a field, optionally with units");
        }
        const auto field = actionField(selector, root);
        std::size_t updated = 0;
        const auto assign = [&](CoreObject& object) {
            try {
                object.set(field.mField, value, field.mUnitType);
                ++updated;
            }
            catch (const UnrecognizedParameter&) {
                // Different models of the same type may offer different fields.
                return;
            }
            catch (const std::exception& error) {
                throw ActionError(ActionError::Kind::EXECUTION,
                                  std::format("assignment to '{}' failed after {} update(s): {}",
                                              object.getName(),
                                              updated,
                                              error.what()));
            }
        };
        const auto assignChildren = [&assign](const CoreObject& parent,
                                              std::string_view childType) {
            for (index_t index = 0;; ++index) {
                auto* child = parent.getSubObject(childType, index);
                if (child == nullptr) {
                    break;
                }
                assign(*child);
            }
        };

        std::vector<GridArea*> areas{&root};
        while (!areas.empty()) {
            auto* area = areas.back();
            areas.pop_back();
            if (type == "all" || type == "area") {
                assign(*area);
            }
            for (index_t index = 0;; ++index) {
                auto* child = area->getSubObject("object", index);
                if (child == nullptr) {
                    break;
                }
                if (auto* subarea = dynamic_cast<GridArea*>(child)) {
                    areas.push_back(subarea);
                    continue;
                }
                const auto* bus = dynamic_cast<GridBus*>(child);
                if (type == "all" || (type == "bus" && bus != nullptr) ||
                    (type == "link" && dynamic_cast<Link*>(child) != nullptr) ||
                    (type == "relay" && dynamic_cast<Relay*>(child) != nullptr)) {
                    assign(*child);
                } else if (bus != nullptr) {
                    if (type == "gen" || type == "generator" || type == "secondary") {
                        assignChildren(*bus, "gen");
                    }
                    if (type == "load" || type == "secondary") {
                        assignChildren(*bus, "load");
                    }
                }
            }
        }
        if (updated == 0) {
            throw ActionError(ActionError::Kind::TARGET,
                              std::format("no '{}' objects accepted field '{}'", type, selector));
        }
    }

    std::string getActionValue(std::string_view selector, const CoreObject& root)
    {
        const auto info = actionField(selector, root);
        const auto* object = info.mObject;
        const bool hasUnit = !units::is_default(info.mUnitType);

        double number = kNullVal;
        try {
            number = object->get(info.mField, info.mUnitType);
        }
        catch (const InvalidParameterValue& error) {
            throw ActionError(ActionError::Kind::TARGET, error.what());
        }
        if (number != kNullVal) {
            if (hasUnit && std::isnan(number)) {
                throw ActionError(ActionError::Kind::TARGET,
                                  std::format("incompatible unit in '{}'", selector));
            }
            return std::format("{}\n", number);
        }

        auto value = object->getString(info.mField);
        // The legacy string getter uses "NA" for unavailable fields. The built-in
        // names and readable string declarations can also hold that literal value.
        if (value == "NA" && info.mField != "name" && info.mField != "description" &&
            (info.mField != "parent" || object->getParent() == nullptr)) {
            const auto* field =
                object->getModelDescription().find(info.mField, ModelFieldKind::PARAMETER);
            if (field == nullptr || !field->readable() ||
                field->valueType() != ModelFieldType::STRING) {
                throw ActionError(ActionError::Kind::TARGET,
                                  std::format("field '{}' is unavailable", selector));
            }
            value = std::get<std::string>(field->value(*object));
        }
        if (hasUnit) {
            throw ActionError(ActionError::Kind::TARGET,
                              std::format("string field '{}' does not accept units", selector));
        }
        return formatActionString(value);
    }

    void printGetActionHelp(std::string_view selector, const CoreObject& root)
    {
        const auto* object = selector.empty() ? &root : locateObject(selector, &root);
        if (object == nullptr) {
            throw ActionError(ActionError::Kind::TARGET,
                              std::format("target '{}' was not found", selector));
        }
        std::cout << "get <field|object:field> [file]\n"
                     "get --help [object]\n"
                     "Use field(unit) to request units for numeric fields.\n";
        if (selector.empty()) {
            return;
        }
        const auto& description = object->getModelDescription();
        std::cout
            << std::format("Declared fields on {} ({})\n", object->getName(), description.name());
        for (const auto* layer = &description; layer != nullptr; layer = layer->parent()) {
            for (const auto& field : layer->fields()) {
                if (field.readable() && description.find(field.name()) == &field &&
                    (field.kind() == ModelFieldKind::PARAMETER ||
                     field.kind() == ModelFieldKind::FLAG)) {
                    std::cout << std::format("  {}\t{}\n", field.name(), field.description());
                }
            }
        }
    }

    std::shared_ptr<Event>
        makeActionEvent(std::string_view description, CoreObject* root, CoreTime earliestTime)
    {
        EventInfo info;
        try {
            info.loadString(description, root);
        }
        catch (const InvalidParameterValue& error) {
            throw ActionError(ActionError::Kind::SYNTAX, error.what());
        }
        if (!info.file.empty()) {
            throw ActionError(
                ActionError::Kind::UNSUPPORTED,
                "add event requires inline values; use a case file for event playback from a file");
        }
        if (info.fieldList.empty() ||
            std::ranges::any_of(info.fieldList, [](const auto& field) { return field.empty(); })) {
            throw ActionError(ActionError::Kind::SYNTAX, "add event requires a field name");
        }
        if (std::ranges::any_of(info.targetObjs,
                                [](const auto* target) { return target == nullptr; })) {
            throw ActionError(ActionError::Kind::TARGET, "event target was not found");
        }
        if (std::ranges::any_of(info.units,
                                [](const auto& unit) { return units::is_error(unit); })) {
            throw ActionError(ActionError::Kind::SYNTAX, "invalid event units");
        }

        const auto& times = info.time;
        const auto unorderedTime = [](CoreTime first, CoreTime second) { return first >= second; };
        if (times.empty() || times.front() == negTime || times.back() == maxTime ||
            std::ranges::adjacent_find(times, unorderedTime) != times.end()) {
            throw ActionError(ActionError::Kind::SYNTAX,
                              "add event requires strictly increasing, finite trigger times");
        }
        if (times.front() < earliestTime) {
            throw ActionError(ActionError::Kind::EXECUTION,
                              "event time precedes the current simulation or start time");
        }
        if (info.period < timeZero || info.period == maxTime ||
            (info.period > timeZero && info.period <= times.back() - times.front())) {
            throw ActionError(ActionError::Kind::SYNTAX,
                              "event period must exceed the span of its trigger times");
        }
        const auto fieldCount = info.fieldList.size();
        const auto valueCount = info.value.size();
        if (fieldCount == 1 ?
                (valueCount != 1 && valueCount != times.size()) :
                (times.size() != 1 || valueCount != fieldCount || info.period > timeZero)) {
            throw ActionError(ActionError::Kind::SYNTAX,
                              "event times, fields, and values do not match");
        }

        auto event = makeEvent(info, root);
        if (!event || !event->isArmed()) {
            throw ActionError(ActionError::Kind::TARGET,
                              "unable to arm event for the given target");
        }
        return std::shared_ptr<Event>{std::move(event)};
    }

    std::string_view continuationStatusName(ContinuationStatus status)
    {
        switch (status) {
            case ContinuationStatus::TARGET_REACHED:
                return "target_reached";
            case ContinuationStatus::NOSE_FOUND:
                return "nose_found";
            case ContinuationStatus::NOSE_BEFORE_TARGET:
                return "nose_before_target";
            case ContinuationStatus::Q_LIMIT_TURN:
                return "q_limit_turn";
            case ContinuationStatus::BASE_SOLVE_FAILED:
                return "base_solve_failed";
            case ContinuationStatus::STEP_LIMIT:
                return "step_limit";
            case ContinuationStatus::NUMERICAL_FAILURE:
            default:
                return "numerical_failure";
        }
    }

    void runActionContinuation(GridDynSimulation& simulation, const GridDynAction& cmd)
    {
        if (cmd.val_double == kNullVal) {
            throw ActionError(ActionError::Kind::SYNTAX, "continuation requires a load scale");
        }
        ContinuationRequest request;
        request.loadScale = cmd.val_double;
        if (cmd.val_double2 != kNullVal) {
            request.stopLambda = cmd.val_double2;
        }
        request.monitorBuses = cmd.monitorBuses;
        request.enforceQLimits = cmd.flag != 0;

        ContinuationResult result;
        try {
            result = simulation.continuationPowerFlow(request);
        }
        catch (const InvalidParameterValue& error) {
            std::throw_with_nested(ActionError(ActionError::Kind::EXECUTION, error.what()));
        }
        if (!cmd.string1.empty()) {
            saveContinuationPowerFlow(result, cmd.string1);
        }
        auto message = std::format("continuation {}", continuationStatusName(result.status));
        if (!result.points.empty()) {
            message += std::format(" lambda={}", result.points.back().lambda);
        }
        if (const auto limit = result.limitingLambda()) {
            message += std::format(" limiting_lambda={}", *limit);
        }
        if (result.targetLambda) {
            message += std::format(" target={}", *result.targetLambda);
        }
        if (!result.successful()) {
            throw ActionError(ActionError::Kind::EXECUTION, message);
        }
        std::cout << message << '\n';
    }

    GridDynSimulation::SolutionModes actionSolverRole(std::string_view name)
    {
        const auto role = gmlc::utilities::convertToLowerCase(name);
        using SolutionModes = GridDynSimulation::SolutionModes;
        if (role == "powerflow") {
            return SolutionModes::POWERFLOW_MODE;
        }
        if (role == "dae") {
            return SolutionModes::DAE_MODE;
        }
        if (role == "algebraic" || role == "dynalg") {
            return SolutionModes::ALGEBRAIC_MODE;
        }
        if (role == "differential" || role == "dyndiff") {
            return SolutionModes::DIFFERENTIAL_MODE;
        }
        throw ActionError(ActionError::Kind::SYNTAX, std::format("unknown solver mode '{}'", name));
    }

    int checkActionPowerFlow(GridDynSimulation& simulation, const GridDynAction& cmd)
    {
        if (cmd.string1.empty()) {
            throw ActionError(ActionError::Kind::SYNTAX, "check requires a subject");
        }
        if (gmlc::utilities::convertToLowerCase(cmd.string1) != "powerflow") {
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              std::format("unsupported check subject '{}'", cmd.string1));
        }
        std::vector<Violation> violations;
        simulation.pFlowCheck(violations);
        const auto writeViolations = [&violations](std::ostream& output) {
            for (const auto& violation : violations) {
                output << violation.to_string() << '\n';
            }
        };
        if (cmd.string2.empty()) {
            writeViolations(std::cout);
            if (!(std::cout << std::flush)) {
                return -3;
            }
        } else {
            std::ofstream output(cmd.string2, std::ios::app);
            if (!output) {
                return -3;
            }
            output << "Violations for " << simulation.getName() << " at "
                   << static_cast<double>(simulation.getSimulationTime()) << '\n';
            writeViolations(output);
            output.close();
            if (!output) {
                return -3;
            }
        }
        if (cmd.flag == 1 && !violations.empty()) {
            throw ActionError(ActionError::Kind::EXECUTION,
                              std::format("{} power-flow violation(s): {}",
                                          violations.size(),
                                          violations.front().to_string()));
        }
        return FUNCTION_EXECUTION_SUCCESS;
    }

    void runActionContingencies(GridDynSimulation& simulation, const GridDynAction& cmd)
    {
        auto mode = gmlc::utilities::convertToLowerCase(cmd.string1);
        if (mode == "gridload") {
            mode = "load";
        }
        const auto selection = getContingencyMode(mode);
        if (selection == ContingencyMode::UNKNOWN || selection == ContingencyMode::CUSTOM) {
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              std::format("unsupported contingency mode '{}'", cmd.string1));
        }
        if (cmd.string2.starts_with("database:")) {
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              "contingency database output is unsupported");
        }
        auto info = emptyExtraInfo;
        info.simplified = cmd.flag == 1;
        auto contingencies = buildContingencyList(&simulation, mode, info);
        if (contingencies.empty()) {
            throw ActionError(ActionError::Kind::TARGET,
                              std::format("no contingencies match '{}'", cmd.string1));
        }
        runContingencyAnalysis(contingencies, cmd.string2, cmd.val_int1, cmd.val_int2);
    }

    void saveActionOutput(GridDynSimulation& simulation, const GridDynAction& cmd)
    {
        const auto subject = gmlc::utilities::convertToLowerCase(cmd.string1);
        if (subject == "recorder") {
            if (!cmd.string2.empty()) {
                throw ActionError(ActionError::Kind::SYNTAX,
                                  "save recorder uses the configured recorder destinations");
            }
            for (const auto& collector : simulation.getCollectors()) {
                if (!collector->getSinkName().empty()) {
                    try {
                        collector->flush();
                    }
                    catch (const std::exception& error) {
                        throw ActionError(ActionError::Kind::EXECUTION,
                                          std::format("unable to flush recorder to '{}': {}",
                                                      collector->getSinkName(),
                                                      error.what()));
                    }
                }
            }
        } else if (subject == "state") {
            saveState(&simulation, cmd.string2);
        } else if (subject == "jacobian") {
            saveJacobian(&simulation,
                         (cmd.string2.empty()) ? "jacobian_" + simulation.getName() + ".bin" :
                                                 cmd.string2);
        } else if (subject == "powerflow") {
            savePowerFlow(&simulation, cmd.string2);
        } else if (subject == "powerflow-checkpoint") {
            if (cmd.string2.empty()) {
                throw ActionError(ActionError::Kind::SYNTAX,
                                  "save powerflow-checkpoint requires a file");
            }
            savePowerFlowCheckpoint(&simulation, cmd.string2);
        } else {
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              std::format("unsupported save subject '{}'", cmd.string1));
        }
    }

    void loadActionInput(GridDynSimulation& simulation, const GridDynAction& cmd)
    {
        const auto subject = gmlc::utilities::convertToLowerCase(cmd.string1);
        if (cmd.string2.empty()) {
            throw ActionError(ActionError::Kind::SYNTAX, "load requires a file");
        }
        if (subject == "state") {
            loadState(&simulation, cmd.string2);
        } else if (subject == "powerflow-checkpoint") {
            loadPowerFlowCheckpoint(&simulation, cmd.string2);
        } else {
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              std::format("unsupported load subject '{}'", cmd.string1));
        }
    }
}  // namespace

int GridDynSimulation::run(CoreTime tEnd)
{
    lastActionError.clear();
    if (actionQueue.empty()) {
        GridDynAction action(GridDynAction::GdAction::RUN);
        action.val_double = (tEnd == negTime) ? stopTime : tEnd;
        return execute(action);
    }
    return runActions(actionQueue, tEnd);
}

int GridDynSimulation::runActions(std::deque<QueuedAction>& queue, CoreTime tEnd)
{
    while (!queue.empty()) {
        auto entry = std::move(queue.front());
        queue.pop_front();

        const auto describeError = [&entry](std::string_view reason) {
            return describeActionError(entry.text, reason, entry.source);
        };
        int result = FUNCTION_EXECUTION_SUCCESS;
        bool paused = false;
        try {
            if (const auto* error = std::get_if<std::string>(&entry.command)) {
                throw ActionError(ActionError::Kind::SYNTAX, *error);
            }
            const auto& action = std::get<GridDynAction>(entry.command);
            if (action.command == GridDynAction::GdAction::ACTION_FILE) {
                expandScript(queue, entry);
                continue;
            }
            const auto actionEnd = entry.stepEnd ? entry.stepEnd : actionStopTime(action);
            paused = tEnd != negTime && actionEnd && *actionEnd > tEnd;
            if (paused && action.command == GridDynAction::GdAction::STEP) {
                entry.stepEnd = actionEnd;
            }
            if (!paused) {
                result = executeAction(action, actionEnd.value_or(stopTime));
            } else if (actionTimeOrigin(pState, currentTime, startTime) < tEnd) {
                bool endedAtEvent = false;
                result = executeAction(action, tEnd, &endedAtEvent);
                paused = !endedAtEvent;
            }
        }
        catch (const ActionError& error) {
            throw ActionError(error.kind(), describeError(error.what()));
        }
        catch (const std::exception& error) {
            std::throw_with_nested(
                ActionError(ActionError::Kind::EXECUTION, describeError(error.what())));
        }

        if (result != FUNCTION_EXECUTION_SUCCESS) {
            lastActionError = describeError(std::format("status {}", result));
            return result;
        }
        if (paused) {
            queue.push_front(std::move(entry));
            return FUNCTION_EXECUTION_SUCCESS;
        }
    }
    return FUNCTION_EXECUTION_SUCCESS;
}

void GridDynSimulation::add(std::string_view actionString)
{
    queueAction(actionQueue, actionString, {});
}

void GridDynSimulation::addActions(std::string_view actions, std::string_view source)
{
    queueActions(actionQueue, actions, source);
}

void GridDynSimulation::addScriptFile(std::string_view file, std::string_view source)
{
    actionQueue.push_back(scriptFileAction(file, source));
}

GridDynSimulation::QueuedAction GridDynSimulation::scriptFileAction(std::string_view file,
                                                                    std::string_view source)
{
    GridDynAction action(GridDynAction::GdAction::ACTION_FILE);
    action.string1 = file;
    return {.command = std::move(action),
            .text = std::format("action \"{}\"", file),
            .source = std::string(source),
            .script = {}};
}

void GridDynSimulation::expandScript(std::deque<QueuedAction>& queue, const QueuedAction& entry)
{
    const auto& file = std::get<GridDynAction>(entry.command).string1;
    if (file.empty()) {
        throw ActionError(ActionError::Kind::SYNTAX, "action requires a script file");
    }
    const auto includingFile =
        entry.script ? entry.script->mPaths.back() : std::filesystem::path(entry.source);
    const auto requested = includingFile.parent_path() / std::filesystem::path(file);
    std::error_code error;
    const auto path = std::filesystem::canonical(requested, error);
    if (error || !std::filesystem::is_regular_file(path, error)) {
        throw FileOperationError(
            std::format("unable to open script file '{}'", requested.string()));
    }
    auto script = std::make_shared<ActionScript>();
    if (entry.script) {
        const auto alreadyIncluded = [&path](const std::filesystem::path& ancestor) {
            std::error_code equivalentError;
            return ancestor == path || std::filesystem::equivalent(ancestor, path, equivalentError);
        };
        if (std::ranges::any_of(entry.script->mPaths, alreadyIncluded)) {
            throw FileOperationError(
                std::format("script include cycle involving '{}'", path.string()));
        }
        script->mPaths = entry.script->mPaths;
    }
    script->mPaths.push_back(path);

    std::ifstream input(path);
    if (!input) {
        throw FileOperationError(std::format("unable to open script file '{}'", path.string()));
    }
    std::deque<QueuedAction> batch;
    const auto source = path.string();
    std::size_t lineNumber = 0;
    for (std::string line; std::getline(input, line);) {
        ++lineNumber;
        if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }
        queueActions(batch, line, std::format("{}:{}", source, lineNumber), script);
    }
    if (input.bad() || !input.eof()) {
        throw FileOperationError(std::format("unable to read script file '{}'", source));
    }
    queue.insert(queue.begin(),
                 std::make_move_iterator(batch.begin()),
                 std::make_move_iterator(batch.end()));
}

void GridDynSimulation::queueActions(std::deque<QueuedAction>& queue,
                                     std::string_view actions,
                                     std::string_view source,
                                     const std::shared_ptr<const ActionScript>& script)
{
    for (const auto& line : gmlc::utilities::stringOps::splitline(actions, '\n')) {
        for (const auto command : action_detail::splitLine(line)) {
            queueAction(queue, command, source, script);
        }
    }
}

void GridDynSimulation::queueAction(std::deque<QueuedAction>& queue,
                                    std::string_view text,
                                    std::string_view source,
                                    const std::shared_ptr<const ActionScript>& script)
{
    const std::string commandText = gmlc::utilities::stringOps::trim(text);
    try {
        auto action = parseAction(commandText);
        if (action.command != GridDynAction::GdAction::IGNORE_ACTION) {
            queue.push_back({.command = std::move(action),
                             .text = commandText,
                             .source = std::string(source),
                             .script = script});
        }
    }
    catch (const ActionError& error) {
        // Report syntax errors in script order, after any preceding actions run.
        queue.push_back({.command = std::string(error.what()),
                         .text = commandText,
                         .source = std::string(source),
                         .script = script});
    }
}

void GridDynSimulation::add(GridDynAction& newAction)
{
    if (newAction.command != GridDynAction::GdAction::IGNORE_ACTION) {
        actionQueue.push_back({.command = newAction,
                               .text = std::string(newAction.commandName()),
                               .source = {},
                               .script = {}});
    }
}

int GridDynSimulation::execute(std::string_view cmd)
{
    try {
        return execute(parseAction(cmd));
    }
    catch (const ActionError& error) {
        throw ActionError(error.kind(), describeActionError(cmd, error.what()));
    }
    catch (const std::exception& error) {
        std::throw_with_nested(
            ActionError(ActionError::Kind::EXECUTION, describeActionError(cmd, error.what())));
    }
}

int GridDynSimulation::execute(const GridDynAction& cmd)
{
    if (cmd.command == GridDynAction::GdAction::ACTION_FILE) {
        lastActionError.clear();
        std::deque<QueuedAction> queue;
        queue.push_back(scriptFileAction(cmd.string1, {}));
        return runActions(queue, negTime);
    }
    return executeAction(cmd, actionStopTime(cmd).value_or(stopTime));
}

void GridDynSimulation::executeOpfAction(const GridDynAction& /*action*/)
{
#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
    throw ActionError(ActionError::Kind::UNSUPPORTED,
                      "opf requires an optimization simulation; use --optimization in the CLI");
#else
    throw ActionError(ActionError::Kind::UNSUPPORTED, "OPF is unavailable in this GridDyn build");
#endif
}

std::optional<CoreTime> GridDynSimulation::actionStopTime(const GridDynAction& action) const
{
    using Action = GridDynAction::GdAction;
    const auto from = actionTimeOrigin(pState, currentTime, startTime);
    const auto absoluteStop = [&](double value) {
        const auto until = actionTimeValue(value, stopTime, "stop time");
        if (until < from) {
            throw ActionError(ActionError::Kind::SYNTAX,
                              "stop time precedes the current simulation or start time");
        }
        return until;
    };
    switch (action.command) {
        case Action::RUN:
            if (controlFlags[POWER_FLOW_ONLY]) {
                return std::nullopt;
            }
            [[fallthrough]];
        case Action::DYNAMIC_DEFAULT:
        case Action::DYNAMIC_DAE:
        case Action::DYNAMIC_PART:
        case Action::DYNAMIC_DECOUPLED:
        case Action::EVENTMODE:
            return absoluteStop(action.val_double);
        case Action::ITERATE:
            return absoluteStop(action.val_double2);
        case Action::STEP: {
            const auto interval = actionInterval(action.val_double, stepTime);
            if (from >= maxTime - interval) {
                throw ActionError(ActionError::Kind::SYNTAX, "invalid step interval");
            }
            return from + interval;
        }
        default:
            return std::nullopt;
    }
}

void GridDynSimulation::requireDynamicSolverMethod(DynamicSolverMethods method) const
{
    if (method == DynamicSolverMethods::DECOUPLED) {
        throw ActionError(ActionError::Kind::UNSUPPORTED, "decoupled dynamics is unsupported");
    }
    if (method == DynamicSolverMethods::PARTITIONED) {
        if (!getSolverInterface(*defDynAlgMode) || !getSolverInterface(*defDynDiffMode)) {
            throw ActionError(ActionError::Kind::TARGET,
                              "partitioned dynamics requires configured algebraic and differential "
                              "solvers; set dynamicsolvermethod partitioned before running");
        }
        if (controlFlags[DAE_INITIALIZATION_FOR_PARTITIONED] &&
            pState <= GridState::DYNAMIC_INITIALIZED && !getSolverInterface(*defDAEMode)) {
            throw ActionError(ActionError::Kind::TARGET,
                              "partitioned DAE initialization requires a configured DAE solver");
        }
    } else if (!getSolverInterface(*defDAEMode) &&
               (defaultDynamicSolverMethod != DynamicSolverMethods::DAE ||
                pState >= GridState::DYNAMIC_INITIALIZED)) {
        throw ActionError(ActionError::Kind::TARGET,
                          "DAE dynamics requires a configured solver; set defdae or "
                          "dynamicsolvermethod dae before running");
    }
}

int GridDynSimulation::executeAction(const GridDynAction& cmd, CoreTime endTime, bool* endedAtEvent)
try {
    auto command = cmd.command;
    if (command == GridDynAction::GdAction::DYNAMIC_DEFAULT) {
        switch (defaultDynamicSolverMethod) {
            case DynamicSolverMethods::PARTITIONED:
                command = GridDynAction::GdAction::DYNAMIC_PART;
                break;
            case DynamicSolverMethods::DECOUPLED:
                command = GridDynAction::GdAction::DYNAMIC_DECOUPLED;
                break;
            case DynamicSolverMethods::DAE:
            default:
                command = GridDynAction::GdAction::DYNAMIC_DAE;
                break;
        }
    }
    int out = FUNCTION_EXECUTION_SUCCESS;
    switch (command) {
        case GridDynAction::GdAction::IGNORE_ACTION:
            break;
        case GridDynAction::GdAction::SET_ACTION: {
            const auto objectInfo = actionField(cmd.string1, *this);
            if (cmd.val_double == kNullVal) {
                if (!units::is_default(objectInfo.mUnitType)) {
                    throw ActionError(ActionError::Kind::SYNTAX,
                                      "a field with units requires a numeric value");
                }
                objectInfo.mObject->set(objectInfo.mField, cmd.string2);
            } else {
                objectInfo.mObject->set(objectInfo.mField, cmd.val_double, objectInfo.mUnitType);
            }
        } break;
        case GridDynAction::GdAction::GET_ACTION: {
            if (cmd.flag == 1) {
                printGetActionHelp(cmd.string1, *this);
                break;
            }
            const auto value = getActionValue(cmd.string1, *this);
            if (cmd.string2.empty()) {
                if (!(std::cout << value << std::flush)) {
                    throw ActionError(ActionError::Kind::EXECUTION, "unable to write get output");
                }
            } else {
                std::ofstream output(cmd.string2, std::ios::app);
                output << value;
                output.close();
                if (!output) {
                    throw ActionError(ActionError::Kind::EXECUTION,
                                      std::format("unable to write get output to '{}'",
                                                  cmd.string2));
                }
            }
        } break;
        case GridDynAction::GdAction::ADD:
            if (cmd.string1 != "event") {
                throw ActionError(ActionError::Kind::UNSUPPORTED,
                                  std::format("unsupported add subject '{}'", cmd.string1));
            }
            add(makeActionEvent(cmd.string2,
                                getRoot(),
                                pState == GridState::STARTUP ? startTime : getSimulationTime()));
            break;
        case GridDynAction::GdAction::SETALL:
            if (cmd.val_double == kNullVal || !std::isfinite(cmd.val_double)) {
                throw ActionError(ActionError::Kind::SYNTAX,
                                  "setall requires a finite numeric value");
            }
            setActionFields(*this,
                            gmlc::utilities::convertToLowerCase(cmd.string1),
                            cmd.string2,
                            cmd.val_double);
            break;
        case GridDynAction::GdAction::SETSOLVER: {
            const auto field = gmlc::utilities::convertToLowerCase(cmd.string2);
            if (cmd.val_double != kNullVal) {
                if (!std::isfinite(cmd.val_double)) {
                    throw ActionError(ActionError::Kind::SYNTAX, "solver setting must be finite");
                }
                solverSet(cmd.string1, field, cmd.val_double);
                break;
            }
            if (cmd.flag == 1) {
                solverSet(cmd.string1, field, cmd.setting);
                break;
            }
            const auto role = actionSolverRole(cmd.string1);
            auto indexText = std::string_view(cmd.string2);
            if (indexText.starts_with('+')) {
                indexText.remove_prefix(1);
            }
            index_t index = 0;
            const auto* begin = indexText.data();
            const auto* end = begin + indexText.size();
            const auto parsed = std::from_chars(begin, end, index);
            const auto* selected = (parsed.ec == std::errc{} && parsed.ptr == end) ?
                getSolverModePtr(index) :
                getSolverModePtr(cmd.string2);
            if (selected == nullptr || !getSolverInterface(selected->offsetIndex)) {
                throw ActionError(ActionError::Kind::TARGET,
                                  std::format("solver '{}' was not found", cmd.string2));
            }
            setDefaultMode(role, *selected);
        } break;
        case GridDynAction::GdAction::POWERFLOW:
            out = powerflow();
            break;
        case GridDynAction::GdAction::CONTINUATION:
            runActionContinuation(*this, cmd);
            break;
        case GridDynAction::GdAction::OPF:
            executeOpfAction(cmd);
            break;
        case GridDynAction::GdAction::CHECK:
            out = checkActionPowerFlow(*this, cmd);
            break;
        case GridDynAction::GdAction::CONTINGENCY:
            runActionContingencies(*this, cmd);
            break;
        case GridDynAction::GdAction::INITIALIZE: {
            const auto startTimeValue = actionTimeValue(cmd.val_double, startTime, "start time");

            if (pState < GridState::STARTUP) {
                throw ActionError(ActionError::Kind::EXECUTION,
                                  "cannot initialize a simulation in an error state");
            }
            if (pState == GridState::STARTUP) {
                out = pFlowInitialize(startTimeValue);
                if (out == 1) {
                    logging::error(this, "unable to initialize powerflow");
                    out = FUNCTION_EXECUTION_FAILURE;
                }
            } else if (pState == GridState::POWERFLOW_COMPLETE) {
                if (cmd.val_double != kNullVal &&
                    startTimeValue < std::max(startTime, currentTime)) {
                    throw ActionError(ActionError::Kind::SYNTAX,
                                      "initialization time precedes the current time");
                }
                requireDynamicSolverMethod(defaultDynamicSolverMethod);
                out = dynInitialize(std::max(startTimeValue, currentTime));
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "unable to complete dynamic power initialization");
                    return FUNCTION_EXECUTION_FAILURE;
                }
            } else if (cmd.val_double != kNullVal && startTimeValue != currentTime) {
                throw ActionError(ActionError::Kind::SYNTAX,
                                  "initialize cannot change the time in the current state");
            }
        } break;
        case GridDynAction::GdAction::ITERATE: {
            const auto timeStepValue = actionInterval(cmd.val_double, stepTime);
            out = eventDrivenPowerflow(endTime, timeStepValue);
        } break;
        case GridDynAction::GdAction::EVENTMODE:
            out = eventDrivenPowerflow(endTime, actionInterval(cmd.val_double2, stepTime));
            break;
        case GridDynAction::GdAction::DYNAMIC_DAE:
        case GridDynAction::GdAction::DYNAMIC_PART: {
            const auto method = command == GridDynAction::GdAction::DYNAMIC_DAE ?
                DynamicSolverMethods::DAE :
                DynamicSolverMethods::PARTITIONED;
            if (method == DynamicSolverMethods::DAE && cmd.val_double2 != kNullVal) {
                throw ActionError(ActionError::Kind::SYNTAX,
                                  "DAE dynamics does not take an interval");
            }
            const auto interval = method == DynamicSolverMethods::PARTITIONED ?
                actionInterval(cmd.val_double2, stepTime) :
                timeZero;
            requireDynamicSolverMethod(method);
            if (pState < GridState::DYNAMIC_INITIALIZED) {
                out = initializeDynamics(actionTimeOrigin(pState, currentTime, startTime), method);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                throw ActionError(ActionError::Kind::EXECUTION,
                                  "dynamics requires differential states");
            }
            if (method == DynamicSolverMethods::DAE) {
                out = dynamicDAE(endTime);
            } else {
                out = dynamicPartitioned(endTime, interval);
            }
        } break;
        case GridDynAction::GdAction::DYNAMIC_DECOUPLED:
            throw ActionError(ActionError::Kind::UNSUPPORTED, "decoupled dynamics is unsupported");
        case GridDynAction::GdAction::STEP: {
            if (!cmd.string1.empty() && gmlc::utilities::convertToLowerCase(cmd.string1) != "dae") {
                throw ActionError(ActionError::Kind::UNSUPPORTED, "step supports the DAE solver");
            }
            requireDynamicSolverMethod(DynamicSolverMethods::DAE);
            CoreTime timeActual = currentTime;
            if (pState < GridState::DYNAMIC_INITIALIZED) {
                out = initializeDynamics(actionTimeOrigin(pState, currentTime, startTime),
                                         DynamicSolverMethods::DAE);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                throw ActionError(ActionError::Kind::EXECUTION,
                                  "step requires differential states");
            }
            if (!getSolverInterface(*defDAEMode)->isInitialized()) {
                out = reInitDyn(*defDAEMode);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }
            bool stepEndedAtEvent = false;
            out = advanceDaeStep(endTime, timeActual, stepEndedAtEvent);
            if (endedAtEvent != nullptr) {
                *endedAtEvent = stepEndedAtEvent;
            }
        } break;
        case GridDynAction::GdAction::RUN:
            if (controlFlags[IDA_INTEGRATION_DIAGNOSTICS_FLAG]) {
                logging::logTo(this,
                               this,
                               PrintLevel::SUMMARY,
                               "IDA run dispatch: state={}, current_time={}, returned_time={}, "
                               "stop_time={}, differential_states={}, roots_disabled={}",
                               static_cast<int>(pState),
                               static_cast<double>(currentTime),
                               static_cast<double>(timeReturn),
                               static_cast<double>(endTime),
                               diffSize(*defDAEMode),
                               static_cast<bool>(controlFlags[ROOTS_DISABLED]));
            }
            if (controlFlags[POWER_FLOW_ONLY]) {
                return powerflow();
            }
            requireDynamicSolverMethod(defaultDynamicSolverMethod);
            if (pState < GridState::DYNAMIC_INITIALIZED) {
                out = dynInitialize(actionTimeOrigin(pState, currentTime, startTime));
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (hasDynamics()) {
                switch (defaultDynamicSolverMethod) {
                    case DynamicSolverMethods::DAE:
                    default:
                        out = dynamicDAE(endTime);
                        break;
                    case DynamicSolverMethods::PARTITIONED:
                        out = dynamicPartitioned(endTime, actionInterval(kNullVal, stepTime));
                        break;
                    case DynamicSolverMethods::DECOUPLED:
                        throw ActionError(ActionError::Kind::UNSUPPORTED,
                                          "decoupled dynamics is unsupported");
                }
            } else {
                logging::summary(this, "No Differential states reverting to stepped power flow");
                out = eventDrivenPowerflow(endTime, actionInterval(kNullVal, stepTime));
            }

            if (getSimulationTime() >= stopTime) {
                saveRecorders();
            }
            break;
        case GridDynAction::GdAction::RESET:
            if (cmd.val_int1 < -1 || cmd.val_int1 > static_cast<int>(ResetLevels::FULL)) {
                throw ActionError(ActionError::Kind::SYNTAX, "reset level must be 0 through 4");
            }
            reset(cmd.val_int1 < 0 ? ResetLevels::MINIMAL : static_cast<ResetLevels>(cmd.val_int1));
            break;
        case GridDynAction::GdAction::SAVE:
            saveActionOutput(*this, cmd);
            break;
        case GridDynAction::GdAction::LOAD:
            loadActionInput(*this, cmd);
            break;
        case GridDynAction::GdAction::CHECKPOINT: {
            if (cmd.string1.empty()) {
                throw ActionError(ActionError::Kind::SYNTAX, "checkpoint requires a file");
            }
            const auto* mode =
                cmd.string2.empty() ? &cEmptySolverMode : getSolverModePtr(cmd.string2);
            if (mode == nullptr) {
                throw ActionError(ActionError::Kind::TARGET,
                                  std::format("solver mode '{}' was not found", cmd.string2));
            }
            checkpointCapture(cmd.string1, *mode);
        } break;
        case GridDynAction::GdAction::ROLLBACK: {
            if (cmd.string1.empty()) {
                throw ActionError(ActionError::Kind::SYNTAX, "rollback requires a file");
            }
            if (!cmd.string2.empty() && cmd.string2 != "local" && cmd.string2 != "minimal") {
                throw ActionError(ActionError::Kind::SYNTAX,
                                  "rollback level must be local or minimal");
            }
            const auto level = cmd.string2 == "minimal" ? CheckpointRestoreLevel::MINIMAL :
                                                          CheckpointRestoreLevel::LOCAL_STATE;
            checkpointRestore(cmd.string1, level);
        } break;
        default:
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              std::format("unsupported command '{}'", cmd.commandName()));
    }
    return out;
}
catch (const InvalidParameterValue& error) {
    std::throw_with_nested(ActionError(ActionError::Kind::SYNTAX, error.what()));
}
catch (const UnrecognizedParameter& error) {
    std::throw_with_nested(ActionError(ActionError::Kind::SYNTAX, error.what()));
}

}  // namespace griddyn
