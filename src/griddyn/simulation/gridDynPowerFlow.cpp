/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../gridBus.h"
#include "../gridDynSimulation.h"
#include "../simulation/diagnostics.h"
#include "../solvers/solverInterface.h"
#include "continuation.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridDynSimulationFileOps.h"
#include "powerFlowErrorRecovery.h"
// system headers
#include <cmath>
#include <cstdio>
#include <iostream>

namespace griddyn {
// --------------- power flow program ---------------

// power flow solver
int gridDynSimulation::powerflow()
{
    const solverMode& sm = *defPowerFlowMode;
    int out = FUNCTION_EXECUTION_SUCCESS;
    count_t voltage_iteration_count = 0;
    count_t power_iteration_count = 0;
    double prevPower = 0;
    int retval = makeReady(gridState_t::INITIALIZED, sm);
    if (retval != FUNCTION_EXECUTION_SUCCESS) {
        LOG_ERROR("Unable to get simulation ready for power flow");
        return retval;
    }
    // load the vectors
    // operate in a loop then check then repeat
    bool hasPowerAdjustments = controlFlags[power_adjust_enabled];

    std::vector<double> slkBusBase(slkBusses.size());

    auto pFlowData = getSolverInterface(sm);
    // Create the error recovery object to use if necessary
    powerFlowErrorRecovery pfer(this, pFlowData);

    if (pFlowData->size() >
        0)  // handle the condition when all buses are swing buses hence nothing to solve
    {
        power_iteration_count = 0;
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
            change_code AdjustmentChanges = change_code::no_change;
            do {
                guessState(currentTime, pFlowData->state_data(), nullptr, sm);
                if ((controlFlags[save_power_flow_input_data] && !controlFlags[power_flow_input_saved]) ) {
                    savePowerFlow(this, powerFlowInputFile);
                    controlFlags[power_flow_input_saved] = true;
                }
                // solve
                coreTime returnTime = currentTime;
                retval = pFlowData->solve(currentTime, returnTime);

                if (retval < 0) {
                    LOG_WARNING("solver error return code:" + std::to_string(retval));

                    if (controlFlags[no_powerflow_error_recovery]) {
                        LOG_ERROR("unable to solve power flow ||" +
                                  pFlowData->getLastErrorString());

                        return retval;
                    }
                    auto prc = pfer.attemptFix(retval);
                    if (prc == powerFlowErrorRecovery::recovery_return_codes::out_of_options) {
                        LOG_ERROR("unable to solve power flow ||" +
                                  pFlowData->getLastErrorString());
                        return retval;
                    }

                    continue;
                }
                if (pfer.attempts() > 0) {
                    if (!std::all_of(pFlowData->state_data(),
                                     pFlowData->state_data() + pFlowData->size(),
                                     [](double a) { return std::isfinite(a); })) {
                        LOG_WARNING("solver returned an infinite or nan");
                        retval = -30;
                    }
                }
                currentTime = returnTime;
                // pass the solution to the bus objects
                setState(currentTime, pFlowData->state_data(), nullptr, sm);
                // tell the components to calculate some parameters and power flows
                updateLocalCache();

                voltage_iteration_count++;

                if (voltage_iteration_count > max_Vadjust_iterations) {
                    LOG_WARNING("WARNING::Voltage Loop iteration count limit exceeded");
                    break;
                }

                if (!controlFlags[no_powerflow_adjustments]) {
                    // check the solution if voltage limits are not ignored

                    if (pState == gridState_t::INITIALIZED) {
                        if (controlFlags[first_run_limits_only]) {
                            AdjustmentChanges =
                                powerFlowAdjust(noInputs, 0, check_level_t::reversable_only);
                        } else {
                            AdjustmentChanges = powerFlowAdjust(noInputs,
                                                                lower_flags(controlFlags),
                                                                check_level_t::reversable_only);
                        }
                    } else {
                        AdjustmentChanges = powerFlowAdjust(noInputs,
                                                            lower_flags(controlFlags),
                                                            check_level_t::reversable_only);
                    }

                    if (AdjustmentChanges > change_code::non_state_change) {
                        reInitpFlow(sm, AdjustmentChanges);
                    }
                    if (AdjustmentChanges == change_code::no_change) {
                        // if there were no adjustable changes check if there was any non-reversable
                        // changes
                        AdjustmentChanges = powerFlowAdjust(noInputs,
                                                            lower_flags(controlFlags),
                                                            check_level_t::full_check);
                        if (AdjustmentChanges > change_code::no_change) {
                            checkNetwork(network_check_type::simplified);
                            if (AdjustmentChanges == change_code::state_count_change) {
                                reInitpFlow(sm, AdjustmentChanges);
                            }
                        }
                    }
                } else {
                    AdjustmentChanges = change_code::no_change;
                }
            } while ((retval < 0) || (AdjustmentChanges != change_code::no_change));

            if (controlFlags[power_adjust_enabled]) {
                hasPowerAdjustments = loadBalance(prevPower, slkBusBase);
                if (hasPowerAdjustments) {
                    if (!controlFlags[no_reset]) {
                        reset(reset_levels::minimal);
                    }
                    if (opFlags[state_change_flag]) {
                        reInitpFlow(sm);
                    }
                }
                power_iteration_count++;
                if (power_iteration_count > max_Padjust_iterations) {
                    LOG_WARNING("WARNING::Power Adjust Loop iteration count limit exceeded");
                    break;
                }
            }
        } while (hasPowerAdjustments);
        // solver stats
        // SGS Kinsol Log file info here sometime Woodard
        if ((consolePrintLevel >= print_level::trace) || (logPrintLevel >= print_level::trace)) {
            pFlowData->logSolverStats(print_level::trace);
        }
    } else {
        setState(currentTime, nullptr, nullptr, sm);
        updateLocalCache();
    }

    if (pState == gridState_t::INITIALIZED) {
        if ((controlFlags[save_power_flow_data]) && (!opFlags[powerflow_saved])) {
            savePowerFlow(this, powerFlowFile);
            opFlags[powerflow_saved] = true;
        }
    }
    // store the results to the buses
    pState = gridState_t::POWERFLOW_COMPLETE;

    return out;
}

void gridDynSimulation::reInitpFlow(const solverMode& sMode, change_code change)
{
    if (opFlags[slack_bus_change]) {
        checkNetwork(network_check_type::full);
    } else if (opFlags[connectivity_change_flag]) {
        checkNetwork(network_check_type::simplified);
    }
    if (opFlags[reset_voltage_flag]) {
        reset(reset_levels::full);
        opFlags.reset(reset_voltage_flag);
    }

    try {
        auto pFlowData = getSolverInterface(sMode);
        if ((opFlags[state_change_flag]) || (change == change_code::state_count_change)) {
            updateOffsets(sMode);
            auto ssize = stateSize(sMode);
            pFlowData->allocate(ssize);
            pFlowData->initialize(currentTime);
            pState = gridState_t::INITIALIZED;
        } else if ((opFlags[object_change_flag]) || (change == change_code::object_change)) {
            if (pState >
                gridState_t::POWERFLOW_COMPLETE) {  // we have to reset for the dynamic computation
                auto ssize = stateSize(sMode);
                if (ssize != pFlowData->size()) {
                    updateOffsets(sMode);
                    pFlowData->allocate(ssize);
                    pFlowData->initialize(currentTime);
                    pState = gridState_t::INITIALIZED;
                }
            }
            pFlowData->setMaxNonZeros(jacSize(sMode));
            if (!controlFlags[dense_solver]) {
                pFlowData->sparseReInit(SolverInterface::sparse_reinit_modes::resize);
            }
        } else {
            if (pState >
                gridState_t::DYNAMIC_INITIALIZED) {  // we have to reset for the dynamic computation
                auto ssize = stateSize(sMode);
                if (ssize != pFlowData->size()) {
                    updateOffsets(sMode);
                    pFlowData->allocate(ssize);
                    pFlowData->initialize(currentTime);
                    pState = gridState_t::INITIALIZED;
                }
            }
            if ((!controlFlags[dense_solver]) && (opFlags[jacobian_count_change_flag])) {
                pFlowData->sparseReInit(SolverInterface::sparse_reinit_modes::resize);
            }
        }
        opFlags &= RESET_CHANGE_FLAG_MASK;
    }
    catch (const std::bad_alloc&) {
        LOG_ERROR("unable to allocate memory");
        pState = gridState_t::GD_ERROR;
        setErrorCode(-101);
        throw;
    }
    catch (const solverException& se) {
        LOG_ERROR("Initialization error");
        pState = gridState_t::GD_ERROR;
        setErrorCode(se.code());
        throw;
    }
}

// we initialize all the objects in the simulation and the default SolverInterface
// all other solver data objects would be initialized by a reInitPFlow(xxx) call;
int gridDynSimulation::pFlowInitialize(coreTime time0)
{
    if (time0 == negTime) {
        time0 = powerFlowStartTime;
        if (time0 == negTime) {
            time0 = startTime - 0.001;
        }
    }
    LOG_NORMAL("Initializing Power flow to time " + std::to_string(time0));
    // run any events up to time0
    EvQ->executeEvents(time0 - 0.001);

    auto pFlowData = getSolverInterface(*defPowerFlowMode);
    const solverMode& sm = pFlowData->getSolverMode();
    defPowerFlowMode = &sm;
    // dynInitializeB
    // this->savePowerFlowXML("testflow.xml");
    // check the network to ensure we have a solvable power flow

    busCount = getInt("totalbuscount");
    linkCount = getInt("totallinkcount");
    currentTime = time0;
    pFlowInitializeA(time0, lower_flags(controlFlags));
    auto ssize = stateSize(sm);
    pFlowData->allocate(ssize);

    // initialization is divided into two parts to account for complex initialization routines that
    // need to communicate so the A phase is start up and the B phase is to set up the results if
    // needed
    int retval = checkNetwork(network_check_type::full);
    if (retval != FUNCTION_EXECUTION_SUCCESS) {
        return retval;
    }
    updateOffsets(sm);
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
    pState = gridState_t::INITIALIZED;
    return FUNCTION_EXECUTION_SUCCESS;
}

// TODO::PT  this really should be done by areas instead of globally
bool gridDynSimulation::loadBalance(double prevPower, const std::vector<double>& prevSlkGen)
{
    double cPower = 0.0;
    double availPower = 0.0;
    std::vector<double> avail;
    std::vector<gridBus*> gbusses;
    getBusVector(gbusses);
    avail.reserve(gbusses.size());
    auto pv = prevSlkGen.begin();
    for (auto& bus : slkBusses) {
        cPower -= (bus->getLinkReal() + bus->getLoadReal());
        bus->set("p",
                 -(*pv));  // reset the slk generators to previous levels so the adjustments work
                           // properly
        ++pv;
    }

    cPower = -(cPower - prevPower);

    // printf ("cPower=%f\n", cPower);
    if (std::abs(cPower) < powerAdjustThreshold) {
        // just let the residual error go to the swing bus.
        return false;
    }
    // TODO:: PT  this makes a really big assumptions about the location (in the simulation) of the
    // buses really need to put some thought into working this through the areas
    if (cPower > 0.0) {
        for (auto& bus : gbusses) {
            if ((bus->isEnabled()) && (bus->getAdjustableCapacityUp() > 0.0)) {
                double maxGen = bus->getMaxGenReal();
                double participation = bus->get("participation");
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
                double participation = bus->get("participation");
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
            gbusses[kk]->generationAdjust(avail[kk] / availPower * cPower);
        }
    }
    return true;
}

void gridDynSimulation::continuationPowerFlow(const std::string& contName)
{
    std::shared_ptr<continuationSequence> cS;
    for (auto& clN : continList) {
        if (contName == clN->name) {
            cS = clN;
        }
    }

    if (!cS) {
        return;
    }
}

void gridDynSimulation::pFlowSensitivityAnalysis() {}

int gridDynSimulation::eventDrivenPowerflow(coreTime t_end, coreTime t_step)
{
    if (t_end == negTime) {
        t_end = stopTime;
    }

    if (t_step == negTime) {
        t_step = stepTime;
    }
    if (!opFlags[dyn_initialized]) {
        dynInitialize(currentTime);
    }
    auto ret = EvQ->executeEvents(currentTime);
    if (ret != change_code::no_change) {
        int pret = powerflow();
        if (pret != FUNCTION_EXECUTION_SUCCESS) {
            return pret;
        }
    } else if (t_end == currentTime) {
        if (controlFlags[force_extra_powerflow]) {
            int pret = powerflow();
            if (pret != FUNCTION_EXECUTION_SUCCESS) {
                return pret;
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
        Area::timestep(currentTime, noInputs, *defPowerFlowMode);
        // execute any events
        ret = EvQ->executeEventsAonly(currentTime);
        // run the power flow
        if ((ret >= change_code::parameter_change) || (controlFlags[force_power_flow]) ||
            (EvQ->getNullEventTime() >= getSimulationTime() + t_step)) {
            int pret = powerflow();
            powerflow_executed = true;
            if (pret != FUNCTION_EXECUTION_SUCCESS) {
                return pret;
            }
        }

        // execute delayed events (typically recorders
        ret = EvQ->executeEventsBonly();
        // if something changed rerun the power flow to get a good solution
        // NOTE this would be an atypical situation to have to rerun this
        if (ret >= change_code::parameter_change) {
            int pret = powerflow();
            if (pret != FUNCTION_EXECUTION_SUCCESS) {
                return pret;
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
        Area::timestep(t_end, noInputs, *defPowerFlowMode);
        currentTime = t_end;
        int pret = powerflow();
        if (pret != FUNCTION_EXECUTION_SUCCESS) {
            return pret;
        }
    }
    return FUNCTION_EXECUTION_SUCCESS;
}

int gridDynSimulation::algUpdateFunction(coreTime time,
                                         const double state[],
                                         double update[],
                                         const solverMode& sMode,
                                         double alpha) noexcept
{
    ++evalCount;
    stateData sD(time, state);
    sD.seqID = (sMode.approx[force_recalc] ? 0 : evalCount);

#ifdef CHECK_STATE
    auto dynDataa = getSolverInterface(sMode);
    for (size_t kk = 0; kk < dynDataa->getSize(); ++kk) {
        if (!std::isfinite(state[kk])) {
            LOG_ERROR("state[" + std::to_string(kk) + "] is not finite");
            return FUNCTION_EXECUTION_FAILURE;
        }
    }
#endif

    if ((!(isDAE(sMode))) && (isDynamic(sMode))) {
        sD.fullState = solverInterfaces[defDAEMode->offsetIndex]->state_data();
    }
    // call the area based function to handle the looping
    preEx(noInputs, sD, sMode);
    algebraicUpdate(noInputs, sD, update, sMode, alpha);
    delayedAlgebraicUpdate(noInputs, sD, update, sMode, alpha);
    return FUNCTION_EXECUTION_SUCCESS;
}
}  // namespace griddyn
