/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "contingency.h"

#include "../events/Event.h"
#include "../gridDynSimulation.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <map>
#include <sstream>

namespace griddyn {
static const std::map<int, std::string> violation_map{
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
    {CONVERGENCE_FAILURE, "solver failed to converge"}};

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
    auto vfind = violation_map.find(violationCode);
    if (vfind != violation_map.end()) {
        violationString += vfind->second + '(' + std::to_string(violationCode) + ")]";
    } else {
        violationString += "unknown violation(" + std::to_string(violationCode) + ")]";
    }
    violationString += std::to_string(level) + "vs. " + std::to_string(limit) + " " +
        std::to_string(percentViolation) + "% violation";
    return violationString;
}

contingency_mode_t getContingencyMode(const std::string& mode)
{
    contingency_mode_t cmode = contingency_mode_t::unknown;
    if ((mode == "n_1") || (mode == "n-1")) {
        cmode = contingency_mode_t::N_1;
    } else if ((mode == "n_1_1") || (mode == "n-1-1")) {
        cmode = contingency_mode_t::N_1_1;
    } else if ((mode == "n_2") || (mode == "n-2")) {
        cmode = contingency_mode_t::N_2;
    } else if (mode == "line") {
        cmode = contingency_mode_t::line;
    } else if (mode == "bus") {
        cmode = contingency_mode_t::bus;
    } else if (mode == "gen") {
        cmode = contingency_mode_t::gen;
    } else if (mode == "load") {
        cmode = contingency_mode_t::load;
    } else if (mode == "custom") {
        cmode = contingency_mode_t::custom;
    }
    return cmode;
}

std::atomic_int Contingency::contingencyCount{0};

Contingency::Contingency(): future_ret(promise_val.get_future())
{
    id = ++contingencyCount;
    name = "contingency_" + std::to_string(id);
}

Contingency::Contingency(gridDynSimulation* sim, std::shared_ptr<Event> ge):
    gds(sim), future_ret(promise_val.get_future())
{
    id = ++contingencyCount;
    name = "contingency_" + std::to_string(id);
    eventList.resize(1);
    eventList[0].push_back(std::move(ge));
}

void Contingency::execute()
{
    auto contSim =
        std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(gds->clone()));
    contSim->set("printlevel", 0);
    int res = FUNCTION_EXECUTION_SUCCESS;
    for (auto& evList : eventList) {
        for (auto& ev : evList) {
            ev->updateObject(contSim.get(), object_update_mode::match);
            ev->trigger();
            ev->updateObject(
                gds, object_update_mode::match);  // map the event back to the original simulation
        }
        res = contSim->powerflow();
    }

    if (res == FUNCTION_EXECUTION_SUCCESS) {
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

void Contingency::add(std::shared_ptr<Event> ge, index_t stage)
{
    gmlc::utilities::ensureSizeAtLeast(eventList, stage + 1);
    eventList[stage].push_back(std::move(ge));
}

std::string Contingency::generateHeader() const
{
    std::stringstream ss;
    ss << "index, name, event";

    stringVec busNames;
    gds->getBusName(busNames);
    for (auto& bn : busNames) {
        ss << ", " << bn << ":V";
    }
    for (auto& bn : busNames) {
        ss << ", " << bn << ":A";
    }
    stringVec linkNames;
    gds->getLinkName(linkNames);
    for (auto& ln : linkNames) {
        ss << ", " << ln << ":flow";
    }
    ss << ", violations";
    return ss.str();
}

const std::string commaQuote = R"(, ")";
std::string Contingency::generateFullOutputLine() const
{
    std::stringstream ss;
    ss << id << ", " << name << commaQuote;
    for (auto& ev : eventList[0]) {
        ss << ev->to_string() << ';';
    }
    ss << '"';

    ss.precision(4);
    for (auto& bn : busVoltages) {
        ss << ", " << bn;
    }
    ss.precision(5);
    for (auto& bn : busAngles) {
        ss << ", " << bn;
    }
    ss.precision(4);

    for (auto& ln : Lineflows) {
        ss << ", " << ln;
    }
    ss << commaQuote;
    for (auto& viol : Violations) {
        ss << viol.to_string() << ';';
    }
    ss << '"';
    return ss.str();
}

std::string Contingency::generateViolationsOutputLine() const
{
    std::stringstream ss;
    ss << id << ", " << name << commaQuote;
    for (auto& ev : eventList[0]) {
        ss << ev->to_string() << ';';
    }
    ss << R"(", ")";
    for (auto& viol : Violations) {
        ss << viol.to_string() << ';';
    }
    ss << '"';
    return ss.str();
}

coreObject* Contingency::getObject() const
{
    return gds;
}

void Contingency::getObjects(std::vector<coreObject*>& objects) const
{
    for (auto& evL : eventList) {
        for (auto& evnt : evL) {
            evnt->getObjects(objects);
        }
    }
}

void Contingency::updateObject(coreObject* newObj, object_update_mode mode)
{
    // update all the events
    for (auto& evList : eventList) {
        for (auto& evnt : evList) {
            evnt->updateObject(newObj, mode);
        }
    }
    // update the simulation if appropriate
    if (mode == object_update_mode::match) {
        if (dynamic_cast<gridDynSimulation*>(newObj) != nullptr) {
            gds = static_cast<gridDynSimulation*>(newObj);
        }
    }
}

std::shared_ptr<Contingency> Contingency::clone(std::shared_ptr<Contingency> con) const
{
    auto newCont = con;
    if (!newCont) {
        con = std::make_shared<Contingency>(gds);
    }
    con->completed = false;
    con->name = name;
    for (int kk = 0; kk < static_cast<int>(eventList.size()); ++kk) {
        for (auto& evnt : eventList[kk]) {
            con->add(evnt->clone(), kk);
        }
    }
    return newCont;
}

}  // namespace griddyn
