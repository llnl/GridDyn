/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridSimulation.h"

#include "../Area.h"
#include "../Generator.h"
#include "../Link.h"
#include "../Relay.h"
#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../gridBus.h"
#include "../loads/zipLoad.h"
#include "../measurement/collector.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/logger.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
gridSimulation::gridSimulation(const std::string& objName): Area(objName), simulationTime(timeZero)
{
    EvQ = std::make_unique<eventQueue>();
#ifdef ENABLE_MULTITHREADING
    gridLog = std::make_unique<helics::Logger>();
#else
    gridLog = std::make_unique<helics::LoggerNoThread>();
#endif
}

gridSimulation::~gridSimulation()
{
    opFlags.set(
        being_deleted);  // set this flag to handle some unusual circumstances with extra objects
}

coreObject* gridSimulation::clone(coreObject* obj) const
{
    auto* sim = cloneBase<gridSimulation, Area>(this, obj);
    if (sim == nullptr) {
        return obj;
    }
    sim->stopTime = stopTime;
    sim->currentTime = currentTime;
    sim->startTime = startTime;
    sim->stepTime = stepTime;
    sim->recordStart = recordStart;
    sim->recordStop = recordStop;
    sim->nextRecordTime = nextRecordTime;
    sim->alertCount = alertCount;
    sim->pState = pState;
    sim->state_record_period = state_record_period;
    sim->consolePrintLevel = consolePrintLevel;
    sim->errorCode = errorCode;

    sim->sourceFile = sourceFile;  // main source file  name
    sim->minUpdateTime =
        minUpdateTime;  // minimum time period to go between updates; for the hybrid simultaneous
    // partitioned solution
    sim->maxUpdateTime = maxUpdateTime;  //(s) max time period to go between updates
    sim->absTime = absTime;  // [s] seconds in UNIX time;

    EvQ->cloneTo(sim->EvQ.get());
    sim->EvQ->mapObjectsOnto(sim);
    // TODO(PT):: mapping the collectors
    return sim;
}

void gridSimulation::setErrorCode(int ecode)
{
    pState = ((ecode == GS_NO_ERROR) ? pState : gridState_t::GD_ERROR), errorCode = ecode;
}

void gridSimulation::add(std::shared_ptr<collector> col)
{
    if (!recordDirectory.empty()) {
        col->set("directory", recordDirectory);
    }
    EvQ->insert(col);
    collectorList.push_back(std::move(col));
}

void gridSimulation::add(std::shared_ptr<Event> evnt)
{
    EvQ->insert(std::move(evnt));
}

void gridSimulation::add(std::shared_ptr<eventAdapter> eA)
{
    EvQ->insert(std::move(eA));
}

void gridSimulation::add(const std::vector<std::shared_ptr<Event>>& elist)
{
    for (const auto& ev : elist) {
        EvQ->insert(ev);
    }
}

void gridSimulation::getEventObjects(std::vector<coreObject*>& objV) const
{
    EvQ->getEventObjects(objV);
}

int gridSimulation::run(coreTime /*finishTime*/)
{
    return FUNCTION_EXECUTION_FAILURE;
}

int gridSimulation::step()
{
    return FUNCTION_EXECUTION_FAILURE;
}

void gridSimulation::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    Area::timestep(time, inputs, sMode);
    EvQ->executeEvents(time);
}

void gridSimulation::saveRecorders()
{
    // save the recorder files
    for (auto& col : collectorList) {
        try {
            col->flush();
            logging::normal(this, "collector successfully flushed to: {}", col->getSinkName());
        }
        catch (const std::exception& e) {
            logging::error(this,
                           "unable to flush collector {} (to {}): {}",
                           col->getName(),
                           col->getSinkName(),
                           e.what());
        }
    }
}

static const char consoleprint[] = "consoleprintlevel";
void gridSimulation::set(std::string_view param, std::string_view val)
{
    std::string temp;
    if ((param == "recorddirectory") || (param == "outputdirectory")) {
        recordDirectory = val;
        for (auto& col : collectorList) {
            col->set("directory", recordDirectory);
        }
    } else if (param == "logprintlevel") {
        temp = gmlc::utilities::convertToLowerCase(val);
        logPrintLevel = stringToPrintLevel(temp);
    } else if (param == consoleprint) {
        temp = gmlc::utilities::convertToLowerCase(val);
        consolePrintLevel = stringToPrintLevel(temp);
    } else if (param == "version") {
        version = val;
    } else if (param == "printlevel") {
        temp = gmlc::utilities::convertToLowerCase(val);
        consolePrintLevel = stringToPrintLevel(temp);
        logPrintLevel = consolePrintLevel;
    } else if (param == "logfile") {
        logFile = val;
        gridLog->openFile(std::string{val});
    } else if (param == "statefile") {
        stateFile = val;
    } else if (param == "sourcefile") {
        sourceFile = val;
    } else {
        Area::set(param, val);
    }
}

std::string gridSimulation::getString(std::string_view param) const
{
    if (param == "logfile") {
        return logFile;
    }
    if (param == "statefile") {
        return stateFile;
    }
    if (param == "sourcefile") {
        return sourceFile;
    }
    if (param == "version") {
        return version;
    }
    return Area::getString(param);
}

void gridSimulation::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "timestart") || (param == "start") || (param == "starttime")) {
        startTime = units::convert(val, unitType, units::second);
    } else if ((param == "abstime") || (param == "walltime")) {
        absTime = val;
    } else if ((param == "stoptime") || (param == "stop") || (param == "timestop")) {
        stopTime = units::convert(val, unitType, units::second);
    } else if (param == "printlevel") {
        auto testLevel = static_cast<print_level>(static_cast<int>(val));
        if ((testLevel > print_level::trace) || (testLevel < print_level::no_print)) {
            throw(invalidParameterValue(std::string{param}));
        }
        consolePrintLevel = testLevel;
        logPrintLevel = testLevel;
    } else if (param == consoleprint) {
        auto testLevel = static_cast<print_level>(static_cast<int>(val));
        if ((testLevel > print_level::trace) || (testLevel < print_level::no_print)) {
            throw(invalidParameterValue(std::string{param}));
        }
        consolePrintLevel = testLevel;
        gridLog->changeLevels(static_cast<int>(consolePrintLevel), static_cast<int>(logPrintLevel));
    } else if (param == "logprintlevel") {
        auto testLevel = static_cast<print_level>(static_cast<int>(val));
        if ((testLevel > print_level::trace) || (testLevel < print_level::no_print)) {
            throw(invalidParameterValue(std::string{param}));
        }
        logPrintLevel = testLevel;
        gridLog->changeLevels(static_cast<int>(consolePrintLevel), static_cast<int>(logPrintLevel));
    } else if ((param == "steptime") || (param == "step") || (param == "timestep")) {
        stepTime = units::convert(val, unitType, units::second);
    } else if ((param == "minupdatetime")) {
        minUpdateTime = units::convert(val, unitType, units::second);
    } else if (param == "maxupdatetime") {
        maxUpdateTime = units::convert(val, unitType, units::second);
    } else if (param == "staterecordperiod") {
        state_record_period = units::convert(val, unitType, units::second);
    } else if (param == "recordstop") {
        recordStop = units::convert(val, unitType, units::second);
    } else if (param == "version") {
        version = std::to_string(val);
    } else if (param == "recordstart") {
        recordStart = units::convert(val, unitType, units::second);
    } else {
        Area::set(param, val, unitType);
    }
}

// find collector
std::shared_ptr<collector> gridSimulation::findCollector(const std::string& collectorName)
{
    for (auto& col : collectorList) {
        if (collectorName == col->getName()) {
            return col;
        }

        if (collectorName == col->getSinkName()) {
            return col;
        }
    }
    auto ind = gmlc::utilities::stringOps::trailingStringInt(collectorName);
    if (isValidIndex(ind, collectorList)) {
        return collectorList[ind];
    }
    return nullptr;
}

void gridSimulation::log(coreObject* object, print_level level, const std::string& message)
{
    if ((level > consolePrintLevel) && (level > logPrintLevel)) {
        return;
    }
    if (!(customLogger)) {
        if (!gridLog->isRunning()) {
            gridLog->startLogging(static_cast<int>(consolePrintLevel),
                                  static_cast<int>(logPrintLevel));
        }
    }

    if (object == nullptr) {
        object = this;
    }
    std::string cname = '[' +
        (isSameObject(object, this) ?
             "sim" :
             (fullObjectName(object) + '(' + std::to_string(object->getUserID()) + ')')) +
        ']';
    std::string simtime = ((currentTime > negTime) ? '(' + std::to_string(currentTime) + ')' :
                                                     std::string("(PRESTART)"));
    std::string key;
    if (level == print_level::warning) {
        key = "||WARNING||";
        ++warnCount;
    } else if (level == print_level::error) {
        key = "||ERROR||";
        ++errorCount;
    }
    // drop the preliminary information in a certain circumstance
    if (level == print_level::summary) {
        if (isSameObject(object, this)) {
            if (currentTime == timeZero) {
                if (!customLogger) {
                    gridLog->log(static_cast<int>(level), message);
                } else {
                    customLogger(static_cast<int>(level), message);
                }
                return;
            }
        }
    }

    if (!customLogger) {
        gridLog->log(static_cast<int>(level), simtime + cname + "::" + key + message);
    } else {
        customLogger(static_cast<int>(level), simtime + cname + "::" + key + message);
    }
}

bool gridSimulation::shouldLog(print_level level) const
{
    return !((level > consolePrintLevel) && (level > logPrintLevel));
}

static const std::map<int, std::string> alertStrings{
    {TRANSLINE_ANGLE_TRIP, "angle limit trip"},
    {TRANSLINE_LIMIT_TRIP, "transmission line limit trip"},
    {GENERATOR_UNDERFREQUENCY_TRIP, "generator underfrequency trip"},
    {GENERATOR_OVERSPEED_TRIP, "generator overspeed trip"},
    {GENERATOR_FAULT, "generator fault"},
    {BUS_UNDER_POWER, "bus under power"},
    {BUS_UNDER_VOLTAGE, "bus low Voltage"},
    {BUS_UNDER_FREQUENCY, "bus under frequency"},
    {LOAD_TRIP, "load trip"},
    {UNDERFREQUENCY_LOAD_TRIP, "underfrequency load trip"},
    {SWITCH1_CLOSE, "Switch 1 close"},
    {SWITCH1_OPEN, "Switch 1 open"},
    {SWITCH2_CLOSE, "Switch 2 close"},
    {SWITCH2_OPEN, "Switch 2 open"},
    {SWITCH_OPEN, "Switch open"},
    {SWITCH_CLOSE, "switch close"},
    {FUSE1_BLOWN_ANGLE, "fuse 1 blown from angle limit"},
    {FUSE1_BLOWN_CURRENT, "fuse 1 blown from current limit"},
    {FUSE_BLOWN_CURRENT, "fuse blown from current limit"},
    {FUSE_BLOWN_ANGLE, "fuse blown from angle limit"},
    {FUSE_BLOWN, "fuse blown"},
    {FUSE2_BLOWN_ANGLE, "fuse 2 blown from angle limit"},
    {FUSE2_BLOWN_CURRENT, "fuse 2 blown from current limit"},
    {BREAKER1_TRIP_CURRENT, "breaker 1 blown from current limit"},
    {BREAKER1_TRIP_ANGLE, "breaker 1 blown from angle limit"},
    {BREAKER2_TRIP_CURRENT, "breaker 1 blown from current limit"},
    {BREAKER2_TRIP_ANGLE, "breaker 1 blown from angle limit"},
    {BREAKER_TRIP_CURRENT, "breaker trip from current limit"},
    {BREAKER_TRIP_ANGLE, "breaker trip from angle limit"},
    {BREAKER_TRIP, "breaker trip"},
    {BREAKER1_RECLOSE, "breaker 1 reclose"},
    {BREAKER2_RECLOSE, "breaker 2 reclose"},
    {BREAKER_RECLOSE, "breaker reclose"},

};

void gridSimulation::alert(coreObject* object, int code)
{
    if (code > MAX_CHANGE_ALERT) {
        switch (code) {
            case UPDATE_TIME_CHANGE:
                EvQ->recheck();
                break;
            case UPDATE_REQUIRED:
                EvQ->insert(object);
                break;
            case UPDATE_NOT_REQUIRED:
                break;
            case OBJECT_NAME_CHANGE:
            case OBJECT_ID_CHANGE:
            case OBJECT_IS_SEARCHABLE:
                Area::alert(object, code);
            default:
                break;
        }
    } else if (code < MIN_CHANGE_ALERT) {
        alertCount++;
        std::string astr;
        auto res = alertStrings.find(code);
        if (res != alertStrings.end()) {
            astr = res->second;
            log(object, print_level::summary, astr);
        } else {
            std::string message = "Unrecognized alert code (" + std::to_string(code) + ')';
            log(object, print_level::summary, message);
        }
    }
}

double gridSimulation::get(std::string_view param, units::unit unitType) const
{
    count_t ival = kInvalidCount;
    double fval = kNullVal;
    if ((param == "collectorcount") || (param == "recordercount")) {
        ival = static_cast<count_t>(collectorList.size());
    } else if (param == "alertcount") {
        ival = alertCount;
    } else if (param == "eventcount") {
        ival = EvQ->size() - 1;
    } else if (param == "warncount") {
        ival = warnCount;
    } else if (param == "errorcount") {
        ival = errorCount;
    } else if (param == "logprintlevel") {
        ival = static_cast<int>(logPrintLevel);
    } else if ((param == "consoleprintlevel") || (param == "printlevel")) {
        ival = static_cast<int>(consolePrintLevel);
    } else if ((param == "stepsize") || (param == "steptime")) {
        fval = units::convert(stepTime, units::second, unitType);
    } else if ((param == "stop") || (param == "stoptime")) {
        fval = units::convert(stopTime, units::second, unitType);
    } else if ((param == "currenttime") || (param == "time")) {
        fval = units::convert(getSimulationTime(), units::second, unitType);
    } else if (param == "starttime") {
        fval = units::convert(getStartTime(), units::second, unitType);
    } else if (param == "eventtime") {
        fval = units::convert(getEventTime(), units::second, unitType);
    } else if (param == "state") {
        fval = static_cast<double>(pState);
    } else {
        fval = Area::get(param, unitType);
    }
    return (ival != kInvalidCount) ? static_cast<double>(ival) : fval;
}

void gridSimulation::setLogger(std::function<void(int, const std::string&)> loggingFunction)
{
    customLogger = std::move(loggingFunction);
}

// TODO(PT):: this really shouldn't be a function,  but still debating alternative approaches to the
// need it addressed
void gridSimulation::resetObjectCounters()
{
    zipLoad::loadCount = 0;
    // Area::areaCount = 0;
    gridBus::busCount = 0;
    Link::linkCount = 0;
    Relay::relayCount = 0;
    Generator::genCount = 0;
}

coreTime gridSimulation::getEventTime() const
{
    return EvQ->getNextTime();
}

coreTime gridSimulation::getEventTime(int eventCode) const
{
    return EvQ->getNextTime(eventCode);
}

coreObject* findMatchingObject(coreObject* obj1, gridPrimary* src, gridPrimary* sec)
{
    if (obj1 == nullptr) {
        return nullptr;
    }
    if (isSameObject(obj1, src)) {
        return sec;
    }
    coreObject* obj2 = nullptr;
    if (dynamic_cast<gridSecondary*>(obj1) !=
        nullptr)  // we know it is a gen or load so it parent should be a bus
    {
        auto* bus = dynamic_cast<gridBus*>(obj1->getParent());
        auto* bus2 = getMatchingBus(bus, src, sec);
        if (bus2 != nullptr) {
            if (dynamic_cast<Generator*>(obj1) != nullptr) {
                obj2 = bus2->getGen(obj1->locIndex);
            } else if (dynamic_cast<zipLoad*>(obj1) != nullptr) {
                obj2 = bus2->getLoad(obj1->locIndex);
            }
        }
    } else if (dynamic_cast<gridBus*>(obj1) != nullptr) {
        obj2 = getMatchingBus(dynamic_cast<gridBus*>(obj1), src, sec);
    } else if (dynamic_cast<Area*>(obj1) != nullptr) {
        obj2 = getMatchingArea(dynamic_cast<Area*>(obj1), src, sec);
    } else if (dynamic_cast<Link*>(obj1) != nullptr) {
        obj2 = getMatchingLink(dynamic_cast<Link*>(obj1), src, sec);
    } else {
        // now we get ugly we are gridSecondary Object
        coreObject* pobj = findMatchingObject(obj1->getParent(), src, sec);
        if (pobj != nullptr) {  // this is an internal string sequence for this purpose, likely
                                // won't be documented
            obj2 = pobj->getSubObject("submodelcode", obj1->locIndex);
        }
    }
    return obj2;
}
}  // namespace griddyn
