/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../GridBus.h"
#include "../GridDynSimulation.h"
#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../simulation/Diagnostics.h"
#include "../solvers/SolverInterface.h"
#include "Continuation.h"
#include "GridDynSimulationFileOps.h"
#include "PowerFlowErrorRecovery.h"
#include "gmlc/utilities/vectorOps.hpp"
// system headers
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
// --------------- power flow program ---------------

// power flow solver
int GridDynSimulation::powerflow()
{
    const SolverMode& solverModeRef = *defPowerFlowMode;
    const int out = FUNCTION_EXECUTION_SUCCESS;
    count_t voltage_iteration_count = 0;
    count_t power_iteration_count = 0;
    double prevPower = 0;
    int retval = makeReady(GridState::INITIALIZED, solverModeRef);
    if (retval != FUNCTION_EXECUTION_SUCCESS) {
        logging::error(this, "Unable to get simulation ready for power flow");
        return retval;
    }
    // load the vectors
    // operate in a loop then check then repeat
    bool hasPowerAdjustments = controlFlags[POWER_ADJUST_ENABLED];

    std::vector<double> slkBusBase(slkBusses.size());

    auto pFlowData = getSolverInterface(solverModeRef);
    // Create the error recovery object to use if necessary
    powerFlowErrorRecovery pfer(this, pFlowData);

    if (pFlowData->size() > 0) {
        // handle the condition when all buses are swing buses hence nothing to solve
        power_iteration_count = 0;
        count_t rebalance_count{0};
        do  // outer power distribution loop
        {
            if (hasPowerAdjustments) {
                prevPower = 0;
                for (size_t kk = 0; kk < slkBusses.size(); ++kk) {
                    slkBusBase[kk] = slkBusses[kk]->getGenerationRealNominal();
                    prevPower += slkBusBase[kk];
                }
            }
            voltage_iteration_count = 0;
            ChangeCode AdjustmentChanges = ChangeCode::NO_CHANGE;
            do {
                guessState(currentTime, pFlowData->stateData(), nullptr, solverModeRef);
                if ((controlFlags[SAVE_POWER_FLOW_INPUT_DATA] &&
                     !controlFlags[POWER_FLOW_INPUT_SAVED])) {
                    savePowerFlow(this, powerFlowInputFile);
                    controlFlags[POWER_FLOW_INPUT_SAVED] = true;
                }
                // solve
                CoreTime returnTime = currentTime;
                retval = pFlowData->solve(currentTime, returnTime);

                if (retval < 0) {
                    logging::warning(this, "solver error return code:{}", retval);

                    if (controlFlags[NO_POWERFLOW_ERROR_RECOVERY]) {
                        logging::error(this,
                                       "unable to solve power flow ||{}",
                                       pFlowData->getLastErrorString());

                        return retval;
                    }
                    auto prc = pfer.attemptFix(retval);
                    if (prc == powerFlowErrorRecovery::RecoveryReturnCodes::OUT_OF_OPTIONS) {
                        if (tripSlippedLines() > 0) {
                            checkNetwork(NetworkCheckType::FULL);
                            reInitpFlow(solverModeRef, ChangeCode::JACOBIAN_CHANGE);
                            if (hasPowerAdjustments) {
                                slkBusBase.resize(slkBusses.size());
                                for (size_t kk = 0; kk < slkBusses.size(); ++kk) {
                                    slkBusBase[kk] = slkBusses[kk]->getGenerationRealNominal();
                                }
                            }
                            continue;
                        }
                        if (!controlFlags[DISABLE_AUTOMATIC_LOAD_LOSS]) {
                            ++rebalance_count;
                            if (rebalance_count < 3) {
                                const int rebalanceStatus = rebalanceLoadGen();
                                if (rebalanceStatus == 0) {
                                    continue;
                                }
                            }
                            if (doAutomaticLoadLoss()) {
                                continue;
                            }
                        }

                        logging::error(this,
                                       "unable to solve power flow ||{}",
                                       pFlowData->getLastErrorString());
                        return retval;
                    }

                    continue;
                }
                if (pfer.attempts() > 0) {
                    if (!std::all_of(pFlowData->stateData(),
                                     pFlowData->stateData() + pFlowData->size(),
                                     [](double stateValue) { return std::isfinite(stateValue); })) {
                        logging::warning(this, "solver returned an infinite or nan");
                        retval = -30;
                    }
                }
                currentTime = returnTime;
                // pass the solution to the bus objects
                setState(currentTime, pFlowData->stateData(), nullptr, solverModeRef);
                // tell the components to calculate some parameters and power flows
                updateLocalCache();

                voltage_iteration_count++;

                if (voltage_iteration_count > max_Vadjust_iterations) {
                    logging::warning(this, "WARNING::Voltage Loop iteration count limit exceeded");
                    break;
                }

                if (!controlFlags[NO_POWERFLOW_ADJUSTMENTS]) {
                    // check the solution if voltage limits are not ignored

                    if (pState == GridState::INITIALIZED) {
                        if (controlFlags[FIRST_RUN_LIMITS_ONLY]) {
                            AdjustmentChanges =
                                powerFlowAdjust(noInputs, 0, CheckLevel::REVERSABLE_ONLY);
                        } else {
                            AdjustmentChanges = powerFlowAdjust(noInputs,
                                                                lower_flags(controlFlags),
                                                                CheckLevel::REVERSABLE_ONLY);
                        }
                    } else {
                        AdjustmentChanges = powerFlowAdjust(noInputs,
                                                            lower_flags(controlFlags),
                                                            CheckLevel::REVERSABLE_ONLY);
                    }

                    if (AdjustmentChanges > ChangeCode::NON_STATE_CHANGE) {
                        reInitpFlow(solverModeRef, AdjustmentChanges);
                    }
                    if (AdjustmentChanges == ChangeCode::NO_CHANGE) {
                        // if there were no adjustable changes check if there was any non-reversable
                        // changes
                        AdjustmentChanges = powerFlowAdjust(noInputs,
                                                            lower_flags(controlFlags),
                                                            CheckLevel::FULL_CHECK);
                        if (AdjustmentChanges > ChangeCode::NO_CHANGE) {
                            checkNetwork(NetworkCheckType::SIMPLIFIED);
                            if (AdjustmentChanges == ChangeCode::STATE_SIZE_CHANGE) {
                                reInitpFlow(solverModeRef, AdjustmentChanges);
                            }
                        }
                    }
                } else {
                    AdjustmentChanges = ChangeCode::NO_CHANGE;
                }
            } while ((retval < 0) || (AdjustmentChanges != ChangeCode::NO_CHANGE));

            if (controlFlags[POWER_ADJUST_ENABLED]) {
                hasPowerAdjustments = loadBalance(prevPower, slkBusBase);
                if (hasPowerAdjustments) {
                    if (!controlFlags[NO_RESET]) {
                        reset(ResetLevels::MINIMAL);
                    }
                    if (opFlags[STATE_CHANGE_FLAG]) {
                        reInitpFlow(solverModeRef);
                    }
                }
                power_iteration_count++;
                if (power_iteration_count > max_Padjust_iterations) {
                    logging::warning(this,
                                     "WARNING::Power Adjust Loop iteration count limit exceeded");
                    break;
                }
            }
        } while (hasPowerAdjustments);
        // solver stats
        // SGS Kinsol Log file info here sometime Woodard
        if ((consolePrintLevel >= PrintLevel::TRACE) || (logPrintLevel >= PrintLevel::TRACE)) {
            pFlowData->logSolverStats(PrintLevel::TRACE);
        }
    } else {
        setState(currentTime, nullptr, nullptr, solverModeRef);
        updateLocalCache();
    }

    if (pState == GridState::INITIALIZED) {
        if ((controlFlags[SAVE_POWER_FLOW_DATA]) && (!opFlags[POWERFLOW_SAVED])) {
            savePowerFlow(this, powerFlowFile);
            opFlags[POWERFLOW_SAVED] = true;
        }
    }
    // store the results to the buses
    pState = GridState::POWERFLOW_COMPLETE;

    return out;
}

void GridDynSimulation::reInitpFlow(const SolverMode& sMode, ChangeCode change)
{
    if (opFlags[SLACK_BUS_CHANGE]) {
        checkNetwork(NetworkCheckType::FULL);
    } else if (opFlags[CONNECTIVITY_CHANGE_FLAG]) {
        checkNetwork(NetworkCheckType::SIMPLIFIED);
    }
    if (opFlags[RESET_VOLTAGE_FLAG]) {
        reset(ResetLevels::FULL);
        opFlags.reset(RESET_VOLTAGE_FLAG);
    }

    try {
        auto pFlowData = getSolverInterface(sMode);
        if ((opFlags[STATE_CHANGE_FLAG]) || (change == ChangeCode::STATE_SIZE_CHANGE)) {
            updateOffsets(sMode);
            auto ssize = stateSize(sMode);
            pFlowData->allocate(ssize);
            pFlowData->initialize(currentTime);
            pState = GridState::INITIALIZED;
        } else if ((opFlags[OBJECT_CHANGE_FLAG]) || (change == ChangeCode::OBJECT_CHANGE)) {
            if (pState >
                GridState::POWERFLOW_COMPLETE) {  // we have to reset for the dynamic computation
                auto ssize = stateSize(sMode);
                if (ssize != pFlowData->size()) {
                    updateOffsets(sMode);
                    pFlowData->allocate(ssize);
                    pFlowData->initialize(currentTime);
                    pState = GridState::INITIALIZED;
                }
            }
            pFlowData->setMaxNonZeros(jacSize(sMode));
            if (!controlFlags[DENSE_SOLVER]) {
                pFlowData->sparseReInit(SolverInterface::SparseReinitMode::RESIZE);
            }
        } else {
            if (pState >
                GridState::DYNAMIC_INITIALIZED) {  // we have to reset for the dynamic computation
                auto ssize = stateSize(sMode);
                if (ssize != pFlowData->size()) {
                    updateOffsets(sMode);
                    pFlowData->allocate(ssize);
                    pFlowData->initialize(currentTime);
                    pState = GridState::INITIALIZED;
                }
            }
            if ((!controlFlags[DENSE_SOLVER]) && (opFlags[JACOBIAN_COUNT_CHANGE_FLAG])) {
                pFlowData->sparseReInit(SolverInterface::SparseReinitMode::RESIZE);
            }
        }
        opFlags &= RESET_CHANGE_FLAG_MASK;
    }
    catch (const std::bad_alloc&) {
        logging::error(this, "unable to allocate memory");
        pState = GridState::GD_ERROR;
        setErrorCode(-101);
        throw;
    }
    catch (const SolverException& se) {
        logging::error(this, "Initialization error");
        pState = GridState::GD_ERROR;
        setErrorCode(se.code());
        throw;
    }
}

// we initialize all the objects in the simulation and the default SolverInterface
// all other solver data objects would be initialized by a reInitPFlow(xxx) call;
int GridDynSimulation::pFlowInitialize(CoreTime time0)
{
    if (time0 == negTime) {
        time0 = powerFlowStartTime;
        if (time0 == negTime) {
            time0 = startTime - 0.001;
        }
    }
    logging::normal(this, "Initializing Power flow to time {}", static_cast<double>(time0));
    // run any events up to time0
    EvQ->executeEvents(time0 - 0.001);

    auto pFlowData = getSolverInterface(*defPowerFlowMode);
    const SolverMode& solverModeRef = pFlowData->getSolverMode();
    defPowerFlowMode = &solverModeRef;
    // dynInitializeB
    // this->savePowerFlowXML("testflow.xml");
    // check the network to ensure we have a solvable power flow

    busCount = getInt("totalbuscount");
    linkCount = getInt("totallinkcount");
    currentTime = time0;
    pFlowInitializeA(time0, lower_flags(controlFlags));
    auto ssize = stateSize(solverModeRef);
    pFlowData->allocate(ssize);

    // initialization is divided into two parts to account for complex initialization routines that
    // need to communicate so the A phase is start up and the B phase is to set up the results if
    // needed
    int retval = checkNetwork(NetworkCheckType::FULL);
    if (retval != FUNCTION_EXECUTION_SUCCESS) {
        return retval;
    }
    updateOffsets(solverModeRef);
    // Occasionally there is a need to execute some function in between the two phases of setup this
    // section of code calls those customized functions
    for (auto& setupOperation : additionalPowerflowSetupFunctions) {
        if (setupOperation) {
            retval = setupOperation();
            if (retval != 0) {
                return retval;
            }
        }
    }
    additionalPowerflowSetupFunctions.clear();
    pFlowObjectInitializeB();

    if (ssize > 0) {
        pFlowData->initialize(time0);
    }
    currentTime = time0;
    pState = GridState::INITIALIZED;
    return FUNCTION_EXECUTION_SUCCESS;
}

bool GridDynSimulation::generatorAdjust(double adjustment)
{
    double availPower{0.0};
    std::vector<double> avail;
    std::vector<GridBus*> gbusses;
    getBusVector(gbusses);
    avail.reserve(gbusses.size());

    // TODO(phlpt): This makes a really big assumption about the location (in the simulation) of the
    // buses really need to put some thought into working this through the areas
    if (adjustment > 0.0) {
        for (auto& bus : gbusses) {
            if ((bus->isEnabled()) && (bus->getAdjustableCapacityUp() > 0.0)) {
                double maxGen = bus->getMaxGenReal();
                const double participation = bus->get("participation");
                if (maxGen > kBigNum / 2) {
                    maxGen = -(1.0 + participation) * (bus->getGenerationReal());
                }
                avail.push_back(maxGen);
            } else {
                avail.push_back(0.0);
            }
        }
    } else {
        for (auto& bus : gbusses) {
            if ((bus->isEnabled()) && (bus->getAdjustableCapacityDown() > 0.0)) {
                double maxGen = bus->getMaxGenReal();
                const double participation = bus->get("participation");
                if (maxGen > kBigNum / 2) {
                    maxGen = -(1.0 + participation) * (bus->getGenerationReal());
                }
                avail.push_back(maxGen);
            } else {
                avail.push_back(0.0);
            }
        }
    }

    availPower = gmlc::utilities::sum(avail);
    for (size_t kk = 0; kk < gbusses.size(); ++kk) {
        if (avail[kk] > 0.0) {
            gbusses[kk]->generationAdjust(avail[kk] / availPower * adjustment);
        }
    }
    return true;
}

// TODO(PT) this really should be done by areas instead of globally
bool GridDynSimulation::loadBalance(double prevPower, const std::vector<double>& prevSlkGen)
{
    double cPower = 0.0;

    auto prevGeneration = prevSlkGen.begin();
    for (auto& bus : slkBusses) {
        cPower -= (bus->getLinkReal() + bus->getLoadReal());
        // reset the slk generators to previous levels so the adjustments work properly
        bus->set("p", -(*prevGeneration));
        ++prevGeneration;
    }

    cPower = -(cPower - prevPower);

    // printf ("cPower=%f\n", cPower);
    if (std::abs(cPower) < powerAdjustThreshold) {
        // just let the residual error go to the swing bus.
        return false;
    }
    return generatorAdjust(cPower);
}

void GridDynSimulation::continuationPowerFlow(std::string_view contName)
{
    std::shared_ptr<ContinuationSequence> continuation;
    for (auto& clN : continList) {
        if (contName == clN->name) {
            continuation = clN;
        }
    }

    if (!continuation) {
        return;
    }
}

void GridDynSimulation::pFlowSensitivityAnalysis() {}

int GridDynSimulation::eventDrivenPowerflow(CoreTime t_end, CoreTime t_step)
{
    if (t_end == negTime) {
        t_end = stopTime;
    }

    if (t_step == negTime) {
        t_step = stepTime;
    }
    if (!opFlags[DYN_INITIALIZED]) {
        dynInitialize(currentTime);
    }
    auto ret = EvQ->executeEvents(currentTime);
    if (ret != ChangeCode::NO_CHANGE) {
        const int powerflowResult = powerflow();
        if (powerflowResult != FUNCTION_EXECUTION_SUCCESS) {
            return powerflowResult;
        }
    } else if (t_end == currentTime) {
        if (controlFlags[FORCE_EXTRA_POWERFLOW]) {
            const int powerflowResult = powerflow();
            if (powerflowResult != FUNCTION_EXECUTION_SUCCESS) {
                return powerflowResult;
            }
        }
    }
    // setup the periodic empty event in the queue
    EvQ->nullEventTime(getSimulationTime() + t_step, t_step);
    auto nextEvent = EvQ->getNextTime();

    while (nextEvent <= t_end) {
        bool powerflow_executed = false;
        currentTime = nextEvent;
        // advance the time
        GridArea::timestep(currentTime, noInputs, *defPowerFlowMode);
        // execute any events
        ret = EvQ->executeEventsAonly(currentTime);
        // run the power flow
        if ((ret >= ChangeCode::PARAMETER_CHANGE) || (controlFlags[FORCE_POWER_FLOW]) ||
            (EvQ->getNullEventTime() >= getSimulationTime() + t_step)) {
            const int powerflowResult = powerflow();
            powerflow_executed = true;
            if (powerflowResult != FUNCTION_EXECUTION_SUCCESS) {
                return powerflowResult;
            }
        }

        // execute delayed events (typically recorders
        ret = EvQ->executeEventsBonly();
        // if something changed rerun the power flow to get a good solution
        // NOTE this would be an atypical situation to have to rerun this
        if (ret >= ChangeCode::PARAMETER_CHANGE) {
            const int powerflowResult = powerflow();
            if (powerflowResult != FUNCTION_EXECUTION_SUCCESS) {
                return powerflowResult;
            }
        }
        nextEvent = EvQ->getNextTime();
        if (powerflow_executed) {
            if (nextEvent < getSimulationTime() +
                    t_step)  // only run the empty event if there is nothing in between
            {
                EvQ->nullEventTime(nextEvent + t_step);
            }
        }
    }
    if (getSimulationTime() != t_end) {
        GridArea::timestep(t_end, noInputs, *defPowerFlowMode);
        currentTime = t_end;
        const int powerflowResult = powerflow();
        if (powerflowResult != FUNCTION_EXECUTION_SUCCESS) {
            return powerflowResult;
        }
    }
    return FUNCTION_EXECUTION_SUCCESS;
}

int GridDynSimulation::algUpdateFunction(CoreTime time,
                                         const double state[],
                                         double update[],
                                         const SolverMode& sMode,
                                         double alpha) noexcept
{
    ++evalCount;
    StateData stateDataValue(time, state);
    stateDataValue.seqID = (sMode.approx[FORCE_RECALC] ? 0 : evalCount);

#ifdef CHECK_STATE
    auto dynDataa = getSolverInterface(sMode);
    for (size_t kk = 0; kk < dynDataa->getSize(); ++kk) {
        if (!std::isfinite(state[kk])) {
            logging::error(this, "state[{}] is not finite", kk);
            return FUNCTION_EXECUTION_FAILURE;
        }
    }
#endif

    if ((!(isDAE(sMode))) && (isDynamic(sMode))) {
        stateDataValue.fullState = solverInterfaces[defDAEMode->offsetIndex]->stateData();
    }
    // call the area based function to handle the looping
    preEx(noInputs, stateDataValue, sMode);
    algebraicUpdate(noInputs, stateDataValue, update, sMode, alpha);
    delayedAlgebraicUpdate(noInputs, stateDataValue, update, sMode, alpha);
    return FUNCTION_EXECUTION_SUCCESS;
}
}  // namespace griddyn
