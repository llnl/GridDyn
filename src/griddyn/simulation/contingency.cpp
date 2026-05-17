/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "contingency.h"

#include "../events/Event.h"
#include "../gridDynSimulation.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
namespace {
    using ViolationMapEntry = std::pair<int, std::string_view>;
    constexpr std::array<ViolationMapEntry, 11> kViolationMap{{
        {NO_VIOLATION, "no violation"},
        {VOLTAGE_OVER_LIMIT_VIOLATION, "voltage over limit"},
        {VOLTAGE_UNDER_LIMIT_VIOLATION, "voltage under limit"},
        {MVA_EXCEED_RATING_A, "MVA over limitA"},
        {MVA_EXCEED_RATING_B, "MVA over limitB"},
        {MVA_EXCEED_ERATING, "MVA over emergency limit"},
        {MINIMUM_ANGLE_EXCEEDED, "min angle exceeded"},
        {MAXIMUM_ANGLE_EXCEEDED, "max angle exceeded"},
        {MINIMUM_CURRENT_EXCEEDED, "current below low limit"},
        {MAXIMUM_CURRENT_EXCEEDED, "current max exceeded"},
        {CONVERGENCE_FAILURE, "solver failed to converge"},
    }};

    std::string_view getViolationText(int code) noexcept
    {
        for (const auto& entry : kViolationMap) {
            if (entry.first == code) {
                return entry.second;
            }
        }
        return {};
    }

    using ContingencyModeEntry = std::pair<std::string_view, ContingencyMode>;
    constexpr std::array<ContingencyModeEntry, 37> kContingencyModeMap{{
        {"n-1", ContingencyMode::N_1},           {"N-1", ContingencyMode::N_1},
        {"n_1", ContingencyMode::N_1},           {"N_1", ContingencyMode::N_1},
        {"n-1-1", ContingencyMode::N_1_1},       {"N-1-1", ContingencyMode::N_1_1},
        {"n_1_1", ContingencyMode::N_1_1},       {"N_1_1", ContingencyMode::N_1_1},
        {"n-2", ContingencyMode::N_2},           {"N-2", ContingencyMode::N_2},
        {"n_2", ContingencyMode::N_2},           {"N_2", ContingencyMode::N_2},
        {"n-2-line", ContingencyMode::N_2_LINE}, {"N-2-LINE", ContingencyMode::N_2_LINE},
        {"n_2_line", ContingencyMode::N_2_LINE}, {"N_2_LINE", ContingencyMode::N_2_LINE},
        {"N_2_Line", ContingencyMode::N_2_LINE}, {"n-3-line", ContingencyMode::N_3_LINE},
        {"N-3-LINE", ContingencyMode::N_3_LINE}, {"n_3_line", ContingencyMode::N_3_LINE},
        {"N_3_LINE", ContingencyMode::N_3_LINE}, {"N_3_Line", ContingencyMode::N_3_LINE},
        {"line", ContingencyMode::LINE},         {"Line", ContingencyMode::LINE},
        {"LINE", ContingencyMode::LINE},         {"gen", ContingencyMode::GEN},
        {"Gen", ContingencyMode::GEN},           {"GEN", ContingencyMode::GEN},
        {"bus", ContingencyMode::BUS},           {"Bus", ContingencyMode::BUS},
        {"BUS", ContingencyMode::BUS},           {"load", ContingencyMode::LOAD},
        {"Load", ContingencyMode::LOAD},         {"LOAD", ContingencyMode::LOAD},
        {"custom", ContingencyMode::CUSTOM},     {"Custom", ContingencyMode::CUSTOM},
        {"CUSTOM", ContingencyMode::CUSTOM},
    }};
}  // namespace

/*std::string m_objectName;        //the  name of the object with the violation
double level;        //the value of the parameter exceeding some limit
double limit;        //the limit value
double percentViolation;        //the violation percent;
int contingency_id;        //usually added later or ignored
int violationCode;      //a code representing the type of violation
int severity = 0;       //a code indicating the severity of the violation
*/
std::string Violation::to_string() const
{
    if (violationCode == 0) {
        return "";
    }
    std::string violationString = m_objectName + '[';
    const auto violationText = getViolationText(violationCode);
    if (!violationText.empty()) {
        violationString += std::string(violationText) + '(' + std::to_string(violationCode) + ")]";
    } else {
        violationString += "unknown violation(" + std::to_string(violationCode) + ")]";
    }
    violationString += std::to_string(level) + "vs. " + std::to_string(limit) + " " +
        std::to_string(percentViolation) + "% violation";
    return violationString;
}

ContingencyMode getContingencyMode(std::string_view mode)
{
    for (const auto& entry : kContingencyModeMap) {
        if (entry.first == mode) {
            return entry.second;
        }
    }
    return ContingencyMode::UNKNOWN;
}

std::atomic_int Contingency::contingencyCount{0};

Contingency::Contingency(): future_ret(promise_val.get_future())
{
    id = ++contingencyCount;
    name = "contingency_" + std::to_string(id);
}

Contingency::Contingency(gridDynSimulation* sim, std::shared_ptr<Event> gridEvent):
    gds(sim), future_ret(promise_val.get_future())
{
    id = ++contingencyCount;
    name = "contingency_" + std::to_string(id);
    eventList.resize(1);
    eventList[0].push_back(std::move(gridEvent));
}

void Contingency::execute()
{
    auto contSim =
        std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(gds->clone()));
    contSim->set("printlevel", 0);
    int res = FUNCTION_EXECUTION_SUCCESS;
    preEventLoad = contSim->getLoadReal();
    preEventGen = contSim->getGenerationReal();
    int stage = 0;
    for (auto& eventStageList : eventList) {
        for (auto& eventPtr : eventStageList) {
            if (!eventPtr) {
                continue;
            }
            eventPtr->updateObject(contSim.get(), object_update_mode::match);
            eventPtr->trigger();
            eventPtr->updateObject(
                gds, object_update_mode::match);  // map the event back to the original simulation
        }
        contSim->pFlowInitialize();
        if (stage == 0) {
            preContingencyLoad = contSim->getLoadReal();
            preContingencyGen = contSim->getGenerationReal();
        }
        res = contSim->powerflow();
        ++stage;
    }

    if (res == FUNCTION_EXECUTION_SUCCESS) {
        contingencyLoad = contSim->getLoadReal();
        contingencyGen = contSim->getGenerationReal();
        islands = contSim->get("islands");
        contSim->pFlowCheck(Violations);
        contSim->getVoltage(busVoltages);
        contSim->getAngle(busAngles);
        contSim->getLinkRealPower(Lineflows);
        lowV = *std::min_element(busVoltages.begin(), busVoltages.end());
    } else {
        Violations.emplace_back(contSim->getName(), CONVERGENCE_FAILURE);
    }

    completed.store(true, std::memory_order_release);
    promise_val.set_value(static_cast<int>(Violations.size()));
}
void Contingency::reset()
{
    completed.store(false);
    promise_val = std::promise<int>();
    future_ret = std::shared_future<int>(promise_val.get_future());
}

void Contingency::wait() const
{
    future_ret.wait();
}

std::future_status Contingency::wait_for(std::chrono::milliseconds waitTime) const
{
    return future_ret.wait_for(waitTime);
}

bool Contingency::isFinished() const
{
    return completed.load(std::memory_order_acquire);
}

void Contingency::setContingencyRoot(gridDynSimulation* gdSim)
{
    if (gds != gdSim) {
        gds = gdSim;
    }
}

void Contingency::add(std::shared_ptr<Event> gridEvent, index_t stage)
{
    gmlc::utilities::ensureSizeAtLeast(eventList, static_cast<std::size_t>(stage) + 1);
    eventList[stage].push_back(std::move(gridEvent));
}

void Contingency::merge(const Contingency& other, index_t stage)
{
    gmlc::utilities::ensureSizeAtLeast(eventList, static_cast<std::size_t>(stage) + 1);
    for (const auto& eventStageList : other.eventList) {
        for (const auto& eventPtr : eventStageList) {
            eventList[stage].push_back(eventPtr);
        }
    }
}

bool Contingency::mergeIfUnique(const Contingency& other, index_t stage)
{
    gmlc::utilities::ensureSizeAtLeast(eventList, static_cast<std::size_t>(stage) + 1);
    bool newEvent = false;
    for (const auto& eventStageList : other.eventList) {
        for (const auto& eventPtr : eventStageList) {
            bool matched = false;
            for (const auto& compEV : eventList[stage]) {
                if (!compEV) {
                    continue;
                }
                if (*eventPtr == *compEV) {
                    matched = true;
                }
            }
            if (!matched) {
                newEvent = true;
                eventList[stage].push_back(eventPtr);
            }
        }
    }
    return newEvent;
}

std::string Contingency::generateOutputLine() const
{
    return (simplifiedOutput) ? generateViolationsOutputLine() : generateFullOutputLine();
}

std::string Contingency::generateHeader() const
{
    std::stringstream stream;
    stream << "index, name, event";

    if (!simplifiedOutput) {
        stringVec busNames;
        gds->getBusName(busNames);
        for (const auto& busName : busNames) {
            stream << ", " << busName << ":V";
        }
        for (const auto& busName : busNames) {
            stream << ", " << busName << ":A";
        }
        stringVec linkNames;
        gds->getLinkName(linkNames);
        for (const auto& linkName : linkNames) {
            stream << ", " << linkName << ":flow";
        }
    }
    stream << ", min Voltage, islands, loadLossEvent, loadLoss, genLossEvent, genLoss, violations";
    return stream.str();
}

const char commaQuote[] = R"(, ")";

std::string Contingency::generateContingencyString() const
{
    std::stringstream stream;
    stream << id << ", " << name << commaQuote;
    for (const auto& eventPtr : eventList[0]) {
        if (eventPtr) {
            stream << eventPtr->to_string() << ';';
        }
    }
    stream << '"';
    return stream.str();
}

std::string Contingency::generateFullOutputLine() const
{
    std::stringstream stream;
    stream << id << ", " << name << commaQuote;
    for (const auto& eventPtr : eventList[0]) {
        if (eventPtr) {
            stream << eventPtr->to_string() << ';';
        }
    }
    stream << '"';

    stream.precision(4);
    for (const auto& busVoltage : busVoltages) {
        stream << ", " << busVoltage;
    }
    stream.precision(5);
    for (const auto& busAngle : busAngles) {
        stream << ", " << busAngle;
    }
    stream.precision(4);

    for (const auto& lineFlow : Lineflows) {
        stream << ", " << lineFlow;
    }
    stream << ", " << lowV;
    stream << ", " << islands;
    stream << ", " << preEventLoad - contingencyLoad;
    stream << ", " << preContingencyLoad - contingencyLoad;
    stream << ", " << preEventGen - contingencyGen;
    stream << ", " << preContingencyGen - contingencyGen;
    stream << commaQuote;
    for (const auto& violation : Violations) {
        stream << violation.to_string() << ';';
    }
    stream << '"';
    return stream.str();
}

std::string Contingency::generateViolationsOutputLine() const
{
    std::stringstream stream;
    stream << id << ", " << name << commaQuote;
    for (const auto& eventPtr : eventList[0]) {
        if (eventPtr) {
            stream << eventPtr->to_string() << ';';
        }
    }
    stream << ", " << lowV;
    stream << ", " << islands;
    stream << ", " << preEventLoad - contingencyLoad;
    stream << ", " << preContingencyLoad - contingencyLoad;
    stream << ", " << preEventGen - contingencyGen;
    stream << ", " << preContingencyGen - contingencyGen;
    stream << commaQuote;
    for (const auto& violation : Violations) {
        stream << violation.to_string() << ';';
    }
    stream << '"';
    return stream.str();
}

CoreObject* Contingency::getObject() const
{
    return gds;
}

void Contingency::getObjects(std::vector<CoreObject*>& objects) const
{
    for (const auto& stageEvents : eventList) {
        for (const auto& eventPtr : stageEvents) {
            if (eventPtr) {
                eventPtr->getObjects(objects);
            }
        }
    }
}

void Contingency::updateObject(CoreObject* newObj, object_update_mode mode)
{
    // update all the events
    for (auto& stageEvents : eventList) {
        for (auto& eventPtr : stageEvents) {
            if (eventPtr) {
                eventPtr->updateObject(newObj, mode);
            }
        }
    }
    // update the simulation if appropriate
    if (mode == object_update_mode::match) {
        if (dynamic_cast<gridDynSimulation*>(newObj) != nullptr) {
            gds = static_cast<gridDynSimulation*>(newObj);
        }
    }
}

std::shared_ptr<Contingency>
    Contingency::clone(const std::shared_ptr<Contingency>& existingContingency) const
{
    auto newContingency = existingContingency;
    if (!newContingency) {
        newContingency = std::make_shared<Contingency>(gds);
    }
    newContingency->completed = false;
    newContingency->name = name;
    for (std::size_t stageIndex = 0; stageIndex < eventList.size(); ++stageIndex) {
        for (const auto& eventPtr : eventList[stageIndex]) {
            if (eventPtr) {
                newContingency->add(eventPtr->clone(), static_cast<index_t>(stageIndex));
            }
        }
    }
    return newContingency;
}

}  // namespace griddyn

