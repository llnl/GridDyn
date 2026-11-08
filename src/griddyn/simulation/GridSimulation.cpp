/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridSimulation.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../Link.h"
#include "../Relay.h"
#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../loads/ZipLoad.h"
#include "../measurement/Collector.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/logger.h"
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
template<class CountType>
static void boundedIncrement(CountType& value)
{
    if (value < std::numeric_limits<CountType>::max()) {
        ++value;
    }
}

const ModelDescription& GridSimulation::getModelDescription() const
{
    static const auto description = ModelDescription::define<GridSimulation>(
        "GridSimulation",
        &GridArea::getModelDescription(),
        {command({"recorddirectory", "outputdirectory"},
                 units::defunit,
                 "Recorddirectory",
                 ModelFieldType::STRING),
         command({"logprintlevel"}, units::defunit, "Logprintlevel", ModelFieldType::STRING),
         command(
             {"consoleprintlevel"}, units::defunit, "Consoleprintlevel", ModelFieldType::STRING),
         command({"version"}, units::defunit, "Version", ModelFieldType::STRING),
         command({"printlevel"}, units::defunit, "Printlevel", ModelFieldType::STRING),
         command({"logfile"}, units::defunit, "Logfile", ModelFieldType::STRING),
         parameter({"statefile"},
                   &GridSimulation::stateFile,
                   units::defunit,
                   "Record file for the state"),
         parameter(
             {"sourcefile"}, &GridSimulation::sourceFile, units::defunit, "Main source file name"),
         parameter({"timestart", "start", "starttime"},
                   &GridSimulation::startTime,
                   units::second,
                   "Start time"),
         parameter({"abstime", "walltime"},
                   &GridSimulation::absTime,
                   units::defunit,
                   "Simulation start as a Unix timestamp"),
         parameter({"stoptime", "stop", "timestop"},
                   &GridSimulation::stopTime,
                   units::second,
                   "End time"),
         parameter({"steptime", "step", "timestep"},
                   &GridSimulation::stepTime,
                   units::second,
                   "Time step"),
         parameter({"minupdatetime"},
                   &GridSimulation::minUpdateTime,
                   units::second,
                   "Minimum time between updates"),
         parameter({"maxupdatetime"},
                   &GridSimulation::maxUpdateTime,
                   units::second,
                   "Maximum time between updates"),
         parameter({"staterecordperiod"},
                   &GridSimulation::state_record_period,
                   units::second,
                   "How often to record the state"),
         parameter(
             {"recordstop"}, &GridSimulation::recordStop, units::second, "Recorder stop time"),
         parameter(
             {"recordstart"}, &GridSimulation::recordStart, units::second, "Recorder start time")});
    return description;
}

GridSimulation::GridSimulation(const std::string& objName):
    GridArea(objName), simulationTime(timeZero)
{
    EvQ = std::make_unique<EventQueue>();
#ifdef ENABLE_MULTITHREADING
    gridLog = std::make_unique<helics::Logger>();
#else
    gridLog = std::make_unique<helics::LoggerNoThread>();
#endif
}

GridSimulation::~GridSimulation()
{
    opFlags.set(
        BEING_DELETED);  // set this flag to handle some unusual circumstances with extra objects
}

CoreObject* GridSimulation::clone(CoreObject* obj) const
{
    auto* sim = cloneBase<GridSimulation, GridArea>(this, obj);
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
    // GridArea::clone skips linkage remapping when cloning into this derived
    // simulation. Rebind references held by buses and secondary objects now
    // that the complete network has been copied.
    sim->updateObjectLinkages(sim);
    // TODO(PT):: mapping the collectors
    return sim;
}

void GridSimulation::setErrorCode(int ecode)
{
    pState = ((ecode == GS_NO_ERROR) ? pState : GridState::GD_ERROR), errorCode = ecode;
}

void GridSimulation::add(std::shared_ptr<Collector> col)
{
    if (!recordDirectory.empty()) {
        col->set("directory", recordDirectory);
    }
    EvQ->insert(col);
    collectorList.push_back(std::move(col));
}

void GridSimulation::add(std::shared_ptr<Event> evnt)
{
    EvQ->insert(std::move(evnt));
}

void GridSimulation::add(std::shared_ptr<EventAdapter> eventAdapter)
{
    EvQ->insert(std::move(eventAdapter));
}

void GridSimulation::add(const std::vector<std::shared_ptr<Event>>& elist)
{
    for (const auto& eventObject : elist) {
        EvQ->insert(eventObject);
    }
}

void GridSimulation::getEventObjects(std::vector<CoreObject*>& objV) const
{
    EvQ->getEventObjects(objV);
}

int GridSimulation::run(CoreTime /*finishTime*/)
{
    return FUNCTION_EXECUTION_FAILURE;
}

int GridSimulation::step()
{
    return FUNCTION_EXECUTION_FAILURE;
}

void GridSimulation::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    GridArea::timestep(time, inputs, sMode);
    EvQ->executeEvents(time);
}

void GridSimulation::saveRecorders()
{
    // save the recorder files
    for (auto& col : collectorList) {
        try {
            col->flush();
            logging::normal(this, "Collector successfully flushed to: {}", col->getSinkName());
        }
        catch (const std::exception& e) {
            logging::error(this,
                           "unable to flush Collector {} (to {}): {}",
                           col->getName(),
                           col->getSinkName(),
                           e.what());
        }
    }
}

static constexpr char consoleprint[] = "consoleprintlevel";
void GridSimulation::set(std::string_view param, std::string_view val)
{
    if (isSetHandled(GridSimulation::getModelDescription().trySetLocal(*this, param, val))) {
        return;
    }
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
    } else {
        GridArea::set(param, val);
    }
}

std::string GridSimulation::getString(std::string_view param) const
{
    if (const auto value = GridSimulation::getModelDescription().tryGetStringLocal(*this, param)) {
        return *value;
    }
    if (param == "logfile") {
        return logFile;
    }

    if (param == "version") {
        return version;
    }
    return GridArea::getString(param);
}

void GridSimulation::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(GridSimulation::getModelDescription().trySet(*this, param, val, unitType))) {
        return;
    }
    if (param == "printlevel") {
        auto testLevel = static_cast<PrintLevel>(static_cast<int>(val));
        if ((testLevel > PrintLevel::TRACE) || (testLevel < PrintLevel::NO_PRINT)) {
            throw(InvalidParameterValue(param));
        }
        consolePrintLevel = testLevel;
        logPrintLevel = testLevel;
    } else if (param == consoleprint) {
        auto testLevel = static_cast<PrintLevel>(static_cast<int>(val));
        if ((testLevel > PrintLevel::TRACE) || (testLevel < PrintLevel::NO_PRINT)) {
            throw(InvalidParameterValue(param));
        }
        consolePrintLevel = testLevel;
        gridLog->changeLevels(static_cast<int>(consolePrintLevel), static_cast<int>(logPrintLevel));
    } else if (param == "logprintlevel") {
        auto testLevel = static_cast<PrintLevel>(static_cast<int>(val));
        if ((testLevel > PrintLevel::TRACE) || (testLevel < PrintLevel::NO_PRINT)) {
            throw(InvalidParameterValue(param));
        }
        logPrintLevel = testLevel;
        gridLog->changeLevels(static_cast<int>(consolePrintLevel), static_cast<int>(logPrintLevel));
    } else if (param == "version") {
        version = std::to_string(val);
    } else {
        GridArea::set(param, val, unitType);
    }
}

// find Collector
std::shared_ptr<Collector> GridSimulation::findCollector(const std::string& collectorName)
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

void GridSimulation::log(CoreObject* object, PrintLevel level, const std::string& message)
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
    const std::string cname = '[' +
        (isSameObject(object, this) ?
             "sim" :
             (fullObjectName(object) + '(' + std::to_string(object->getUserID()) + ')')) +
        ']';
    const std::string simtime = ((currentTime > negTime) ? '(' + std::to_string(currentTime) + ')' :
                                                           std::string("(PRESTART)"));
    std::string key;
    if (level == PrintLevel::WARNING) {
        key = "||WARNING||";
        boundedIncrement(warnCount);
    } else if (level == PrintLevel::ERROR) {
        key = "||ERROR||";
        boundedIncrement(errorCount);
    }
    // drop the preliminary information in a certain circumstance
    if (level == PrintLevel::SUMMARY) {
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

bool GridSimulation::shouldLog(PrintLevel level) const
{
    return (level <= consolePrintLevel) || (level <= logPrintLevel);
}

static const std::map<int, std::string> ALERT_STRINGS{
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

void GridSimulation::alert(CoreObject* object, int code)
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
                GridArea::alert(object, code);
                break;
            default:
                break;
        }
    } else if (code < MIN_CHANGE_ALERT) {
        boundedIncrement(alertCount);
        std::string astr;
        auto res = ALERT_STRINGS.find(code);
        if (res != ALERT_STRINGS.end()) {
            astr = res->second;
            log(object, PrintLevel::SUMMARY, astr);
        } else {
            const std::string message = "Unrecognized alert code (" + std::to_string(code) + ')';
            log(object, PrintLevel::SUMMARY, message);
        }
    }
}

double GridSimulation::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = GridSimulation::getModelDescription().tryGet(*this, param, unitType)) {
        return *value;
    }
    count_t ival = kInvalidCount;
    double fval = kNullVal;
    if ((param == "collectorcount") || (param == "recordercount")) {
        ival = static_cast<count_t>(collectorList.size());
    } else if (param == "alertcount") {
        ival = alertCount;
    } else if (param == "eventcount") {
        const auto eventCount = EvQ->size();
        ival = (eventCount > 0) ? (eventCount - 1) : 0;
    } else if (param == "warncount") {
        ival = warnCount;
    } else if (param == "errorcount") {
        ival = errorCount;
    } else if (param == "logprintlevel") {
        ival = static_cast<int>(logPrintLevel);
    } else if ((param == "consoleprintlevel") || (param == "printlevel")) {
        ival = static_cast<int>(consolePrintLevel);
    } else if (param == "stepsize") {
        fval = units::convert(stepTime, units::second, unitType);
    } else if ((param == "currenttime") || (param == "time")) {
        fval = units::convert(getSimulationTime(), units::second, unitType);
    } else if (param == "eventtime") {
        fval = units::convert(getEventTime(), units::second, unitType);
    } else if (param == "state") {
        fval = static_cast<double>(pState);
    } else {
        fval = GridArea::get(param, unitType);
    }
    return (ival != kInvalidCount) ? static_cast<double>(ival) : fval;
}

void GridSimulation::setLogger(std::function<void(int, const std::string&)> loggingFunction)
{
    customLogger = std::move(loggingFunction);
}

// TODO(PT):: this really shouldn't be a function,  but still debating alternative approaches to the
// need it addressed
void GridSimulation::resetObjectCounters()
{
    ZipLoad::loadCount = 0;
    // GridArea::areaCount = 0;
    GridBus::busCount = 0;
    Link::linkCount = 0;
    Relay::relayCount = 0;
    Generator::genCount = 0;
}

CoreTime GridSimulation::getEventTime() const
{
    return EvQ->getNextTime();
}

CoreTime GridSimulation::getEventTime(int eventCode) const
{
    return EvQ->getNextTime(eventCode);
}

// NOLINTNEXTLINE(misc-no-recursion)
CoreObject* findMatchingObject(CoreObject* obj1, GridPrimary* src, GridPrimary* sec)
{
    if (obj1 == nullptr) {
        return nullptr;
    }
    if (isSameObject(obj1, src)) {
        return sec;
    }
    CoreObject* obj2 = nullptr;
    if (dynamic_cast<GridSecondary*>(obj1) !=
        nullptr)  // we know it is a gen or load so it parent should be a bus
    {
        auto* bus = dynamic_cast<GridBus*>(obj1->getParent());
        auto* bus2 = getMatchingBus(bus, src, sec);
        if (bus2 != nullptr) {
            if (dynamic_cast<Generator*>(obj1) != nullptr) {
                obj2 = bus2->getGen(obj1->locIndex);
            } else if (dynamic_cast<ZipLoad*>(obj1) != nullptr) {
                obj2 = bus2->getLoad(obj1->locIndex);
            }
        }
    } else if (dynamic_cast<GridBus*>(obj1) != nullptr) {
        obj2 = getMatchingBus(dynamic_cast<GridBus*>(obj1), src, sec);
    } else if (dynamic_cast<GridArea*>(obj1) != nullptr) {
        obj2 = getMatchingGridArea(dynamic_cast<GridArea*>(obj1), src, sec);
    } else if (dynamic_cast<Link*>(obj1) != nullptr) {
        obj2 = getMatchingLink(dynamic_cast<Link*>(obj1), src, sec);
    } else {
        // now we get ugly we are GridSecondary object
        const CoreObject* pobj = findMatchingObject(obj1->getParent(), src, sec);
        if (pobj != nullptr) {  // this is an internal string sequence for this purpose, likely
                                // won't be documented
            obj2 = pobj->getSubObject("submodelcode", obj1->locIndex);
        }
    }
    return obj2;
}
}  // namespace griddyn
