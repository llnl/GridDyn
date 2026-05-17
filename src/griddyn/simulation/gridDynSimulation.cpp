/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gridDynSimulation.h"

#include "../Link.h"
#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../events/parameterOperator.h"
#include "../gridBus.h"
#include "../loads/gridLabDLoad.h"
#include "../solvers/solverInterface.h"
#include "contingency.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gridDynSimulationFileOps.h"
#include "utilities/matrixData.hpp"
#include <cassert>
#include <compare>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace griddyn {
static typeFactory<gridDynSimulation>
    simulationFactory(  // NOLINT(bugprone-throwing-static-initialization)
        "simulation",
        stringVec{"GridDyn", "gridlab", "gridlabd"},
        "GridDyn");

std::atomic<gridDynSimulation*> gridDynSimulation::s_instance{nullptr};

// local search functions for MPI based objects
static count_t searchForGridlabDobject(const CoreObject* obj);

gridDynSimulation::gridDynSimulation(const std::string& objName):
    gridSimulation(objName), controlFlags(0LL)
{
// defaults
#ifndef GRIDDYN_ENABLE_KLU
    controlFlags.set(dense_solver);
#endif
}

gridDynSimulation::~gridDynSimulation()
{
    // if we are destroying the primary instance then we need to remove it from access
    auto* instancePtr = this;
    s_instance.compare_exchange_strong(instancePtr, static_cast<gridDynSimulation*>(nullptr));
}

void gridDynSimulation::setInstance(gridDynSimulation* gds)
{
    s_instance = gds;
}

gridDynSimulation* gridDynSimulation::getInstance()
{
    return s_instance;
}

CoreObject* gridDynSimulation::clone(CoreObject* obj) const
{
    auto* sim = cloneBase<gridDynSimulation, gridSimulation>(this, obj);
    if (sim == nullptr) {
        return obj;
    }
    sim->controlFlags = controlFlags;
    sim->max_Vadjust_iterations = max_Vadjust_iterations;
    sim->max_Padjust_iterations = max_Padjust_iterations;

    sim->probeStepTime = probeStepTime;
    sim->powerAdjustThreshold = powerAdjustThreshold;
    sim->powerFlowStartTime = powerFlowStartTime;
    sim->tols = tols;

    sim->actionQueue = actionQueue;
    sim->default_ordering = default_ordering;
    sim->powerFlowFile = powerFlowFile;
    sim->defaultDynamicSolverMethod = defaultDynamicSolverMethod;
    // std::vector < std::shared_ptr < SolverInterface >> solverInterfaces;
    // std::vector<GridComponent *>singleStepObjects;
    // now clone the solverInterfaces
    auto solverInterfaceCount = static_cast<count_t>(solverInterfaces.size());
    sim->solverInterfaces.resize(solverInterfaceCount);
    sim->extraStateInformation.resize(solverInterfaceCount, nullptr);
    sim->extraDerivInformation.resize(solverInterfaceCount, nullptr);
    // the first two are local and don't have solvers associated with them
    for (index_t kk = 1; kk < solverInterfaceCount; ++kk) {
        if (solverInterfaces[kk]) {
            if (sim->solverInterfaces[kk]) {
                solverInterfaces[kk]->cloneTo(sim->solverInterfaces[kk].get(), true);
            } else {
                sim->solverInterfaces[kk] = solverInterfaces[kk]->clone(true);
            }
            sim->solverInterfaces[kk]->setSimulationData(sim);
        }
    }
    // copy the default solver modes
    if ((defPowerFlowMode->offsetIndex < solverInterfaceCount) &&
        (solverInterfaces[defPowerFlowMode->offsetIndex])) {
        sim->defPowerFlowMode =
            &(sim->solverInterfaces[defPowerFlowMode->offsetIndex]->getSolverMode());
    }
    if ((defDAEMode->offsetIndex < solverInterfaceCount) &&
        (solverInterfaces[defDAEMode->offsetIndex])) {
        sim->defDAEMode = &(sim->solverInterfaces[defDAEMode->offsetIndex]->getSolverMode());
    }
    if ((defDynAlgMode->offsetIndex < solverInterfaceCount) &&
        (solverInterfaces[defDynAlgMode->offsetIndex])) {
        sim->defDynAlgMode = &(sim->solverInterfaces[defDynAlgMode->offsetIndex]->getSolverMode());
    }
    if ((defDynDiffMode->offsetIndex < solverInterfaceCount) &&
        (solverInterfaces[defDynDiffMode->offsetIndex])) {
        sim->defDynDiffMode =
            &(sim->solverInterfaces[defDynDiffMode->offsetIndex]->getSolverMode());
    }
    return sim;
}
int gridDynSimulation::tripSlippedLines()
{
    std::vector<Link*> lineTest;
    lineTest.reserve(linkCount);
    getLinkVector(lineTest);
    int disconnectCount{0};
    for (auto& line : lineTest) {
        if (line->testAndTrip(2)) {
            ++disconnectCount;
        }
    }
    return disconnectCount;
}

int gridDynSimulation::rebalanceLoadGen()
{
    std::vector<gridBus*> bnetwork;
    bnetwork.reserve(busCount);
    getBusVector(bnetwork);
    double totalLoad{0.0};
    double totalGen{0.0};
    for (auto& bus : bnetwork) {
        if (bus->isConnected() && bus->isEnabled()) {
            if (bus->Network == 1) {
                totalLoad += bus->getLoadReal();
                totalGen += bus->getGenerationReal();
            }
        } else {
            bus->Network = 8;
        }
    }
    totalLoad *= 1.04;
    if (std::abs(totalLoad + totalGen) > 0.04) {
        if (generatorAdjust(totalLoad + totalGen)) {
            return 0;
        }
    }

    return -1;
}

bool gridDynSimulation::doAutomaticLoadLoss()
{
    std::vector<gridBus*> bnetwork;
    bnetwork.reserve(busCount);
    getBusVector(bnetwork);
    bool modified{false};
    for (auto& bus : bnetwork) {
        if (bus->getLoadReal() != 0.0) {
            if (bus->getVoltage() < 0.95) {
                int loadIndex{0};
                do {
                    auto* load = bus->getLoad(loadIndex);
                    if (load != nullptr) {
                        if (load->isConnected()) {
                            modified = true;
                            load->disconnect();
                            break;
                        }
                        ++loadIndex;
                    } else {
                        break;
                    }
                } while (true);
            }
        }
    }

    if (!modified) {
        const gridBus* maxDiffBus{nullptr};
        double maxDiff{0.0};
        for (auto& bus : bnetwork) {
            auto ldp = bus->getLoadReal();
            if (ldp == 0.0) {
                continue;
            }
            auto genp = bus->getGenerationReal();
            auto lnkp = bus->getLinkReal();
            auto sum = lnkp + ldp + genp;
            if (sum > maxDiff) {
                maxDiffBus = bus;
                maxDiff = sum;
            }
        }
        if ((maxDiff > 0.01) && (maxDiffBus != nullptr)) {
            int loadIndex{0};
            do {
                auto* load = maxDiffBus->getLoad(loadIndex);
                if (load != nullptr) {
                    if (load->isConnected()) {
                        modified = true;
                        load->disconnect();
                        break;
                    }
                    ++loadIndex;
                } else {
                    break;
                }
            } while (true);
        }
    }
    return modified;
}

int gridDynSimulation::checkNetwork(NetworkCheckType checkType)
{
    // make a full list of all buses
    std::vector<gridBus*> bnetwork;
    bnetwork.reserve(busCount);
    getBusVector(bnetwork);
    if (checkType == NetworkCheckType::FULL) {
        slkBusses.clear();

        for (auto& bus : bnetwork) {
            if (bus->isEnabled() && bus->isConnected()) {
                // check to make sure the bus can actually work
                if (bus->checkCapable()) {
                    bus->Network = 0;
                    const auto busType = bus->getType();
                    if ((busType == gridBus::busType::SLK) || (busType == gridBus::busType::afix)) {
                        slkBusses.push_back(bus);
                    }
                } else {
                    bus->disable();
                    bus->Network = -1;
                }
            } else {
                bus->Network = -1;
            }
        }
    } else {
        for (auto& bus : bnetwork) {
            bus->Network = (std::min)(bus->Network, 0);
        }
    }
    int32_t networkNum = 0;
    std::queue<gridBus*> bstk;
    for (auto& networkBus : bnetwork) {
        if ((networkBus->Network == 0) && (networkBus->isConnected())) {
            networkNum++;
            networkBus->followNetwork(networkNum, bstk);
            while (!bstk.empty()) {
                if (bstk.front()->Network != networkNum) {
                    bstk.front()->followNetwork(networkNum, bstk);
                }
                bstk.pop();
            }
        }
    }
    // check to make sure we have a swing bus for each network
    networkCount = networkNum;
    for (int32_t nn = 1; nn <= networkNum; nn++) {
        bool slackFound = false;
        bool pvFound = false;
        bool afixFound = false;
        for (auto& slackBus : slkBusses) {
            if (slackBus->Network == nn) {
                if (slackBus->getType() == gridBus::busType::SLK) {
                    slackFound = true;
                    break;
                }
                afixFound = true;
            }
        }
        if (!slackFound) {
            for (auto& networkBus : bnetwork) {
                if (networkBus->Network == nn) {
                    if (networkBus->getType() == gridBus::busType::SLK) {
                        slackFound = true;
                        break;
                    }
                    if (networkBus->getType() == gridBus::busType::PV) {
                        pvFound = true;
                    } else if (networkBus->getType() == gridBus::busType::afix) {
                        afixFound = true;
                    }
                    if (afixFound && pvFound) {
                        slackFound =
                            true;  // a pv bus and a separate afix bus can function as a slk bus
                        break;
                    }
                }
            }
        }
        // now we go into a loop to make a new slack bus
        if (!slackFound) {
            if (controlFlags[disable_auto_slack_bus]) {
                if (controlFlags[disable_auto_disconnect]) {
                    logging::error(this, "no SLK bus found in network {}", nn);
                    for (auto& networkBus : bnetwork) {
                        if (networkBus->Network == nn) {
                            logging::debug(this,
                                           "Network {} bus {}:{}",
                                           nn,
                                           networkBus->getUserID(),
                                           networkBus->getName());
                        }
                    }
                    return NO_SLACK_BUS_FOUND;
                }

                logging::warning(this, "no SLK bus found in network {} disconnecting buses", nn);
                for (auto& networkBus : bnetwork) {
                    if (networkBus->Network == nn) {
                        networkBus->disconnect();
                    }
                }
            } else {
                gridBus* maxCapBus = nullptr;
                double maxcap = 0.0;
                for (auto& networkBus : bnetwork) {
                    if (networkBus->Network == nn) {
                        if (networkBus->getType() == gridBus::busType::PV) {
                            const double cap = networkBus->getAdjustableCapacityUp();
                            if (cap > maxcap) {
                                maxcap = cap;
                                maxCapBus = networkBus;
                            }
                        }
                    }
                }
                if (maxCapBus != nullptr) {
                    maxCapBus->set("type", "slk");
                } else {
                    if (controlFlags[disable_auto_disconnect]) {
                        logging::error(this, "no SLK bus or PV bus found in network {}", nn);
                        for (auto& networkBus : bnetwork) {
                            if (networkBus->Network == nn) {
                                logging::debug(this,
                                               "Network {} bus {}:{}",
                                               nn,
                                               networkBus->getUserID(),
                                               networkBus->getName());
                            }
                        }
                        return NO_SLACK_BUS_FOUND;
                    }

                    logging::warning(this,
                                     "no SLK or PV bus found in network {} disconnecting buses",
                                     nn);
                    for (auto& networkBus : bnetwork) {
                        if (networkBus->Network == nn) {
                            logging::normal(this,
                                            "Network {} disconnect bus {}:{}",
                                            nn,
                                            networkBus->getUserID(),
                                            networkBus->getName());
                            networkBus->disconnect();
                        }
                    }
                }
            }
        }
    }

    return FUNCTION_EXECUTION_SUCCESS;
}

double gridDynSimulation::getState(index_t offset) const
{
    return getState(offset, cLocalSolverMode);
}

double gridDynSimulation::getState(index_t offset, const solverMode& sMode) const
{
    solverMode nMode = sMode;
    double ret = kNullVal;
    if (isLocal(sMode)) {
        switch (pState) {
            case gridState_t::POWERFLOW_COMPLETE:
            case gridState_t::INITIALIZED:
                nMode = *defPowerFlowMode;
                break;
            case gridState_t::DYNAMIC_INITIALIZED:
            case gridState_t::DYNAMIC_COMPLETE:
            case gridState_t::DYNAMIC_PARTIAL:
                if (defaultDynamicSolverMethod == dynamic_solver_methods::dae) {
                    nMode = *defDAEMode;
                } else {
                    nMode = *defDynAlgMode;
                }
                break;
            default:
                nMode =
                    cEmptySolverMode;  // this should actually do nothing since the size should be 0
        }
    }

    auto solData = getSolverInterface(nMode);
    if (solData) {
        const double* state = solData->state_data();
        if (solData->size() > offset) {
            ret = state[offset];
        }
    }

    return ret;
}

std::vector<double> gridDynSimulation::getState(const solverMode& sMode) const
{
    solverMode nMode = sMode;
    if (isLocal(sMode)) {
        switch (pState) {
            case gridState_t::POWERFLOW_COMPLETE:
            case gridState_t::INITIALIZED:
                nMode = *defPowerFlowMode;
                break;
            case gridState_t::DYNAMIC_INITIALIZED:
            case gridState_t::DYNAMIC_COMPLETE:
            case gridState_t::DYNAMIC_PARTIAL:
                if (defaultDynamicSolverMethod == dynamic_solver_methods::dae) {
                    nMode = *defDAEMode;
                } else {
                    nMode = *defDynAlgMode;
                }
                break;
            default:
                nMode =
                    cEmptySolverMode;  // this should actually do nothing since the size should be 0
        }
    }

    std::vector<double> states;
    auto solData = getSolverInterface(nMode);
    if (solData) {
        const double* state = solData->state_data();
        if (solData->size() != 0) {
            states.assign(state, state + solData->size());
        }
    }

    return states;
}

/*
mixed = 0,  //!< everything is mixed through each other
grouped = 1,  //!< all similar variables are grouped together (angles, then voltage, then algebraic,
then differential) algebraic_grouped = 2, //!< all the algebraic variables are grouped, then the
differential voltage_first = 3, //!< grouped with the voltage coming first angle_first = 4,  //!<
grouped with the angle coming first differential_first = 5, //!< differential and algebraic grouped
with differential first
*/

void gridDynSimulation::setupOffsets(const solverMode& sMode, offset_ordering offsetOrdering)
{
    solverOffsets offsetValues;
    switch (offsetOrdering) {
        case offset_ordering::mixed:
        default:
            offsetValues.algOffset = 0;
            break;
        case offset_ordering::grouped:
        case offset_ordering::voltage_first:
            offsetValues.vOffset = 0;
            offsetValues.aOffset = voltageStateCount(sMode);
            offsetValues.algOffset = offsetValues.aOffset + angleStateCount(sMode);
            if (hasDifferential(sMode)) {
                offsetValues.diffOffset = totalAlgSize(sMode);
            }
            break;
        case offset_ordering::algebraic_grouped:
            offsetValues.algOffset = 0;
            if (hasDifferential(sMode)) {
                offsetValues.diffOffset = totalAlgSize(sMode);
            }
            break;
        case offset_ordering::angle_first:
            offsetValues.aOffset = 0;
            offsetValues.vOffset = angleStateCount(sMode);
            offsetValues.algOffset = offsetValues.aOffset + voltageStateCount(sMode);
            if (hasDifferential(sMode)) {
                offsetValues.diffOffset = totalAlgSize(sMode);
            }
            break;
        case offset_ordering::differential_first:
            offsetValues.diffOffset = 0;
            offsetValues.algOffset = diffSize(sMode);
            break;
    }

    // call the area setOffset function to distribute the offsets
    setOffsets(offsetValues, sMode);
}

// --------------- run the simulation ---------------

int gridDynSimulation::run(coreTime t_end)
{
    if (t_end == negTime) {
        t_end = stopTime;
    }
    gridDynAction gda;
    gda.command = gridDynAction::gd_action_t::run;
    gda.val_double = t_end;
    return execute(gda);
}

void gridDynSimulation::add(std::string_view actionString)
{
    actionQueue.emplace(std::string{actionString});
}

void gridDynSimulation::add(gridDynAction& newAction)
{
    actionQueue.push(newAction);
}

int gridDynSimulation::execute(std::string_view cmd)
{
    const gridDynAction gda(std::string{cmd});
    return execute(gda);
}

// NOLINTNEXTLINE(misc-no-recursion)
int gridDynSimulation::execute(const gridDynAction& cmd)
{
    int out = FUNCTION_EXECUTION_SUCCESS;
    switch (cmd.command) {
        case gridDynAction::gd_action_t::ignore:
        default:
            break;
        case gridDynAction::gd_action_t::set: {
            objInfo objectInfo(cmd.string1, this);
            if (cmd.val_double == kNullVal) {
                objectInfo.m_obj->set(objectInfo.m_field, cmd.string2);
            } else {
                objectInfo.m_obj->set(objectInfo.m_field, cmd.val_double, objectInfo.m_unitType);
            }
        } break;
        case gridDynAction::gd_action_t::setall:
            setAll(cmd.string1, cmd.string2, cmd.val_double);
            break;
        case gridDynAction::gd_action_t::setsolver:
            solverSet(cmd.string1, cmd.string2, cmd.val_double);
            break;
        case gridDynAction::gd_action_t::print:
            break;
        case gridDynAction::gd_action_t::powerflow:
            out = powerflow();
            break;
        case gridDynAction::gd_action_t::check:
            if (cmd.string1 == "powerflow") {
                std::vector<Violation> violations;
                pFlowCheck(violations);
                if (cmd.string2.empty()) {
                    for (auto& violation : violations) {
                        std::cout << violation.to_string() << '\n';
                    }
                } else {
                    std::ofstream outF(cmd.string2, std::ios_base::app);
                    if (outF.is_open()) {
                        outF << "Violations for " << getName() << "at "
                             << std::to_string(static_cast<double>(getSimulationTime())) << '\n';
                        for (auto& violation : violations) {
                            outF << violation.to_string() << '\n';
                        }
                        out = FUNCTION_EXECUTION_SUCCESS;
                    } else {
                        out = -3;
                    }
                }
            }
            break;
        case gridDynAction::gd_action_t::contingency: {
            auto info = emptyExtraInfo;

            if (cmd.flag == 1) {
                info.simplified = true;
            }
            auto contingencies = buildContingencyList(this, cmd.string1, info);
            if (!contingencies.empty()) {
                runContingencyAnalysis(contingencies, cmd.string2, cmd.val_int1, cmd.val_int2);
                out = FUNCTION_EXECUTION_SUCCESS;
            } else {
                out = FUNCTION_EXECUTION_FAILURE;
            }
        }

        break;
        case gridDynAction::gd_action_t::continuation:
            out = FUNCTION_EXECUTION_FAILURE;
            break;
        case gridDynAction::gd_action_t::initialize: {
            const coreTime startTimeValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : startTime;

            if (pState == gridState_t::STARTUP) {
                out = pFlowInitialize(startTimeValue);
                if (out == 1) {
                    logging::error(this, "unable to initialize powerflow");
                    out = FUNCTION_EXECUTION_FAILURE;
                }
            } else if (pState == gridState_t::POWERFLOW_COMPLETE) {
                out = dynInitialize(startTimeValue);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "unable to complete dynamic power initialization");
                    return FUNCTION_EXECUTION_FAILURE;
                }
            }
        } break;
        case gridDynAction::gd_action_t::iterate: {
            const coreTime timeStepValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stepTime;
            const coreTime endTimeValue =
                (cmd.val_double2 != kNullVal) ? coreTime(cmd.val_double2) : stopTime;
            out = eventDrivenPowerflow(endTimeValue, timeStepValue);
        } break;
        case gridDynAction::gd_action_t::eventmode: {
            const coreTime endTimeValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stopTime;
            if (cmd.val_double2 != kNullVal) {
                const coreTime timeStepValue = cmd.val_double2;
                out = eventDrivenPowerflow(endTimeValue, timeStepValue);
            } else {
                out = eventDrivenPowerflow(endTimeValue);
            }
        }

        break;
        case gridDynAction::gd_action_t::dynamicDAE: {
            const coreTime endTimeValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stopTime;
            if (pState < gridState_t::DYNAMIC_INITIALIZED) {
                out = dynInitialize();
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                logging::warning(this, "No Differential states halting computation");
                return out;
            }
            out = dynamicDAE(endTimeValue);
        } break;
        case gridDynAction::gd_action_t::dynamicPart: {
            const double endTimeValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stopTime;
            const double timeStepValue =
                (cmd.val_double2 != kNullVal) ? coreTime(cmd.val_double2) : stepTime;
            if (pState < gridState_t::DYNAMIC_INITIALIZED) {
                out = dynInitialize();
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                logging::warning(this, "No Differential states halting computation");
                return out;
            }
            out = dynamicPartitioned(endTimeValue, timeStepValue);
        } break;
        case gridDynAction::gd_action_t::dynamicDecoupled: {
            const double endTimeValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stopTime;
            const double timeStepValue =
                (cmd.val_double2 != kNullVal) ? coreTime(cmd.val_double2) : stepTime;
            if (pState < gridState_t::DYNAMIC_INITIALIZED) {
                out = dynInitialize();
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            out = dynamicDecoupled(endTimeValue, timeStepValue);
        } break;
        case gridDynAction::gd_action_t::step: {
            const coreTime timeStepValue =
                (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stepTime;
            coreTime timeActual =
                (cmd.val_double2 != kNullVal) ? coreTime(cmd.val_double2) : stopTime;
            if (pState < gridState_t::DYNAMIC_INITIALIZED) {
                out = dynInitialize();
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                logging::warning(this, "No Differential states halting computation");
                return out;
            }
            out = step(timeStepValue, timeActual);
        } break;
        case gridDynAction::gd_action_t::run:
            if (actionQueue.empty()) {
                const coreTime endTimeValue =
                    (cmd.val_double != kNullVal) ? coreTime(cmd.val_double) : stopTime;
                if (controlFlags[power_flow_only]) {
                    out = powerflow();
                } else {
                    if (pState < gridState_t::DYNAMIC_INITIALIZED) {
                        out = dynInitialize(currentTime);
                        if (out != FUNCTION_EXECUTION_SUCCESS) {
                            return out;
                        }
                    }

                    if (hasDynamics()) {
                        switch (defaultDynamicSolverMethod) {
                            case dynamic_solver_methods::dae:
                            default:
                                out = dynamicDAE(endTimeValue);
                                break;
                            case dynamic_solver_methods::partitioned:
                                out = dynamicPartitioned(endTimeValue, stepTime);
                                break;
                            case dynamic_solver_methods::decoupled:
                                out = dynamicDecoupled(endTimeValue, stepTime);
                                break;
                        }
                    } else {
                        logging::summary(this,
                                         "No Differential states reverting to stepped power flow");
                        out = eventDrivenPowerflow(endTimeValue, stepTime);
                    }

                    if (getSimulationTime() >= stopTime) {
                        saveRecorders();
                    }
                }
            } else {
                while (!actionQueue.empty()) {
                    out = execute(actionQueue.front());
                    actionQueue.pop();
                    if (out != FUNCTION_EXECUTION_SUCCESS) {
                        return out;
                    }
                }
            }
            break;
        case gridDynAction::gd_action_t::reset:
            if (cmd.val_int1 >= 0) {
                reset(static_cast<reset_levels>(cmd.val_int1));
            } else {
                reset(reset_levels::minimal);
            }
            break;
        case gridDynAction::gd_action_t::save:
            if (cmd.string1 == "recorder") {
                saveRecorders();
            } else if (cmd.string1 == "state") {
                saveState(this, (cmd.string2.empty()) ? stateFile : cmd.string2);
            } else if (cmd.string1 == "Jacobian") {
                saveJacobian(this,
                             (cmd.string2.empty()) ? "jacobian_" + getName() + ".bin" :
                                                     cmd.string2);
            } else if (cmd.string1 == "powerflow") {
                savePowerFlow(this, (cmd.string2.empty()) ? powerFlowFile : cmd.string2);
            } else if ((cmd.string1 == "voltage") || (cmd.string1 == "jacstate")) {
            }
            break;
        case gridDynAction::gd_action_t::load:
            if ((cmd.string1 == "state") || (cmd.string1 == "powerflow")) {
            }
            break;
        case gridDynAction::gd_action_t::add:
        case gridDynAction::gd_action_t::rollback:
        case gridDynAction::gd_action_t::checkpoint:
            break;
    }
    return out;
}

bool gridDynSimulation::hasDynamics() const
{
    return diffSize(*defDAEMode) > 0;
}

// need to update probably with a new field in SolverInterface
count_t gridDynSimulation::nonZeros(const solverMode& sMode) const
{
    auto solverInterface = getSolverInterface(sMode);
    return solverInterface ? solverInterface->nonZeros() : 0;
}

// --------------- set properties ---------------
void gridDynSimulation::set(std::string_view param, std::string_view val)
{
    if (param == "powerflowfile") {
        powerFlowFile = val;
        controlFlags.set(save_power_flow_data);
    } else if (param == "powerflowinputfile") {
        powerFlowInputFile = val;
        controlFlags.set(save_power_flow_input_data);
    } else if (param == "defpowerflow") {
        setDefaultMode(solution_modes_t::powerflow_mode, getSolverMode(std::string{val}));
    } else if (param == "defdae") {
        setDefaultMode(solution_modes_t::dae_mode, getSolverMode(std::string{val}));
    } else if (param == "defdynalg") {
        setDefaultMode(solution_modes_t::algebraic_mode, getSolverMode(std::string{val}));
    } else if (param == "defdyndiff") {
        setDefaultMode(solution_modes_t::differential_mode, getSolverMode(std::string{val}));
    } else if (param == "action") {
        auto actions = gmlc::utilities::stringOps::splitline(val, ';');
        gmlc::utilities::stringOps::trim(actions);
        for (auto& actionString : actions) {
            add(actionString);
        }
    } else if (param == "ordering") {
        auto order = gmlc::utilities::convertToLowerCase(val);
        if (order == "mixed") {
            default_ordering = offset_ordering::mixed;
        } else if (order == "grouped") {
            default_ordering = offset_ordering::grouped;
        } else if (order == "algebraic_grouped") {
            default_ordering = offset_ordering::algebraic_grouped;
        } else if (order == "voltage_first") {
            default_ordering = offset_ordering::voltage_first;
        } else if (order == "angle_first") {
            default_ordering = offset_ordering::angle_first;
        } else if (order == "differential_first") {
            default_ordering = offset_ordering::differential_first;
        } else {
            throw(invalidParameterValue(val));
        }
    } else if (param == "dynamicsolvermethod") {
        auto method = gmlc::utilities::convertToLowerCase(val);
        if (method == "dae") {
            defaultDynamicSolverMethod = dynamic_solver_methods::dae;
        } else if (method == "partitioned") {
            defaultDynamicSolverMethod = dynamic_solver_methods::partitioned;
        } else if (method == "decoupled") {
            defaultDynamicSolverMethod = dynamic_solver_methods::decoupled;
        } else {
            throw(invalidParameterValue(val));
        }
    } else {
        gridSimulation::set(param, val);
    }
}

std::string gridDynSimulation::getString(std::string_view param) const
{
    if (param == "powerflowfile") {
        return powerFlowFile;
    }
    if (param == "powerflowinputfile") {
        return powerFlowInputFile;
    }
    return gridSimulation::getString(param);
}

void gridDynSimulation::setDefaultMode(solution_modes_t mode, const solverMode& sMode)
{
    auto solverData = getSolverInterface(sMode);
    if (!solverData) {
        solverData = makeSolver(this, sMode);
        add(solverData);
    }
    switch (mode) {
        case solution_modes_t::powerflow_mode:
            if (isAlgebraicOnly(sMode)) {
                if (opFlags[pFlow_initialized]) {
                    if (solverData->isInitialized()) {
                        reInitpFlow(solverData->getSolverMode());
                    }
                }
                solverData->set("mode", "powerflow");
                defPowerFlowMode = &(solverData->getSolverMode());
            } else {
                throw(invalidParameterValue("mode"));
            }
            break;
        case solution_modes_t::dae_mode:
            if (isDAE(sMode)) {
                if (opFlags[dyn_initialized]) {
                    if (solverData->isInitialized()) {
                        reInitDyn(solverData->getSolverMode());
                    }
                }

                defDAEMode = &(solverData->getSolverMode());
            } else {
                throw(invalidParameterValue("mode"));
            }
            break;
        case solution_modes_t::algebraic_mode:
            if (isAlgebraicOnly(sMode)) {
                if (opFlags[dyn_initialized]) {
                    if (solverData->isInitialized()) {
                        reInitDyn(solverData->getSolverMode());
                    }
                }
                solverData->setFlag("dynamic", true);
                defDynAlgMode = &(solverData->getSolverMode());
            } else {
                throw(invalidParameterValue("mode"));
            }
            break;
        case solution_modes_t::differential_mode:
            if (isDifferentialOnly(sMode)) {
                if (opFlags[dyn_initialized]) {
                    if (solverData->isInitialized()) {
                        reInitDyn(solverData->getSolverMode());
                    }
                }
                defDynDiffMode = &(solverData->getSolverMode());
            } else {
                throw(invalidParameterValue("mode"));
            }
            break;
        default:
            throw(unrecognizedParameter("mode"));
            break;
    }
}

static const std::unordered_map<std::string, int>& getFlagControlMap()
{
    static const std::unordered_map<std::string, int> flagControlMap{
        {"autoallocate", power_adjust_enabled},
        {"power_adjust", power_adjust_enabled},
        {"sparse", -dense_solver},
        {"dense", dense_solver},
        {"no_auto_autogen", no_auto_autogen},
        {"auto_bus_disconnect", auto_bus_disconnect},
        {"roots_disabled", roots_disabled},
        {"voltageconstraints", voltage_constraints_flag},
        {"record_on_halt", record_on_halt_flag},
        {"constraints_disabled", constraints_disabled},
        {"dc_mode", dc_mode},
        {"dcFlow_initialization", dcFlow_initialization},
        {"no_link_adjustments", disable_link_adjustments},
        {"disable_link_adjustments", disable_link_adjustments},
        {"ignore_bus_limits", ignore_bus_limits},
        {"powerflow_only", power_flow_only},
        {"no_powerflow_adjustments", no_powerflow_adjustments},
        {"savepowerflow", save_power_flow_data},
        {"savepowerflowinput", save_power_flow_input_data},
        {"low_voltage_check", low_voltage_checking},
        {"no_powerflow_error_recovery", no_powerflow_error_recovery},
        {"dae_initialization_for_partitioned", dae_initialization_for_partitioned},
        {"force_powerflow", force_extra_powerflow},
        {"force_extra_powerflow", force_extra_powerflow},
        {"droop_power_flow", droop_power_flow},
    };
    return flagControlMap;
}

void gridDynSimulation::setFlag(std::string_view flag, bool val)
{
    const auto& flagControlMap = getFlagControlMap();
    auto flagIter = flagControlMap.find(std::string{flag});
    if (flagIter != flagControlMap.end()) {
        if (flagIter->second >= 0) {
            controlFlags.set(flagIter->second, val);
        } else {
            controlFlags.set(-flagIter->second, !val);
        }
    } else if (flag == "threads") {
        /*parallel_residual_enabled = 7,
                        parallel_jacobian_enabled = 8,
                        parallel_contingency_enabled = 9,*/
        controlFlags.set(parallel_residual_enabled, val);
        controlFlags.set(parallel_contingency_enabled, val);
        controlFlags.set(parallel_jacobian_enabled, val);
        // TODO(phlpt): Add some more option controls here.
    } else {
        gridSimulation::setFlag(flag, val);
    }
}

void gridDynSimulation::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "tolerance") || (param == "rtol")) {
        tols.rtol = val;
    } else if (param == "voltagetolerance") {
        tols.voltageTolerance = units::convert(val, unitType, units::puV, systemBasePower);
    } else if (param == "angletolerance") {
        tols.angleTolerance = units::convert(val, unitType, units::rad);
    } else if (param == "defaulttolerance") {
        tols.defaultTolerance = val;
    } else if (param == "tolerancerelaxation") {
        tols.toleranceRelaxationFactor = val;
    } else if (param == "powerflowstarttime") {
        powerFlowStartTime = units::convert(val, unitType, units::second);
    } else if (param == "timetolerance") {
        tols.timeTol = units::convert(val, unitType, units::second);
    } else if (param == "poweradjustthreshold") {
        powerAdjustThreshold = units::convert(val, unitType, units::puMW, systemBasePower);
    } else if (param == "maxpoweradjustiterations") {
        max_Padjust_iterations = static_cast<count_t>(val);
    } else if (param == "defpowerflow") {
        setDefaultMode(solution_modes_t::powerflow_mode, getSolverMode(static_cast<index_t>(val)));
    } else if (param == "defdae") {
        setDefaultMode(solution_modes_t::dae_mode, getSolverMode(static_cast<index_t>(val)));
    } else if (param == "defdynalg") {
        setDefaultMode(solution_modes_t::algebraic_mode, getSolverMode(static_cast<index_t>(val)));
    } else if (param == "defdyndiff") {
        setDefaultMode(solution_modes_t::differential_mode,
                       getSolverMode(static_cast<index_t>(val)));
    } else if (param == "maxvoltageadjustiterations") {
        max_Vadjust_iterations = static_cast<count_t>(val);
    } else {
        try {
            gridSimulation::set(param, val, unitType);
        }
        catch (const unrecognizedParameter&) {
            setFlag(param, (val > 0.1));
        }
    }
}

void gridDynSimulation::solverSet(std::string_view solverName, std::string_view field, double val)
{
    auto solverData = getSolverInterface(solverName);
    solverData->set(field, val);
}

void gridDynSimulation::solverSet(std::string_view solverName,
                                  std::string_view field,
                                  std::string_view val)
{
    auto solverData = getSolverInterface(solverName);
    solverData->set(field, val);
}

double gridDynSimulation::get(std::string_view param, units::unit unitType) const
{
    count_t val = kInvalidCount;
    double fval = kNullVal;

    if (param == "statesize") {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = stateSize(*defPowerFlowMode);
        } else {
            val = stateSize(*defDAEMode);
        }
    } else if (param == "vcount") {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = voltageStateCount(*defPowerFlowMode);
        } else {
            val = voltageStateCount(*defDAEMode);
        }
    } else if (param == "account") {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = angleStateCount(*defPowerFlowMode);
        } else {
            val = angleStateCount(*defDAEMode);
        }
    } else if (param == "algcount") {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = algSize(*defPowerFlowMode);
        } else {
            val = algSize(*defDAEMode);
        }
    } else if (param == "jacsize") {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = jacSize(*defPowerFlowMode);
        } else {
            val = jacSize(*defDAEMode);
        }
    } else if ((param == "diffcount") || (param == "diffsize")) {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = 0;
        } else {
            val = diffSize(*defDAEMode);
        }
    } else if (param == "nonzeros") {
        if (pState <= gridState_t::POWERFLOW_COMPLETE) {
            val = nonZeros(*defPowerFlowMode);
        } else {
            val = nonZeros(*defDAEMode);
        }
    } else if (param == "pflowstatesize") {
        val = stateSize(*defPowerFlowMode);
    } else if (param == "dynstatesize") {
        val = stateSize(*defDAEMode);
    } else if (param == "pflownonzeros") {
        val = nonZeros(*defPowerFlowMode);
    } else if (param == "dynnonzeros") {
        val = nonZeros(*defDAEMode);
    } else if (param == "residcount") {
        val = residCount;
    } else if (param == "haltcount") {
        val = haltCount;
    } else if (param == "islands" || param == "networkcount" || param == "networks") {
        val = networkCount;
    } else if (param == "iterationcount") {
        fval = getSolverInterface(*defPowerFlowMode)->get("iterationcount");
    } else if ((param == "jacobiancallcount") || (param == "jaccallcount")) {
        val = JacobianCallCount;
    } else if (param == "rootcount") {
        val = rootCount;
    } else if (param == "stepcount") {
        auto solverData = getSolverInterface(*defDAEMode);
        if (solverData && solverData->isInitialized()) {
            fval = solverData->get("resevals");
        } else {
            fval = 0;
        }
    } else if (param == "voltagetolerance") {
        fval = tols.voltageTolerance;
    } else if (param == "angletolerance") {
        fval = tols.angleTolerance;
    } else if (param == "timetolerance") {
        fval = tols.timeTol;
    } else if (param == "poweradjustthreshold") {
        fval = powerAdjustThreshold;
    } else {
        fval = gridSimulation::get(param, unitType);
    }
    return (val != kInvalidCount) ? static_cast<double>(val) : fval;
}

static const std::map<int, size_t>& getAlertFlags()
{
    static const std::map<int, size_t> alertFlags{
        std::make_pair(STATE_COUNT_CHANGE, state_change_flag),
        std::make_pair(STATE_COUNT_INCREASE, state_change_flag),
        std::make_pair(STATE_COUNT_DECREASE, state_change_flag),
        std::make_pair(ROOT_COUNT_CHANGE, root_change_flag),
        std::make_pair(ROOT_COUNT_INCREASE, root_change_flag),
        std::make_pair(ROOT_COUNT_DECREASE, root_change_flag),
        std::make_pair(JAC_COUNT_CHANGE, jacobian_count_change_flag),
        std::make_pair(JAC_COUNT_DECREASE, jacobian_count_change_flag),
        std::make_pair(JAC_COUNT_INCREASE, jacobian_count_change_flag),
        std::make_pair(OBJECT_COUNT_CHANGE, object_change_flag),
        std::make_pair(OBJECT_COUNT_INCREASE, object_change_flag),
        std::make_pair(OBJECT_COUNT_DECREASE, object_change_flag),
        std::make_pair(CONSTRAINT_COUNT_CHANGE, constraint_change_flag),
        std::make_pair(CONSTRAINT_COUNT_INCREASE, constraint_change_flag),
        std::make_pair(CONSTRAINT_COUNT_DECREASE, constraint_change_flag),
        std::make_pair(SLACK_BUS_CHANGE, slack_bus_change),
        std::make_pair(POTENTIAL_FAULT_CHANGE, reset_voltage_flag),
        std::make_pair(VERY_LOW_VOLTAGE_ALERT, low_bus_voltage),
        std::make_pair(INVALID_STATE_ALERT, invalid_state_flag),
        std::make_pair(CONNECTIVITY_CHANGE, connectivity_change_flag),

    };
    return alertFlags;
}

void gridDynSimulation::alert(CoreObject* object, int code)
{
    if ((code >= MIN_CHANGE_ALERT) && (code < MAX_CHANGE_ALERT)) {
        const auto& alertFlags = getAlertFlags();
        auto res = alertFlags.find(code);
        if (res != alertFlags.end()) {
            auto flagNum = res->second;
            opFlags.set(flagNum);
        } else {
            gridSimulation::alert(object, code);
        }

        Area::alert(object, code);
    } else if (code == SINGLE_STEP_REQUIRED) {
        controlFlags.set(single_step_mode);
        if (dynamic_cast<GridComponent*>(object) != nullptr) {
            singleStepObjects.push_back(static_cast<GridComponent*>(object));
        }
    } else {
        gridSimulation::alert(object, code);
    }
}

int gridDynSimulation::makeReady(gridState_t desiredState, const solverMode& sMode)
{
    // check to make sure we at or greater than the desiredState
    int retval = FUNCTION_EXECUTION_SUCCESS;
    // check if we have to do some steps first
    // all these function will call this function so it is a recursive function essentially
    if (pState < desiredState) {
        switch (desiredState) {
            case gridState_t::INITIALIZED:
                retval = pFlowInitialize(currentTime);
                if (retval != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "Unable to initialize power flow solution");
                    return retval;
                }
                break;
            case gridState_t::POWERFLOW_COMPLETE:
                retval = powerflow();
                if (retval != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "unable to complete power flow");
                    return retval;
                }
                break;
            case gridState_t::DYNAMIC_INITIALIZED:
                retval = dynInitialize(currentTime);
                if (retval != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "Unable to initialize dynamic solution");
                    return retval;
                }
                break;
            default:
                break;
        }
    }
    // Now if we are beyond the desired state we might have to do some additional stuff to make sure
    // everything is ready to go
    if (desiredState != pState) {
        switch (desiredState) {
            case gridState_t::INITIALIZED:
                if (!controlFlags[no_reset]) {
                    reset(reset_levels::minimal);
                }
                reInitpFlow(sMode);
                break;
            case gridState_t::DYNAMIC_INITIALIZED:
                if (pState == gridState_t::DYNAMIC_PARTIAL) {
                } else if (pState == gridState_t::DYNAMIC_COMPLETE) {
                    // check to make sure nothing has changed
                    dynamicCheckAndReset(sMode);
                }
                break;
            default:
                break;
        }
    }
    // now check the SolverInterface
    auto solverData = getSolverInterface(sMode);
    if (!solverData)  // we need to create the solver
    {
        solverData = updateSolver(sMode);
    }
    if (!solverData->isInitialized()) {
        if (desiredState == gridState_t::INITIALIZED) {
            reInitpFlow(sMode);
        } else if (desiredState == gridState_t::DYNAMIC_INITIALIZED) {
            retval = reInitDyn(sMode);
        }
    }
    return retval;
}

int gridDynSimulation::countMpiObjects(bool printInfo) const
{
    const int gridlabdObjects = searchForGridlabDobject(this);
    // print out the gridlabd objects
    if (printInfo) {
        std::cout << "NumberOfGridLABDInstances = " << gridlabdObjects << '\n';
    }
    return gridlabdObjects;
}

void gridDynSimulation::setMaxNonZeros(const solverMode& sMode, count_t nonZeros)
{
    getSolverInterface(sMode)->setMaxNonZeros(nonZeros);
}

std::shared_ptr<SolverInterface> gridDynSimulation::getSolverInterface(const solverMode& sMode)
{
    std::shared_ptr<SolverInterface> solveD;
    if (isValidIndex(sMode.offsetIndex, solverInterfaces)) {
        solveD = solverInterfaces[sMode.offsetIndex];
        if (!solveD) {
            solveD = updateSolver(sMode);
        }
    } else {
        solveD = updateSolver(sMode);
    }
    return solveD;
}

std::shared_ptr<const SolverInterface>
    gridDynSimulation::getSolverInterface(const solverMode& sMode) const
{
    if (isValidIndex(sMode.offsetIndex, solverInterfaces)) {
        return solverInterfaces[sMode.offsetIndex];
    }
    return nullptr;
}

void gridDynSimulation::add(std::shared_ptr<SolverInterface> nSolver)
{
    auto offsetIndex = nSolver->getSolverMode().offsetIndex;

    auto nextIndex = offsets.size();
    if (offsetIndex != kNullLocation) {
        if (offsetIndex <= nextIndex) {
            if (solverInterfaces[offsetIndex] == nSolver) {
                return;
            }
        }
        nextIndex = offsetIndex;
    } else {
        nextIndex =
            (std::max)(nextIndex, static_cast<decltype(nextIndex)>(5));  // new modes start at 5
        nSolver->setIndex(nextIndex);
        if (std::cmp_greater_equal(nextIndex, solverInterfaces.size())) {
            solverInterfaces.resize(nextIndex + 1);
            extraStateInformation.resize(nextIndex + 1, nullptr);
            extraDerivInformation.resize(nextIndex + 1, nullptr);
        }
    }

    auto sMode = nSolver->getSolverMode();
    nSolver->setSimulationData(this, sMode);
    solverInterfaces[nextIndex] = std::move(nSolver);
    solverInterfaces[nextIndex]->lock();
    updateSolver(sMode);
}

solverMode gridDynSimulation::getSolverMode(std::string_view solverType)
{
    solverMode sMode(kInvalidCount);
    if ((solverType == "ac") || (solverType == "acflow") || (solverType == "pflow") ||
        (solverType == "powerflow")) {
        return *defPowerFlowMode;
    }
    if ((solverType == "dae") || (solverType == "dynamic")) {
        return *defDAEMode;
    }
    if ((solverType == "algebraic_only") || (solverType == "algebraic") ||
        (solverType == "dynalg")) {
        return *defDynAlgMode;
    }
    if ((solverType == "differential_only") || (solverType == "differential") ||
        (solverType == "dyndiff")) {
        return *defDynDiffMode;
    }

    for (auto& solverData : solverInterfaces) {
        if (solverData && (solverData->getName() == solverType)) {
            return solverData->getSolverMode();
        }
    }
    if ((solverType == "dc") || (solverType == "dcflow")) {
        setDC(sMode);
        sMode.algebraic = true;
        sMode.dynamic = false;
        sMode.differential = false;
    } else if (solverType == "dc_dynamic") {
        sMode.dynamic = true;
        setDC(sMode);
    } else {
        if (solverType == "dc_algebraic") {
            setDC(sMode);
            sMode.algebraic = true;
            sMode.dynamic = true;
            sMode.differential = false;
        } else if (solverType == "dc_differential") {
            setDC(sMode);
            sMode.differential = true;
            sMode.dynamic = true;
            sMode.algebraic = false;
        } else {
            const std::shared_ptr<SolverInterface> solverData = makeSolver(solverType);
            if (solverData) {
                add(solverData);
                return solverData->getSolverMode();
            }
            return sMode;
        }
        const solverMode nMode = offsets.find(sMode);
        if (nMode.offsetIndex != sMode.offsetIndex) {
            return nMode;
        }

        sMode.offsetIndex = offsets.size();
        sMode.offsetIndex = (std::max)(sMode.offsetIndex, static_cast<index_t>(6));
        updateSolver(sMode);
    }
    return sMode;
}

solverMode gridDynSimulation::getCurrentMode(const solverMode& sMode) const
{
    if (sMode.offsetIndex != kNullLocation) {
        return sMode;
    }
    switch (pState) {
        case gridState_t::GD_ERROR:
        case gridState_t::STARTUP:
            return cLocalSolverMode;
        case gridState_t::INITIALIZED:
        case gridState_t::POWERFLOW_COMPLETE:
            return *defPowerFlowMode;
        case gridState_t::DYNAMIC_INITIALIZED:
        case gridState_t::DYNAMIC_COMPLETE:
        case gridState_t::DYNAMIC_PARTIAL:
            switch (defaultDynamicSolverMethod) {
                case dynamic_solver_methods::dae:
                    return *defDAEMode;
                case dynamic_solver_methods::partitioned:
                    return *defDynAlgMode;
                case dynamic_solver_methods::decoupled:
                    return cLocalSolverMode;
                default:
                    return *defDAEMode;
            }
        default:
            // this should never happen
            assert(false);
            return cEmptySolverMode;
    }
}

const solverMode& gridDynSimulation::getSolverMode(index_t index) const
{
    if (isValidIndex(index, solverInterfaces)) {
        return (solverInterfaces[index]) ? solverInterfaces[index]->getSolverMode() :
                                           cEmptySolverMode;
    }
    return cEmptySolverMode;
}

const solverMode* gridDynSimulation::getSolverModePtr(std::string_view solverType) const
{
    if ((solverType == "ac") || (solverType == "acflow") || (solverType == "pflow") ||
        (solverType == "powerflow")) {
        return defPowerFlowMode;
    }
    if ((solverType == "dae") || (solverType == "dynamic")) {
        return defDAEMode;
    }
    if ((solverType == "dyndiff") || (solverType == "differential_only")) {
        return defDynDiffMode;
    }
    if ((solverType == "dynalg") || (solverType == "algebraic_only")) {
        return defDynAlgMode;
    }

    for (const auto& solverData : solverInterfaces) {
        if (solverData && (solverData->getName() == solverType)) {
            return &(solverData->getSolverMode());
        }
    }
    return nullptr;
}

const solverMode* gridDynSimulation::getSolverModePtr(index_t index) const
{
    if (isValidIndex(index, solverInterfaces)) {
        return (solverInterfaces[index]) ? &(solverInterfaces[index]->getSolverMode()) : nullptr;
    }
    return nullptr;
}

std::shared_ptr<const SolverInterface> gridDynSimulation::getSolverInterface(index_t index) const
{
    return (isValidIndex(index, solverInterfaces)) ? solverInterfaces[index] : nullptr;
}

std::shared_ptr<SolverInterface> gridDynSimulation::getSolverInterface(index_t index)
{
    return (isValidIndex(index, solverInterfaces)) ? solverInterfaces[index] : nullptr;
}

std::shared_ptr<SolverInterface> gridDynSimulation::getSolverInterface(std::string_view solverName)
{
    // just run through the list of SolverInterface objects and find the first one that matches the
    // name
    if (solverName == "powerflow") {
        return getSolverInterface(*defPowerFlowMode);
    }
    if ((solverName == "dynamic") || (solverName == "dae")) {
        return getSolverInterface(*defDAEMode);
    }
    if ((solverName == "algebraic") || (solverName == "dynalg")) {
        if (defDynAlgMode != nullptr) {
            return getSolverInterface(*defDynAlgMode);
        }
        return nullptr;
    }
    if ((solverName == "differential") || (solverName == "dyndiff")) {
        if (defDynDiffMode != nullptr) {
            return getSolverInterface(*defDynDiffMode);
        }
        return nullptr;
    }
    for (auto& solverData : solverInterfaces) {
        if (solverData && (solverData->getName() == solverName)) {
            return solverData;
        }
    }
    return nullptr;
}

std::shared_ptr<SolverInterface> gridDynSimulation::updateSolver(const solverMode& sMode)
{
    auto kIndex = sMode.offsetIndex;
    solverMode solverModeValue = sMode;
    if (!isValidIndex(kIndex, solverInterfaces)) {
        if ((kIndex > 10000) || (kIndex < 0)) {
            kIndex = static_cast<index_t>(solverInterfaces.size());
            solverModeValue.offsetIndex = kIndex;
        }
        solverInterfaces.resize(kIndex + 1);
        extraStateInformation.resize(kIndex + 1, nullptr);
        extraDerivInformation.resize(kIndex + 1, nullptr);
    }
    if (!solverInterfaces[kIndex]) {
        solverInterfaces[kIndex] = makeSolver(this, sMode);
    }

    auto solverData = solverInterfaces[kIndex];
    assert(solverData);
    if (controlFlags[dense_solver]) {
        solverData->set("dense", 1.0);
    }
    solverData->set("tolerance", tols.rtol);
    if ((pState >= gridState_t::INITIALIZED) && (!isDynamic(solverModeValue))) {
        const auto stateCount = stateSize(solverModeValue);
        solverData->allocate(stateCount, rootSize(solverModeValue));
        checkOffsets(solverModeValue);
        guessState(currentTime, solverData->state_data(), nullptr, solverModeValue);
        solverData->initialize(currentTime);
    } else if (pState >= gridState_t::DYNAMIC_INITIALIZED) {
        const auto stateCount = stateSize(solverModeValue);
        solverData->allocate(stateCount, rootSize(solverModeValue));
        checkOffsets(solverModeValue);

        guessState(currentTime,
                   solverData->state_data(),
                   (hasDifferential(sMode)) ? solverData->deriv_data() : nullptr,
                   solverModeValue);

        solverData->initialize(currentTime);
    }
    return solverData;
}

void gridDynSimulation::parameterDerivatives(coreTime time,
                                             parameterSet& parameterOperators,
                                             const index_t indices[],
                                             const double values[],
                                             count_t parameterCount,
                                             const double state[],
                                             const double dstate_dt[],
                                             matrixData<double>& matrixDataRef,
                                             const solverMode& sMode)
{
    const double delta = 1e-7;
    for (index_t ii = 0; ii < parameterCount; ++ii) {
        parameterOperators[indices[ii]]->setParameter(values[ii]);
    }

    stateData stateDataValue(time, state, dstate_dt, 0);

    fillExtraStateData(stateDataValue, sMode);
    // compute this for the base case with the specified parameters
    preEx(noInputs, stateDataValue, sMode);
    const count_t nsize = stateSize(sMode);
    std::vector<double> residbase(nsize);

    std::vector<double> residChange(nsize);

    if (isDifferentialOnly(sMode)) {
        derivative(noInputs, stateDataValue, residbase.data(), sMode);
        delayedDerivative(noInputs, stateDataValue, residbase.data(), sMode);
    } else {
        residual(noInputs, stateDataValue, residbase.data(), sMode);
        delayedResidual(noInputs, stateDataValue, residbase.data(), sMode);
    }

    for (index_t pp = 0; pp < parameterCount; ++pp) {
        const double newval = values[pp] + delta;
        parameterOperators[indices[pp]]->setParameter(newval);
        if (isDifferentialOnly(sMode)) {
            derivative(noInputs, stateDataValue, residChange.data(), sMode);
            delayedDerivative(noInputs, stateDataValue, residChange.data(), sMode);
        } else {
            residual(noInputs, stateDataValue, residChange.data(), sMode);
            delayedResidual(noInputs, stateDataValue, residChange.data(), sMode);
        }
        // find the changed elements
        for (index_t stateIndex = 0; stateIndex < nsize; ++stateIndex) {
            if (std::abs(residbase[stateIndex] - residChange[stateIndex]) > 1e-12) {
                matrixDataRef.assign(stateIndex,
                                     pp,
                                     (residChange[stateIndex] - residbase[stateIndex]) / delta);
            }
        }
        // reset the parameters
        parameterOperators[indices[pp]]->setParameter(values[pp]);
    }
}

void gridDynSimulation::checkOffsets(const solverMode& sMode)
{
    if (offsets.getAlgOffset(sMode) == kNullLocation) {
        updateOffsets(sMode);
    }
}

void gridDynSimulation::getSolverReady(std::shared_ptr<SolverInterface>& solver)
{
    const auto stateCount = stateSize(solver->getSolverMode());
    if (stateCount != solver->size()) {
        reInitDyn(solver->getSolverMode());
    } else {
        checkOffsets(solver->getSolverMode());
    }
}
void gridDynSimulation::addInitOperation(std::function<int()> fptr)
{
    if (fptr) {
        additionalPowerflowSetupFunctions.push_back(std::move(fptr));
    }
}
void gridDynSimulation::fillExtraStateData(stateData& stateDataRef, const solverMode& sMode) const
{
    if ((!isDAE(sMode)) && (isDynamic(sMode))) {
        if (sMode.pairedOffsetIndex != kNullLocation) {
            const solverMode& pSMode = getSolverMode(sMode.pairedOffsetIndex);
            if (isDifferentialOnly(pSMode)) {
                if (extraStateInformation[pSMode.offsetIndex] != nullptr) {
                    stateDataRef.diffState = extraStateInformation[pSMode.offsetIndex];
                    stateDataRef.dstate_dt = extraDerivInformation[pSMode.offsetIndex];
                    stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
                    stateDataRef.pairIndex = pSMode.offsetIndex;
                } else {
                    stateDataRef.diffState = solverInterfaces[pSMode.offsetIndex]->state_data();
                    stateDataRef.dstate_dt = solverInterfaces[pSMode.offsetIndex]->deriv_data();
                    stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
                    stateDataRef.pairIndex = pSMode.offsetIndex;
                }
            } else if (isAlgebraicOnly(pSMode)) {
                stateDataRef.algState = solverInterfaces[pSMode.offsetIndex]->state_data();
                stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
                stateDataRef.pairIndex = pSMode.offsetIndex;
            } else if (isDAE(pSMode)) {
                stateDataRef.fullState = solverInterfaces[pSMode.offsetIndex]->state_data();
                stateDataRef.pairIndex = pSMode.offsetIndex;
                stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
            }
        }
    }
}

bool gridDynSimulation::checkEventsForDynamicReset(coreTime cTime, const solverMode& sMode)
{
    if (EvQ->getNextTime() < cTime) {
        auto eventReturn = EvQ->executeEvents(cTime);
        if (eventReturn > change_code::non_state_change) {
            return dynamicCheckAndReset(sMode);
        }
    }
    return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
static count_t searchForGridlabDobject(const CoreObject* obj)
{
    count_t cnt = 0;
    const auto* bus = dynamic_cast<const gridBus*>(obj);
    if (bus != nullptr) {
        index_t loadIndex = 0;
        const CoreObject* loadObject = bus->getLoad(loadIndex);
        while (loadObject != nullptr) {
            const auto* gridLabdLoad = dynamic_cast<const loads::gridLabDLoad*>(loadObject);
            if (gridLabdLoad != nullptr) {
                cnt += gridLabdLoad->mpiCount();
            }
            ++loadIndex;
            loadObject = bus->getLoad(loadIndex);
        }
        return cnt;
    }
    const auto* area = dynamic_cast<const Area*>(obj);
    if (area != nullptr) {
        index_t areaIndex = 0;
        bus = area->getBus(areaIndex);
        while (bus != nullptr) {
            cnt += searchForGridlabDobject(bus);
            ++areaIndex;
            bus = area->getBus(areaIndex);
        }
        areaIndex = 0;
        const Area* subArea = area->getArea(areaIndex);
        while (subArea != nullptr) {
            cnt += searchForGridlabDobject(subArea);
            ++areaIndex;
            subArea = area->getArea(areaIndex);
        }
        return cnt;
    }
    const auto* gridLabdLoad = dynamic_cast<const loads::gridLabDLoad*>(obj);
    if (gridLabdLoad != nullptr) {
        cnt += gridLabdLoad->mpiCount();
    }
    return cnt;
}

}  // namespace griddyn
