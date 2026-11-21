/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../GridDynSimulation.h"

#include "../GridBus.h"
#include "../Link.h"
#include "../Load.h"
#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../events/ParameterOperator.h"
#include "../loads/GridLabDLoad.h"
#include "../solvers/SolverInterface.h"
#include "Contingency.h"
#include "GridArea.h"
#include "GridDynSimulationFileOps.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#ifdef GRIDDYN_ENABLE_OPENMP_INTERNAL
#    include <omp.h>
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <compare>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
static TypeFactory<GridDynSimulation>
    gSimulationFactory("simulation", stringVec{"GridDyn", "gridlab", "gridlabd"}, "GridDyn");

std::atomic<GridDynSimulation*> GridDynSimulation::s_instance{nullptr};

// local search functions for MPI based objects
static count_t searchForGridlabDobject(const CoreObject* obj);

void GridDynSimulation::partitionedDiagnostic(std::string_view message) const
{
    if (!controlFlags[PARTITIONED_DIAGNOSTICS_FLAG]) {
        return;
    }
    std::ofstream traceFile{"partitioned-diagnostics.log", std::ios::app};
    if (traceFile.is_open()) {
        traceFile << message << '\n';
    }
}

const ModelDescription& GridDynSimulation::getModelDescription() const
{
    static const auto description = ModelDescription::define<GridDynSimulation>(
        "GridDynSimulation",
        &GridSimulation::getModelDescription(),
        {command({"powerflowfile"}, units::defunit, "Powerflowfile", ModelFieldType::STRING),
         command(
             {"powerflowinputfile"}, units::defunit, "Powerflowinputfile", ModelFieldType::STRING),
         command({"defpowerflow"},
                 units::defunit,
                 "Default power-flow solver mode",
                 ModelFieldType::STRING),
         command({"defdae"}, units::defunit, "Default DAE solver mode", ModelFieldType::STRING),
         command({"defdynalg"},
                 units::defunit,
                 "Default algebraic solver mode",
                 ModelFieldType::STRING),
         command({"defdyndiff"},
                 units::defunit,
                 "Default differential solver mode",
                 ModelFieldType::STRING),
         command({"action"}, units::defunit, "Action", ModelFieldType::STRING),
         command({"ordering"}, units::defunit, "Ordering", ModelFieldType::STRING),
         command({"dynamicsolvermethod"},
                 units::defunit,
                 "Dynamicsolvermethod",
                 ModelFieldType::STRING),
         command({"residualparallelmode", "residualparallel"},
                 units::defunit,
                 "Residualparallelmode",
                 ModelFieldType::STRING),
         command({"arkodetable", "erktable"},
                 units::defunit,
                 "ARKode explicit Runge-Kutta table",
                 ModelFieldType::STRING),
         command({"arkodecompensatedsums", "compensatedsums"},
                 units::defunit,
                 "Use compensated sums in ARKode",
                 ModelFieldType::STRING),
         parameter({"tolerance", "rtol"},
                   &GridDynSimulation::tols,
                   &tolerances::rtol,
                   units::defunit,
                   "Relative solver tolerance"),
         command({"voltagetolerance"}, units::defunit, "Voltagetolerance"),
         parameter({"angletolerance"},
                   &GridDynSimulation::tols,
                   &tolerances::angleTolerance,
                   units::rad,
                   "Voltage angle tolerance"),
         parameter({"defaulttolerance"},
                   &GridDynSimulation::tols,
                   &tolerances::defaultTolerance,
                   units::defunit,
                   "Default solver tolerance"),
         parameter({"tolerancerelaxation"},
                   &GridDynSimulation::tols,
                   &tolerances::toleranceRelaxationFactor,
                   units::defunit,
                   "Tolerance relaxation factor"),
         parameter({"powerflowstarttime"},
                   &GridDynSimulation::powerFlowStartTime,
                   units::second,
                   "Power-flow start time"),
         parameter({"timetolerance"},
                   &GridDynSimulation::tols,
                   &tolerances::timeTol,
                   units::second,
                   "Time tolerance"),
         command({"poweradjustthreshold"}, units::defunit, "Poweradjustthreshold"),
         parameter({"maxpoweradjustiterations"},
                   &GridDynSimulation::max_Padjust_iterations,
                   units::defunit,
                   "Maximum number of Power adjust iterations"),
         parameter({"maxvoltageadjustiterations"},
                   &GridDynSimulation::max_Vadjust_iterations,
                   units::defunit,
                   "Maximum number of Voltage adjust iterations"),
         flag({"autoallocate", "power_adjust"},
              &GridDynSimulation::controlFlags,
              POWER_ADJUST_ENABLED,
              "Power adjust enabled"),
         flag({"dense"}, &GridDynSimulation::controlFlags, DENSE_SOLVER, "Dense solver"),
         flag({"no_auto_autogen", "NO_AUTO_AUTOGEN"},
              &GridDynSimulation::controlFlags,
              NO_AUTO_AUTOGEN,
              "No auto autogen"),
         flag({"auto_bus_disconnect", "AUTO_BUS_DISCONNECT"},
              &GridDynSimulation::controlFlags,
              AUTO_BUS_DISCONNECT,
              "Auto bus disconnect"),
         flag({"roots_disabled", "ROOTS_DISABLED"},
              &GridDynSimulation::controlFlags,
              ROOTS_DISABLED,
              "Roots disabled"),
         flag({"voltageconstraints"},
              &GridDynSimulation::controlFlags,
              VOLTAGE_CONSTRAINTS_FLAG,
              "Voltage constraints"),
         flag({"record_on_halt"},
              &GridDynSimulation::controlFlags,
              RECORD_ON_HALT_FLAG,
              "Record on halt"),
         flag({"constraints_disabled", "CONSTRAINTS_DISABLED"},
              &GridDynSimulation::controlFlags,
              CONSTRAINTS_DISABLED,
              "Constraints disabled"),
         flag({"dc_mode", "DC_MODE"}, &GridDynSimulation::controlFlags, DC_MODE, "Dc mode"),
         flag({"dcFlow_initialization", "DC_FLOW_INITIALIZATION"},
              &GridDynSimulation::controlFlags,
              DC_FLOW_INITIALIZATION,
              "Dc flow initialization"),
         flag({"no_link_adjustments", "DISABLE_LINK_ADJUSTMENTS"},
              &GridDynSimulation::controlFlags,
              DISABLE_LINK_ADJUSTMENTS,
              "Disable link adjustments"),
         flag({"no_tap_changer", "no_tap_changer_adjustments", "DISABLE_TAP_CHANGER_ADJUSTMENTS"},
              &GridDynSimulation::controlFlags,
              DISABLE_TAP_CHANGER_ADJUSTMENTS,
              "Disable tap changer adjustments"),
         flag(
             {"no_shunt_adjustments", "no_switched_shunt_adjustments", "DISABLE_SHUNT_ADJUSTMENTS"},
             &GridDynSimulation::controlFlags,
             DISABLE_SHUNT_ADJUSTMENTS,
             "Disable shunt adjustments"),
         flag({"first_run_limits_only", "FIRST_RUN_LIMITS_ONLY"},
              &GridDynSimulation::controlFlags,
              FIRST_RUN_LIMITS_ONLY,
              "First run limits only"),
         flag({"no_reset", "NO_RESET"}, &GridDynSimulation::controlFlags, NO_RESET, "No reset"),
         flag({"ignore_bus_limits", "IGNORE_BUS_LIMITS"},
              &GridDynSimulation::controlFlags,
              IGNORE_BUS_LIMITS,
              "Ignore bus limits"),
         flag({"ignore_reactive_limits"},
              &GridDynSimulation::controlFlags,
              IGNORE_REACTIVE_POWER_LIMITS,
              "Ignore reactive power limits"),
         flag({"no_auto_slack_bus", "DISABLE_AUTO_SLACK_BUS"},
              &GridDynSimulation::controlFlags,
              DISABLE_AUTO_SLACK_BUS,
              "Disable auto slack bus"),
         flag({"no_auto_disconnect", "DISABLE_AUTO_DISCONNECT"},
              &GridDynSimulation::controlFlags,
              DISABLE_AUTO_DISCONNECT,
              "Disable auto disconnect"),
         flag({"no_automatic_load_loss", "DISABLE_AUTOMATIC_LOAD_LOSS"},
              &GridDynSimulation::controlFlags,
              DISABLE_AUTOMATIC_LOAD_LOSS,
              "Disable automatic load loss"),
         flag({"powerflow_only"},
              &GridDynSimulation::controlFlags,
              POWER_FLOW_ONLY,
              "Power flow only"),
         flag({"no_powerflow_adjustments", "NO_POWERFLOW_ADJUSTMENTS"},
              &GridDynSimulation::controlFlags,
              NO_POWERFLOW_ADJUSTMENTS,
              "No powerflow adjustments"),
         flag({"savepowerflow"},
              &GridDynSimulation::controlFlags,
              SAVE_POWER_FLOW_DATA,
              "Save power flow data"),
         flag({"savepowerflowinput"},
              &GridDynSimulation::controlFlags,
              SAVE_POWER_FLOW_INPUT_DATA,
              "Save power flow input data"),
         flag({"low_voltage_check"},
              &GridDynSimulation::controlFlags,
              LOW_VOLTAGE_CHECKING,
              "Low voltage checking"),
         flag({"no_powerflow_error_recovery", "NO_POWERFLOW_ERROR_RECOVERY"},
              &GridDynSimulation::controlFlags,
              NO_POWERFLOW_ERROR_RECOVERY,
              "No powerflow error recovery"),
         flag({"dae_initialization_for_partitioned", "DAE_INITIALIZATION_FOR_PARTITIONED"},
              &GridDynSimulation::controlFlags,
              DAE_INITIALIZATION_FOR_PARTITIONED,
              "Dae initialization for partitioned"),
         flag({"strict_governor_limits", "strictgovernorlimits"},
              &GridDynSimulation::controlFlags,
              STRICT_GOVERNOR_LIMITS,
              "Strict governor limits"),
         flag({"strict_exciter_limits", "strictexciterlimits"},
              &GridDynSimulation::controlFlags,
              STRICT_EXCITER_LIMITS,
              "Strict exciter limits"),
         flag({"ida_initial_condition_diagnostics",
               "idainitialconditiondiagnostics",
               "ida_ic_diagnostics"},
              &GridDynSimulation::controlFlags,
              IDA_INITIAL_CONDITION_DIAGNOSTICS,
              "Ida initial condition diagnostics"),
         flag({"ida_initial_condition_stop_on_failure",
               "idainitialconditionstoponfailure",
               "ida_ic_stop_on_failure"},
              &GridDynSimulation::controlFlags,
              IDA_INITIAL_CONDITION_STOP_ON_FAILURE,
              "Ida initial condition stop on failure"),
         flag({"ida_integration_diagnostics", "ida_integration_trace"},
              &GridDynSimulation::controlFlags,
              IDA_INTEGRATION_DIAGNOSTICS_FLAG,
              "Ida integration diagnostics"),
         flag({"partitioned_diagnostics", "partitioned_trace"},
              &GridDynSimulation::controlFlags,
              PARTITIONED_DIAGNOSTICS_FLAG,
              "Partitioned diagnostics"),
         flag({"disable_stabilizers"},
              &GridDynSimulation::controlFlags,
              DISABLE_STABILIZERS_FOR_DIAGNOSTICS,
              "Disable stabilizers for diagnostics"),
         flag({"force_event_powerflow", "FORCE_POWER_FLOW"},
              &GridDynSimulation::controlFlags,
              FORCE_POWER_FLOW,
              "Force power flow"),
         flag({"force_powerflow", "force_extra_powerflow", "FORCE_EXTRA_POWERFLOW"},
              &GridDynSimulation::controlFlags,
              FORCE_EXTRA_POWERFLOW,
              "Force extra powerflow"),
         flag({"droop_power_flow", "DROOP_POWER_FLOW"},
              &GridDynSimulation::controlFlags,
              DROOP_POWER_FLOW,
              "Droop power flow"),
         inverseFlag(
             {"sparse"}, &GridDynSimulation::controlFlags, DENSE_SOLVER, "Use a sparse solver"),
         command({"threads"},
                 units::defunit,
                 "Set parallel residual, Jacobian, and contingency execution",
                 ModelFieldType::BOOLEAN)});
    return description;
}

GridDynSimulation::GridDynSimulation(const std::string& objName):
    GridSimulation(objName), controlFlags(0LL)
{
// defaults
#ifndef GRIDDYN_ENABLE_KLU
    controlFlags.set(DENSE_SOLVER);
#endif
}

GridDynSimulation::~GridDynSimulation()
{
    // if we are destroying the primary instance then we need to remove it from access
    auto* instancePtr = this;
    s_instance.compare_exchange_strong(instancePtr, static_cast<GridDynSimulation*>(nullptr));
}

void GridDynSimulation::setInstance(GridDynSimulation* gds)
{
    s_instance = gds;
}

GridDynSimulation* GridDynSimulation::getInstance()
{
    return s_instance;
}

CoreObject* GridDynSimulation::clone(CoreObject* obj) const
{
    auto* sim = cloneBase<GridDynSimulation, GridSimulation>(this, obj);
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
    sim->residualParallelMode = residualParallelMode;
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
int GridDynSimulation::tripSlippedLines()
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

int GridDynSimulation::rebalanceLoadGen()
{
    std::vector<GridBus*> bnetwork;
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

bool GridDynSimulation::doAutomaticLoadLoss()
{
    std::vector<GridBus*> bnetwork;
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
        const GridBus* maxDiffBus{nullptr};
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

int GridDynSimulation::checkNetwork(NetworkCheckType checkType)
{
    // make a full list of all buses
    std::vector<GridBus*> bnetwork;
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
                    if ((busType == GridBus::BusType::SLK) || (busType == GridBus::BusType::AFIX)) {
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
    std::queue<GridBus*> bstk;
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
    count_t disconnectedWithoutSlackNetworkCount{0};
    count_t disconnectedWithoutSlackBusCount{0};
    count_t disconnectedWithoutSlackOrPvNetworkCount{0};
    count_t disconnectedWithoutSlackOrPvBusCount{0};
    for (int32_t nn = 1; nn <= networkNum; nn++) {
        bool slackFound = false;
        bool pvFound = false;
        bool afixFound = false;
        bool indirectVoltageControlFound = false;
        for (auto& slackBus : slkBusses) {
            if (slackBus->Network == nn) {
                if (slackBus->getType() == GridBus::BusType::SLK) {
                    slackFound = true;
                    break;
                }
                afixFound = true;
            }
        }
        if (!slackFound) {
            for (auto& networkBus : bnetwork) {
                if (networkBus->Network == nn) {
                    if (networkBus->getType() == GridBus::BusType::SLK) {
                        slackFound = true;
                        break;
                    }
                    if (networkBus->checkFlag(INDIRECT_VOLTAGE_CONTROL)) {
                        indirectVoltageControlFound = true;
                    }
                    if (networkBus->getType() == GridBus::BusType::PV) {
                        pvFound = true;
                    } else if (networkBus->getType() == GridBus::BusType::AFIX) {
                        afixFound = true;
                    }
                    if (afixFound && (pvFound || indirectVoltageControlFound)) {
                        // AFIX plus a PV or remote voltage controller is a reference.
                        slackFound = true;
                        break;
                    }
                }
            }
        }
        // now we go into a loop to make a new slack bus
        if (!slackFound) {
            if (controlFlags[DISABLE_AUTO_SLACK_BUS]) {
                if (controlFlags[DISABLE_AUTO_DISCONNECT]) {
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

                ++disconnectedWithoutSlackNetworkCount;
                for (auto& networkBus : bnetwork) {
                    if (networkBus->Network == nn) {
                        ++disconnectedWithoutSlackBusCount;
                        networkBus->disconnect();
                    }
                }
            } else {
                GridBus* maxCapBus = nullptr;
                double maxcap = 0.0;
                for (auto& networkBus : bnetwork) {
                    if (networkBus->Network == nn) {
                        if (networkBus->getType() == GridBus::BusType::PV) {
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
                    if (controlFlags[DISABLE_AUTO_DISCONNECT]) {
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

                    ++disconnectedWithoutSlackOrPvNetworkCount;
                    for (auto& networkBus : bnetwork) {
                        if (networkBus->Network == nn) {
                            ++disconnectedWithoutSlackOrPvBusCount;
                            networkBus->disconnect();
                        }
                    }
                }
            }
        }
    }

    if (disconnectedWithoutSlackNetworkCount > 0) {
        logging::summary(this,
                         "Automatically disconnected {} buses across {} networks without a SLK "
                         "bus",
                         disconnectedWithoutSlackBusCount,
                         disconnectedWithoutSlackNetworkCount);
    }
    if (disconnectedWithoutSlackOrPvNetworkCount > 0) {
        logging::summary(this,
                         "Automatically disconnected {} buses across {} networks without a SLK "
                         "or PV bus",
                         disconnectedWithoutSlackOrPvBusCount,
                         disconnectedWithoutSlackOrPvNetworkCount);
    }

    return FUNCTION_EXECUTION_SUCCESS;
}

double GridDynSimulation::getState(index_t offset) const
{
    return getState(offset, cLocalSolverMode);
}

double GridDynSimulation::getState(index_t offset, const SolverMode& sMode) const
{
    SolverMode nMode = sMode;
    double ret = kNullVal;
    if (isLocal(sMode)) {
        switch (pState) {
            case GridState::POWERFLOW_COMPLETE:
            case GridState::INITIALIZED:
                nMode = *defPowerFlowMode;
                break;
            case GridState::DYNAMIC_INITIALIZED:
            case GridState::DYNAMIC_COMPLETE:
            case GridState::DYNAMIC_PARTIAL:
                if (defaultDynamicSolverMethod == DynamicSolverMethods::DAE) {
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
        const double* state = solData->stateData();
        if (solData->size() > offset) {
            ret = state[offset];
        }
    }

    return ret;
}

std::vector<double> GridDynSimulation::getState(const SolverMode& sMode) const
{
    SolverMode nMode = sMode;
    if (isLocal(sMode)) {
        switch (pState) {
            case GridState::POWERFLOW_COMPLETE:
            case GridState::INITIALIZED:
                nMode = *defPowerFlowMode;
                break;
            case GridState::DYNAMIC_INITIALIZED:
            case GridState::DYNAMIC_COMPLETE:
            case GridState::DYNAMIC_PARTIAL:
                if (defaultDynamicSolverMethod == DynamicSolverMethods::DAE) {
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
        const double* state = solData->stateData();
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

void GridDynSimulation::setupOffsets(const SolverMode& sMode, OffsetOrdering offsetOrdering)
{
    SolverOffsets offsetValues;
    switch (offsetOrdering) {
        case OffsetOrdering::MIXED:
        default:
            offsetValues.algOffset = 0;
            break;
        case OffsetOrdering::GROUPED:
        case OffsetOrdering::VOLTAGE_FIRST:
            offsetValues.vOffset = 0;
            offsetValues.aOffset = voltageStateCount(sMode);
            offsetValues.algOffset = offsetValues.aOffset + angleStateCount(sMode);
            if (hasDifferential(sMode)) {
                offsetValues.diffOffset = totalAlgSize(sMode);
            }
            break;
        case OffsetOrdering::ALGEBRAIC_GROUPED:
            offsetValues.algOffset = 0;
            if (hasDifferential(sMode)) {
                offsetValues.diffOffset = totalAlgSize(sMode);
            }
            break;
        case OffsetOrdering::ANGLE_FIRST:
            offsetValues.aOffset = 0;
            offsetValues.vOffset = angleStateCount(sMode);
            offsetValues.algOffset = offsetValues.aOffset + voltageStateCount(sMode);
            if (hasDifferential(sMode)) {
                offsetValues.diffOffset = totalAlgSize(sMode);
            }
            break;
        case OffsetOrdering::DIFFERENTIAL_FIRST:
            offsetValues.diffOffset = 0;
            offsetValues.algOffset = diffSize(sMode);
            break;
    }

    // call the area setOffset function to distribute the offsets
    setOffsets(offsetValues, sMode);
}

// --------------- run the simulation ---------------

namespace {
    GridDynAction parseAction(std::string_view text)
    {
        try {
            return GridDynAction(text);
        }
        catch (const std::logic_error& error) {
            throw ActionError(ActionError::Kind::SYNTAX, error.what());
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

    while (!actionQueue.empty()) {
        auto entry = std::move(actionQueue.front());
        actionQueue.pop_front();

        const auto describeError = [&entry](std::string_view reason) {
            if (entry.source.empty()) {
                return std::format("action '{}' failed: {}", entry.text, reason);
            }
            return std::format("{}: action '{}' failed: {}", entry.source, entry.text, reason);
        };
        int result = FUNCTION_EXECUTION_SUCCESS;
        bool paused = false;
        try {
            if (const auto* error = std::get_if<std::string>(&entry.command)) {
                throw ActionError(ActionError::Kind::SYNTAX, *error);
            }
            const auto& action = std::get<GridDynAction>(entry.command);
            const auto actionEnd = actionStopTime(action);
            paused = tEnd != negTime && actionEnd && *actionEnd > tEnd;
            if (!paused) {
                result = execute(action);
            } else if (getSimulationTime() < tEnd) {
                result = executeAction(action, tEnd);
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
            actionQueue.push_front(std::move(entry));
            return FUNCTION_EXECUTION_SUCCESS;
        }
    }
    return FUNCTION_EXECUTION_SUCCESS;
}

void GridDynSimulation::add(std::string_view actionString)
{
    queueAction(actionString, {});
}

void GridDynSimulation::addActions(std::string_view actions, std::string_view source)
{
    for (const auto& action : gmlc::utilities::stringOps::splitline(actions, ';')) {
        queueAction(action, source);
    }
}

void GridDynSimulation::queueAction(std::string_view text, std::string_view source)
{
    const std::string commandText = gmlc::utilities::stringOps::trim(text);
    try {
        auto action = parseAction(commandText);
        if (action.command != GridDynAction::GdAction::IGNORE_ACTION) {
            actionQueue.push_back({std::move(action), commandText, std::string(source)});
        }
    }
    catch (const ActionError& error) {
        // Report syntax errors in script order, after any preceding actions run.
        actionQueue.push_back({std::string(error.what()), commandText, std::string(source)});
    }
}

void GridDynSimulation::add(GridDynAction& newAction)
{
    if (newAction.command != GridDynAction::GdAction::IGNORE_ACTION) {
        actionQueue.push_back({newAction, std::string(newAction.commandName()), {}});
    }
}

int GridDynSimulation::execute(std::string_view cmd)
{
    try {
        return execute(parseAction(cmd));
    }
    catch (const ActionError& error) {
        throw ActionError(error.kind(), std::format("action '{}' failed: {}", cmd, error.what()));
    }
}

int GridDynSimulation::execute(const GridDynAction& cmd)
{
    return executeAction(cmd, actionStopTime(cmd).value_or(stopTime));
}

std::optional<CoreTime> GridDynSimulation::actionStopTime(const GridDynAction& action) const
{
    using Action = GridDynAction::GdAction;
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
            return (action.val_double != kNullVal) ? CoreTime(action.val_double) : stopTime;
        case Action::ITERATE:
            return (action.val_double2 != kNullVal) ? CoreTime(action.val_double2) : stopTime;
        case Action::STEP:
            return (action.val_double != kNullVal) ? CoreTime(action.val_double) : stepTime;
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

int GridDynSimulation::executeAction(const GridDynAction& cmd, CoreTime endTime)
{
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
            ObjectInfo objectInfo(cmd.string1, this);
            if (objectInfo.mObject == nullptr) {
                throw ActionError(ActionError::Kind::TARGET,
                                  std::format("target '{}' was not found", cmd.string1));
            }
            if (cmd.val_double == kNullVal) {
                objectInfo.mObject->set(objectInfo.mField, cmd.string2);
            } else {
                objectInfo.mObject->set(objectInfo.mField, cmd.val_double, objectInfo.mUnitType);
            }
        } break;
        case GridDynAction::GdAction::SETALL:
            setAll(cmd.string1, cmd.string2, cmd.val_double);
            break;
        case GridDynAction::GdAction::SETSOLVER:
            if (cmd.string2.empty() || cmd.val_double == kNullVal) {
                throw ActionError(ActionError::Kind::UNSUPPORTED,
                                  "solver selection is unsupported");
            }
            solverSet(cmd.string1, cmd.string2, cmd.val_double);
            break;
        case GridDynAction::GdAction::POWERFLOW:
            out = powerflow();
            break;
        case GridDynAction::GdAction::CHECK:
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
            } else {
                throw ActionError(ActionError::Kind::UNSUPPORTED,
                                  std::format("unsupported check subject '{}'", cmd.string1));
            }
            break;
        case GridDynAction::GdAction::CONTINGENCY: {
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
        case GridDynAction::GdAction::INITIALIZE: {
            const CoreTime startTimeValue =
                (cmd.val_double != kNullVal) ? CoreTime(cmd.val_double) : startTime;

            if (pState == GridState::STARTUP) {
                out = pFlowInitialize(startTimeValue);
                if (out == 1) {
                    logging::error(this, "unable to initialize powerflow");
                    out = FUNCTION_EXECUTION_FAILURE;
                }
            } else if (pState == GridState::POWERFLOW_COMPLETE) {
                out = dynInitialize(startTimeValue);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "unable to complete dynamic power initialization");
                    return FUNCTION_EXECUTION_FAILURE;
                }
            }
        } break;
        case GridDynAction::GdAction::ITERATE: {
            const CoreTime timeStepValue =
                (cmd.val_double != kNullVal) ? CoreTime(cmd.val_double) : stepTime;
            out = eventDrivenPowerflow(endTime, timeStepValue);
        } break;
        case GridDynAction::GdAction::EVENTMODE: {
            if (cmd.val_double2 != kNullVal) {
                const CoreTime timeStepValue = cmd.val_double2;
                out = eventDrivenPowerflow(endTime, timeStepValue);
            } else {
                out = eventDrivenPowerflow(endTime);
            }
        }

        break;
        case GridDynAction::GdAction::DYNAMIC_DAE:
        case GridDynAction::GdAction::DYNAMIC_PART: {
            const auto method = command == GridDynAction::GdAction::DYNAMIC_DAE ?
                DynamicSolverMethods::DAE :
                DynamicSolverMethods::PARTITIONED;
            requireDynamicSolverMethod(method);
            if (pState < GridState::DYNAMIC_INITIALIZED) {
                out = initializeDynamics(negTime, method);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                logging::warning(this, "No Differential states halting computation");
                return out;
            }
            if (method == DynamicSolverMethods::DAE) {
                out = dynamicDAE(endTime);
            } else {
                const CoreTime interval =
                    (cmd.val_double2 != kNullVal) ? CoreTime(cmd.val_double2) : stepTime;
                out = dynamicPartitioned(endTime, interval);
            }
        } break;
        case GridDynAction::GdAction::DYNAMIC_DECOUPLED:
            throw ActionError(ActionError::Kind::UNSUPPORTED, "decoupled dynamics is unsupported");
        case GridDynAction::GdAction::STEP: {
            requireDynamicSolverMethod(DynamicSolverMethods::DAE);
            CoreTime timeActual =
                (cmd.val_double2 != kNullVal) ? CoreTime(cmd.val_double2) : stopTime;
            if (pState < GridState::DYNAMIC_INITIALIZED) {
                out = initializeDynamics(negTime, DynamicSolverMethods::DAE);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }

            if (!hasDynamics()) {
                logging::warning(this, "No Differential states halting computation");
                return out;
            }
            if (!getSolverInterface(*defDAEMode)->isInitialized()) {
                out = reInitDyn(*defDAEMode);
                if (out != FUNCTION_EXECUTION_SUCCESS) {
                    return out;
                }
            }
            out = step(endTime, timeActual);
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
                out = dynInitialize(currentTime);
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
                        out = dynamicPartitioned(endTime, stepTime);
                        break;
                    case DynamicSolverMethods::DECOUPLED:
                        throw ActionError(ActionError::Kind::UNSUPPORTED,
                                          "decoupled dynamics is unsupported");
                }
            } else {
                logging::summary(this, "No Differential states reverting to stepped power flow");
                out = eventDrivenPowerflow(endTime, stepTime);
            }

            if (getSimulationTime() >= stopTime) {
                saveRecorders();
            }
            break;
        case GridDynAction::GdAction::RESET:
            if (cmd.val_int1 >= 0) {
                reset(static_cast<ResetLevels>(cmd.val_int1));
            } else {
                reset(ResetLevels::MINIMAL);
            }
            break;
        case GridDynAction::GdAction::SAVE:
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
            } else {
                throw ActionError(ActionError::Kind::UNSUPPORTED,
                                  std::format("unsupported save subject '{}'", cmd.string1));
            }
            break;
        default:
            throw ActionError(ActionError::Kind::UNSUPPORTED,
                              std::format("unsupported command '{}'", cmd.commandName()));
    }
    return out;
}

bool GridDynSimulation::hasDynamics() const
{
    return diffSize(*defDAEMode) > 0;
}

void GridDynSimulation::configureResidualParallelism()
{
    constexpr count_t autoBusThreshold = 1200;
    const auto totalBuses = static_cast<count_t>(getInt("totalbuscount"));
    const bool enable = (residualParallelMode == ResidualParallelMode::ON) ||
        ((residualParallelMode == ResidualParallelMode::AUTO) && (totalBuses >= autoBusThreshold));
#ifdef GRIDDYN_ENABLE_OPENMP_INTERNAL
    constexpr count_t maxResidualThreads = 8;
    if (enable) {
        const auto availableThreads = static_cast<count_t>(omp_get_max_threads());
        const auto busThreadCount = std::max<count_t>(1, (totalBuses + 299) / 300);
        const auto threadCount = std::min({availableThreads, busThreadCount, maxResidualThreads});
        setResidualThreadCount(static_cast<int>(threadCount));
    } else {
        setResidualThreadCount(1);
    }
#else
    (void)enable;
    setResidualThreadCount(1);
#endif
}

// need to update probably with a new field in SolverInterface
count_t GridDynSimulation::nonZeros(const SolverMode& sMode) const
{
    auto solverInterface = getSolverInterface(sMode);
    return solverInterface ? solverInterface->nonZeros() : 0;
}

// --------------- set properties ---------------
void GridDynSimulation::set(std::string_view param, std::string_view val)
{
    if (param == "powerflowfile") {
        powerFlowFile = val;
        controlFlags.set(SAVE_POWER_FLOW_DATA);
    } else if (param == "powerflowinputfile") {
        powerFlowInputFile = val;
        controlFlags.set(SAVE_POWER_FLOW_INPUT_DATA);
    } else if (param == "defpowerflow") {
        setDefaultMode(SolutionModes::POWERFLOW_MODE, getSolverMode(std::string{val}));
    } else if (param == "defdae") {
        setDefaultMode(SolutionModes::DAE_MODE, getSolverMode(std::string{val}));
    } else if (param == "defdynalg") {
        setDefaultMode(SolutionModes::ALGEBRAIC_MODE, getSolverMode(std::string{val}));
    } else if (param == "defdyndiff") {
        setDefaultMode(SolutionModes::DIFFERENTIAL_MODE, getSolverMode(std::string{val}));
    } else if (param == "action") {
        addActions(val);
    } else if (param == "ordering") {
        auto order = gmlc::utilities::convertToLowerCase(val);
        if (order == "mixed") {
            default_ordering = OffsetOrdering::MIXED;
        } else if (order == "grouped") {
            default_ordering = OffsetOrdering::GROUPED;
        } else if (order == "algebraic_grouped") {
            default_ordering = OffsetOrdering::ALGEBRAIC_GROUPED;
        } else if (order == "voltage_first") {
            default_ordering = OffsetOrdering::VOLTAGE_FIRST;
        } else if (order == "angle_first") {
            default_ordering = OffsetOrdering::ANGLE_FIRST;
        } else if (order == "differential_first") {
            default_ordering = OffsetOrdering::DIFFERENTIAL_FIRST;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if (param == "dynamicsolvermethod") {
        auto method = gmlc::utilities::convertToLowerCase(val);
        if (method == "dae") {
            registerDynamicSolver(*defDAEMode);
            defaultDynamicSolverMethod = DynamicSolverMethods::DAE;
        } else if (method == "partitioned") {
            registerDynamicSolver(*defDynAlgMode);
            registerDynamicSolver(*defDynDiffMode);
            defaultDynamicSolverMethod = DynamicSolverMethods::PARTITIONED;
        } else if (method == "decoupled") {
            defaultDynamicSolverMethod = DynamicSolverMethods::DECOUPLED;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if ((param == "residualparallelmode") || (param == "residualparallel")) {
        auto parallelMode = gmlc::utilities::convertToLowerCase(val);
        if (parallelMode == "off") {
            residualParallelMode = ResidualParallelMode::OFF;
        } else if (parallelMode == "on") {
            residualParallelMode = ResidualParallelMode::ON;
        } else if (parallelMode == "auto") {
            residualParallelMode = ResidualParallelMode::AUTO;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if ((param == "arkodetable") || (param == "erktable") ||
               (param == "arkodecompensatedsums") || (param == "compensatedsums")) {
        // These are solver-level ARKode controls, but accepting them here lets
        // command-line and simulation-file parameters reach the default
        // differential solver without adding model parameters.
        auto solverData = getSolverInterface(*defDynDiffMode);
        if (solverData) {
            solverData->set(param, val);
        } else {
            throw(InvalidParameterValue(param));
        }
    } else {
        GridSimulation::set(param, val);
    }
}

std::string GridDynSimulation::getString(std::string_view param) const
{
    if (param == "powerflowfile") {
        return powerFlowFile;
    }
    if (param == "powerflowinputfile") {
        return powerFlowInputFile;
    }
    return GridSimulation::getString(param);
}

void GridDynSimulation::setDefaultMode(SolutionModes mode, const SolverMode& sMode)
{
    auto solverData = getSolverInterface(sMode);
    if (!solverData) {
        solverData = makeSolver(this, sMode);
        add(solverData);
    }
    switch (mode) {
        case SolutionModes::POWERFLOW_MODE:
            if (isAlgebraicOnly(sMode)) {
                if (opFlags[POWERFLOW_INITIALIZED]) {
                    if (solverData->isInitialized()) {
                        reInitpFlow(solverData->getSolverMode());
                    }
                }
                solverData->set("mode", "powerflow");
                defPowerFlowMode = &(solverData->getSolverMode());
            } else {
                throw(InvalidParameterValue("mode"));
            }
            break;
        case SolutionModes::DAE_MODE:
            if (isDAE(sMode)) {
                if (opFlags[DYN_INITIALIZED]) {
                    if (solverData->isInitialized()) {
                        reInitDyn(solverData->getSolverMode());
                    }
                }

                defDAEMode = &(solverData->getSolverMode());
            } else {
                throw(InvalidParameterValue("mode"));
            }
            break;
        case SolutionModes::ALGEBRAIC_MODE:
            if (isAlgebraicOnly(sMode)) {
                if (opFlags[DYN_INITIALIZED]) {
                    if (solverData->isInitialized()) {
                        reInitDyn(solverData->getSolverMode());
                    }
                }
                solverData->setFlag("dynamic", true);
                defDynAlgMode = &(solverData->getSolverMode());
            } else {
                throw(InvalidParameterValue("mode"));
            }
            break;
        case SolutionModes::DIFFERENTIAL_MODE:
            if (isDifferentialOnly(sMode)) {
                if (opFlags[DYN_INITIALIZED]) {
                    if (solverData->isInitialized()) {
                        reInitDyn(solverData->getSolverMode());
                    }
                }
                defDynDiffMode = &(solverData->getSolverMode());
            } else {
                throw(InvalidParameterValue("mode"));
            }
            break;
        default:
            throw(UnrecognizedParameter("mode"));
            break;
    }
}

void GridDynSimulation::setFlag(std::string_view flag, bool val)
{
    const bool useDaeInitialization = controlFlags[DAE_INITIALIZATION_FOR_PARTITIONED];
    if (GridDynSimulation::getModelDescription().trySetFlag(*this, flag, val)) {
        if (!useDaeInitialization && controlFlags[DAE_INITIALIZATION_FOR_PARTITIONED]) {
            registerDynamicSolver(*defDAEMode);
        }
        return;
    }
    if (flag == "threads") {
        controlFlags.set(PARALLEL_RESIDUAL_ENABLED, val);
        controlFlags.set(PARALLEL_CONTINGENCY_ENABLED, val);
        controlFlags.set(PARALLEL_JACOBIAN_ENABLED, val);
        residualParallelMode = val ? ResidualParallelMode::ON : ResidualParallelMode::OFF;
    } else {
        GridSimulation::setFlag(flag, val);
    }
}

bool GridDynSimulation::getFlag(std::string_view flag) const
{
    if (const auto value = GridDynSimulation::getModelDescription().tryGetFlag(*this, flag)) {
        return *value;
    }
    if (flag == "threads") {
        return controlFlags[PARALLEL_RESIDUAL_ENABLED] &&
            controlFlags[PARALLEL_CONTINGENCY_ENABLED] && controlFlags[PARALLEL_JACOBIAN_ENABLED];
    }
    return GridSimulation::getFlag(flag);
}

void GridDynSimulation::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(
            GridDynSimulation::getModelDescription().trySet(*this, param, val, unitType))) {
        return;
    }
    if (param == "voltagetolerance") {
        tols.voltageTolerance = units::convert(val, unitType, units::puV, systemBasePower);
    } else if (param == "poweradjustthreshold") {
        powerAdjustThreshold = units::convert(val, unitType, units::puMW, systemBasePower);
    } else if (param == "defpowerflow") {
        setDefaultMode(SolutionModes::POWERFLOW_MODE, getSolverMode(static_cast<index_t>(val)));
    } else if (param == "defdae") {
        setDefaultMode(SolutionModes::DAE_MODE, getSolverMode(static_cast<index_t>(val)));
    } else if (param == "defdynalg") {
        setDefaultMode(SolutionModes::ALGEBRAIC_MODE, getSolverMode(static_cast<index_t>(val)));
    } else if (param == "defdyndiff") {
        setDefaultMode(SolutionModes::DIFFERENTIAL_MODE, getSolverMode(static_cast<index_t>(val)));
    } else {
        try {
            GridSimulation::set(param, val, unitType);
        }
        catch (const UnrecognizedParameter&) {
            setFlag(param, val != 0.0);
        }
    }
}

void GridDynSimulation::solverSet(std::string_view solverName, std::string_view field, double val)
{
    auto solverData = getSolverInterface(solverName);
    if (!solverData) {
        throw ActionError(ActionError::Kind::TARGET,
                          std::format("solver '{}' was not found", solverName));
    }
    solverData->set(field, val);
}

void GridDynSimulation::solverSet(std::string_view solverName,
                                  std::string_view field,
                                  std::string_view val)
{
    auto solverData = getSolverInterface(solverName);
    if (!solverData) {
        throw ActionError(ActionError::Kind::TARGET,
                          std::format("solver '{}' was not found", solverName));
    }
    solverData->set(field, val);
}

double GridDynSimulation::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            GridDynSimulation::getModelDescription().tryGet(*this, param, unitType)) {
        return *value;
    }
    if (param == "threads") {
        return getFlag(param) ? 1.0 : 0.0;
    }
    count_t val = kInvalidCount;
    double fval = kNullVal;

    if (param == "statesize") {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
            val = stateSize(*defPowerFlowMode);
        } else {
            val = stateSize(*defDAEMode);
        }
    } else if (param == "vcount") {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
            val = voltageStateCount(*defPowerFlowMode);
        } else {
            val = voltageStateCount(*defDAEMode);
        }
    } else if (param == "account") {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
            val = angleStateCount(*defPowerFlowMode);
        } else {
            val = angleStateCount(*defDAEMode);
        }
    } else if (param == "algcount") {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
            val = algSize(*defPowerFlowMode);
        } else {
            val = algSize(*defDAEMode);
        }
    } else if (param == "jacsize") {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
            val = jacSize(*defPowerFlowMode);
        } else {
            val = jacSize(*defDAEMode);
        }
    } else if ((param == "diffcount") || (param == "diffsize")) {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
            val = 0;
        } else {
            val = diffSize(*defDAEMode);
        }
    } else if (param == "nonzeros") {
        if (pState <= GridState::POWERFLOW_COMPLETE) {
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
    } else if (param == "poweradjustthreshold") {
        fval = powerAdjustThreshold;
    } else {
        fval = GridSimulation::get(param, unitType);
    }
    return (val != kInvalidCount) ? static_cast<double>(val) : fval;
}

static const std::map<int, size_t>& getAlertFlags()
{
    static const std::map<int, size_t> alertFlags{
        std::make_pair(STATE_COUNT_CHANGE, STATE_CHANGE_FLAG),
        std::make_pair(STATE_COUNT_INCREASE, STATE_CHANGE_FLAG),
        std::make_pair(STATE_COUNT_DECREASE, STATE_CHANGE_FLAG),
        std::make_pair(ROOT_COUNT_CHANGE, ROOT_CHANGE_FLAG),
        std::make_pair(ROOT_COUNT_INCREASE, ROOT_CHANGE_FLAG),
        std::make_pair(ROOT_COUNT_DECREASE, ROOT_CHANGE_FLAG),
        std::make_pair(JAC_COUNT_CHANGE, JACOBIAN_COUNT_CHANGE_FLAG),
        std::make_pair(JAC_COUNT_DECREASE, JACOBIAN_COUNT_CHANGE_FLAG),
        std::make_pair(JAC_COUNT_INCREASE, JACOBIAN_COUNT_CHANGE_FLAG),
        std::make_pair(OBJECT_COUNT_CHANGE, OBJECT_CHANGE_FLAG),
        std::make_pair(OBJECT_COUNT_INCREASE, OBJECT_CHANGE_FLAG),
        std::make_pair(OBJECT_COUNT_DECREASE, OBJECT_CHANGE_FLAG),
        std::make_pair(CONSTRAINT_COUNT_CHANGE, CONSTRAINT_CHANGE_FLAG),
        std::make_pair(CONSTRAINT_COUNT_INCREASE, CONSTRAINT_CHANGE_FLAG),
        std::make_pair(CONSTRAINT_COUNT_DECREASE, CONSTRAINT_CHANGE_FLAG),
        std::make_pair(SLACK_BUS_CHANGE, SLACK_BUS_CHANGE_FLAG),
        std::make_pair(POTENTIAL_FAULT_CHANGE, RESET_VOLTAGE_FLAG),
        std::make_pair(VERY_LOW_VOLTAGE_ALERT, LOW_BUS_VOLTAGE),
        std::make_pair(INVALID_STATE_ALERT, INVALID_STATE_FLAG),
        std::make_pair(CONNECTIVITY_CHANGE, CONNECTIVITY_CHANGE_FLAG),

    };
    return alertFlags;
}

void GridDynSimulation::alert(CoreObject* object, int code)
{
    if ((code >= MIN_CHANGE_ALERT) && (code < MAX_CHANGE_ALERT)) {
        const auto& alertFlags = getAlertFlags();
        auto res = alertFlags.find(code);
        if (res != alertFlags.end()) {
            auto flagNum = res->second;
            opFlags.set(flagNum);
        } else {
            GridSimulation::alert(object, code);
        }

        GridArea::alert(object, code);
    } else if (code == SINGLE_STEP_REQUIRED) {
        controlFlags.set(SINGLE_STEP_MODE);
        if (dynamic_cast<GridComponent*>(object) != nullptr) {
            singleStepObjects.push_back(static_cast<GridComponent*>(object));
        }
    } else {
        GridSimulation::alert(object, code);
    }
}

int GridDynSimulation::makeReady(GridState desiredState, const SolverMode& sMode)
{
    // check to make sure we at or greater than the desiredState
    int retval = FUNCTION_EXECUTION_SUCCESS;
    // check if we have to do some steps first
    // all these function will call this function so it is a recursive function essentially
    if (pState < desiredState) {
        switch (desiredState) {
            case GridState::INITIALIZED:
                retval = pFlowInitialize(currentTime);
                if (retval != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "Unable to initialize power flow solution");
                    return retval;
                }
                break;
            case GridState::POWERFLOW_COMPLETE:
                retval = powerflow();
                if (retval != FUNCTION_EXECUTION_SUCCESS) {
                    logging::error(this, "unable to complete power flow");
                    return retval;
                }
                break;
            case GridState::DYNAMIC_INITIALIZED:
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
            case GridState::INITIALIZED:
                if (!controlFlags[NO_RESET]) {
                    reset(ResetLevels::MINIMAL);
                }
                reInitpFlow(sMode);
                break;
            case GridState::DYNAMIC_INITIALIZED:
                if (pState == GridState::DYNAMIC_PARTIAL) {
                } else if (pState == GridState::DYNAMIC_COMPLETE) {
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
        if (desiredState == GridState::INITIALIZED) {
            reInitpFlow(sMode);
        } else if (desiredState == GridState::DYNAMIC_INITIALIZED) {
            retval = reInitDyn(sMode);
        }
    }
    return retval;
}

int GridDynSimulation::countMpiObjects(bool printInfo) const
{
    const int gridlabdObjects = searchForGridlabDobject(this);
    // print out the gridlabd objects
    if (printInfo) {
        std::cout << "NumberOfGridLABDInstances = " << gridlabdObjects << '\n';
    }
    return gridlabdObjects;
}

void GridDynSimulation::setMaxNonZeros(const SolverMode& sMode, count_t nonZeros)
{
    getSolverInterface(sMode)->setMaxNonZeros(nonZeros);
}

std::shared_ptr<SolverInterface> GridDynSimulation::getSolverInterface(const SolverMode& sMode)
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
    GridDynSimulation::getSolverInterface(const SolverMode& sMode) const
{
    if (isValidIndex(sMode.offsetIndex, solverInterfaces)) {
        return solverInterfaces[sMode.offsetIndex];
    }
    return nullptr;
}

void GridDynSimulation::add(std::shared_ptr<SolverInterface> nSolver)
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

SolverMode GridDynSimulation::getSolverMode(std::string_view solverType)
{
    SolverMode sMode(kInvalidCount);
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
        const SolverMode nMode = offsets.find(sMode);
        if (nMode.offsetIndex != sMode.offsetIndex) {
            return nMode;
        }

        sMode.offsetIndex = offsets.size();
        sMode.offsetIndex = (std::max)(sMode.offsetIndex, static_cast<index_t>(6));
        updateSolver(sMode);
    }
    return sMode;
}

SolverMode GridDynSimulation::getCurrentMode(const SolverMode& sMode) const
{
    if (sMode.offsetIndex != kNullLocation) {
        return sMode;
    }
    switch (pState) {
        case GridState::GD_ERROR:
        case GridState::STARTUP:
            return cLocalSolverMode;
        case GridState::INITIALIZED:
        case GridState::POWERFLOW_COMPLETE:
            return *defPowerFlowMode;
        case GridState::DYNAMIC_INITIALIZED:
        case GridState::DYNAMIC_COMPLETE:
        case GridState::DYNAMIC_PARTIAL:
            switch (defaultDynamicSolverMethod) {
                case DynamicSolverMethods::DAE:
                    return *defDAEMode;
                case DynamicSolverMethods::PARTITIONED:
                    return *defDynAlgMode;
                case DynamicSolverMethods::DECOUPLED:
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

const SolverMode& GridDynSimulation::getSolverMode(index_t index) const
{
    if (isValidIndex(index, solverInterfaces)) {
        return (solverInterfaces[index]) ? solverInterfaces[index]->getSolverMode() :
                                           cEmptySolverMode;
    }
    return cEmptySolverMode;
}

const SolverMode* GridDynSimulation::getSolverModePtr(std::string_view solverType) const
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

const SolverMode* GridDynSimulation::getSolverModePtr(index_t index) const
{
    if (isValidIndex(index, solverInterfaces)) {
        return (solverInterfaces[index]) ? &(solverInterfaces[index]->getSolverMode()) : nullptr;
    }
    return nullptr;
}

std::shared_ptr<const SolverInterface> GridDynSimulation::getSolverInterface(index_t index) const
{
    return (isValidIndex(index, solverInterfaces)) ? solverInterfaces[index] : nullptr;
}

std::shared_ptr<SolverInterface> GridDynSimulation::getSolverInterface(index_t index)
{
    return (isValidIndex(index, solverInterfaces)) ? solverInterfaces[index] : nullptr;
}

std::shared_ptr<SolverInterface> GridDynSimulation::getSolverInterface(std::string_view solverName)
{
    if (solverName == "powerflow") {
        return defPowerFlowMode ? getSolverInterface(*defPowerFlowMode) : nullptr;
    }
    if ((solverName == "dynamic") || (solverName == "dae")) {
        return defDAEMode ? getSolverInterface(*defDAEMode) : nullptr;
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

std::shared_ptr<SolverInterface> GridDynSimulation::updateSolver(const SolverMode& sMode)
{
    return updateSolver(sMode, true);
}

void GridDynSimulation::registerDynamicSolver(const SolverMode& mode)
{
    if (!getSolverInterface(mode.offsetIndex)) {
        updateSolver(mode, false);
    }
}

std::shared_ptr<SolverInterface> GridDynSimulation::updateSolver(const SolverMode& sMode,
                                                                 bool initialize)
{
    auto kIndex = sMode.offsetIndex;
    SolverMode solverModeValue = sMode;
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
    if (controlFlags[DENSE_SOLVER]) {
        solverData->set("dense", 1.0);
    }
    solverData->set("tolerance", tols.rtol);
    solverData->setFlag("ida_ic_diagnostics", controlFlags[IDA_INITIAL_CONDITION_DIAGNOSTICS]);
    solverData->setFlag("ida_ic_stop_on_failure",
                        controlFlags[IDA_INITIAL_CONDITION_STOP_ON_FAILURE]);
    solverData->setFlag("ida_integration_diagnostics",
                        controlFlags[IDA_INTEGRATION_DIAGNOSTICS_FLAG]);
    if (!initialize) {
        return solverData;
    }
    if ((pState >= GridState::INITIALIZED) && (!isDynamic(solverModeValue))) {
        const auto stateCount = stateSize(solverModeValue);
        solverData->allocate(stateCount, rootSize(solverModeValue));
        checkOffsets(solverModeValue);
        guessState(currentTime, solverData->stateData(), nullptr, solverModeValue);
        solverData->initialize(currentTime);
    } else if (pState >= GridState::DYNAMIC_INITIALIZED) {
        const auto stateCount = stateSize(solverModeValue);
        solverData->allocate(stateCount, rootSize(solverModeValue));
        checkOffsets(solverModeValue);

        guessState(currentTime,
                   solverData->stateData(),
                   (hasDifferential(sMode)) ? solverData->derivData() : nullptr,
                   solverModeValue);

        solverData->initialize(currentTime);
    }
    return solverData;
}

void GridDynSimulation::parameterDerivatives(CoreTime time,
                                             ParameterSet& parameterOperators,
                                             const index_t indices[],
                                             const double values[],
                                             count_t parameterCount,
                                             const double state[],
                                             const double dstateDt[],
                                             MatrixData<double>& matrixDataRef,
                                             const SolverMode& sMode)
{
    const double delta = 1e-7;
    for (index_t ii = 0; ii < parameterCount; ++ii) {
        parameterOperators[indices[ii]]->setParameter(values[ii]);
    }

    StateData stateDataValue(time, state, dstateDt, 0);

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

void GridDynSimulation::checkOffsets(const SolverMode& sMode)
{
    if (offsets.getAlgOffset(sMode) == kNullLocation) {
        updateOffsets(sMode);
    }
}

void GridDynSimulation::getSolverReady(std::shared_ptr<SolverInterface>& solver)
{
    const auto stateCount = stateSize(solver->getSolverMode());
    if (stateCount != solver->size()) {
        reInitDyn(solver->getSolverMode());
    } else {
        checkOffsets(solver->getSolverMode());
    }
}
void GridDynSimulation::addInitOperation(std::function<int()> fptr)
{
    if (fptr) {
        additionalPowerflowSetupFunctions.push_back(std::move(fptr));
    }
}
void GridDynSimulation::fillExtraStateData(StateData& stateDataRef, const SolverMode& sMode) const
{
    if ((!isDAE(sMode)) && (isDynamic(sMode))) {
        if (sMode.pairedOffsetIndex != kNullLocation) {
            const SolverMode& pSMode = getSolverMode(sMode.pairedOffsetIndex);
            if (isDifferentialOnly(pSMode)) {
                if (extraStateInformation[pSMode.offsetIndex] != nullptr) {
                    stateDataRef.diffState = extraStateInformation[pSMode.offsetIndex];
                    stateDataRef.dstate_dt = extraDerivInformation[pSMode.offsetIndex];
                    stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
                    stateDataRef.pairIndex = pSMode.offsetIndex;
                } else {
                    stateDataRef.diffState = solverInterfaces[pSMode.offsetIndex]->stateData();
                    stateDataRef.dstate_dt = solverInterfaces[pSMode.offsetIndex]->derivData();
                    stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
                    stateDataRef.pairIndex = pSMode.offsetIndex;
                }
            } else if (isAlgebraicOnly(pSMode)) {
                stateDataRef.algState = solverInterfaces[pSMode.offsetIndex]->stateData();
                stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
                stateDataRef.pairIndex = pSMode.offsetIndex;
            } else if (isDAE(pSMode)) {
                stateDataRef.fullState = solverInterfaces[pSMode.offsetIndex]->stateData();
                stateDataRef.pairIndex = pSMode.offsetIndex;
                stateDataRef.altTime = solverInterfaces[pSMode.offsetIndex]->getSolverTime();
            }
        }
    }
}

bool GridDynSimulation::checkEventsForDynamicReset(CoreTime cTime, const SolverMode& sMode)
{
    if (EvQ->getNextTime() < cTime) {
        auto eventReturn = EvQ->executeEvents(cTime);
        if (eventReturn > ChangeCode::NON_STATE_CHANGE) {
            return dynamicCheckAndReset(sMode);
        }
    }
    return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
static count_t searchForGridlabDobject(const CoreObject* obj)
{
    count_t cnt = 0;
    const auto* bus = dynamic_cast<const GridBus*>(obj);
    if (bus != nullptr) {
        index_t loadIndex = 0;
        const CoreObject* loadObject = bus->getLoad(loadIndex);
        while (loadObject != nullptr) {
            const auto* gridLabdLoad = dynamic_cast<const loads::GridLabDLoad*>(loadObject);
            if (gridLabdLoad != nullptr) {
                cnt += gridLabdLoad->mpiCount();
            }
            ++loadIndex;
            loadObject = bus->getLoad(loadIndex);
        }
        return cnt;
    }
    const auto* area = dynamic_cast<const GridArea*>(obj);
    if (area != nullptr) {
        index_t areaIndex = 0;
        bus = area->getBus(areaIndex);
        while (bus != nullptr) {
            cnt += searchForGridlabDobject(bus);
            ++areaIndex;
            bus = area->getBus(areaIndex);
        }
        areaIndex = 0;
        const GridArea* subGridArea = area->getGridArea(areaIndex);
        while (subGridArea != nullptr) {
            cnt += searchForGridlabDobject(subGridArea);
            ++areaIndex;
            subGridArea = area->getGridArea(areaIndex);
        }
        return cnt;
    }
    const auto* gridLabdLoad = dynamic_cast<const loads::GridLabDLoad*>(obj);
    if (gridLabdLoad != nullptr) {
        cnt += gridLabdLoad->mpiCount();
    }
    return cnt;
}

}  // namespace griddyn
