/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dynamicInitialConditionRecovery.h"

#include "../gridDynSimulation.h"
#include "../solvers/solverInterface.h"
#include "diagnostics.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace griddyn {
dynamicInitialConditionRecovery::dynamicInitialConditionRecovery(
    gridDynSimulation* gds,
    std::shared_ptr<SolverInterface> sd): sim(gds), solver(std::move(sd))
{
}
dynamicInitialConditionRecovery::~dynamicInitialConditionRecovery() = default;

bool dynamicInitialConditionRecovery::hasMoreFixes() const
{
    return (attempt_number < 6);
}
int dynamicInitialConditionRecovery::attemptFix()
{
    int retval = -101;
    while (attempt_number < 6) {
        ++attempt_number;
        switch (attempt_number) {
            case 1:
                retval = dynamicFix1();
                break;
            case 2:
                retval = dynamicFix2();
                break;
            case 3:
                retval = dynamicFix3();
                break;
            case 4:
                retval = dynamicFix4();
                break;
            case 5:
                retval = dynamicFix5();
                break;
            default:
                break;
        }
        if (retval == FUNCTION_EXECUTION_SUCCESS) {
            return FUNCTION_EXECUTION_SUCCESS;
        }
        if (retval == SOLVER_INVALID_STATE_ERROR) {
            lowVoltageCheck();
        }
    }
    return retval;
}

void dynamicInitialConditionRecovery::reset()
{
    attempt_number = 0;
}

void dynamicInitialConditionRecovery::updateInfo(std::shared_ptr<SolverInterface> sd)
{
    solver = std::move(sd);
}
int dynamicInitialConditionRecovery::attempts() const
{
    return attempt_number;
}

int dynamicInitialConditionRecovery::lowVoltageCheck()
{
    stateData sD(sim->getSimulationTime(), solver->state_data(), solver->deriv_data());

    sim->rootCheck(noInputs, sD, solver->getSolverMode(), check_level_t::low_voltage_check);

    int mmatch = JacobianCheck(sim, solver->getSolverMode());
    if (mmatch != 0) {
        printStateNames(sim, solver->getSolverMode());
    }
    return solver->calcIC(sim->getSimulationTime(),
                          sim->probeStepTime,
                          SolverInterface::ic_modes::fixed_diff,
                          true);
}

// Try any non-reversible adjustments which might be out there
int dynamicInitialConditionRecovery::dynamicFix1()
{
    sim->checkNetwork(gridDynSimulation::network_check_type::simplified);  // do a network check
    sim->converge(sim->getSimulationTime(),
                  solver->state_data(),
                  solver->deriv_data(),
                  solver->getSolverMode(),
                  converge_mode::block_iteration,
                  3.0);
    return solver->calcIC(sim->getSimulationTime(),
                          sim->probeStepTime,
                          SolverInterface::ic_modes::fixed_diff,
                          true);
}

// try a few rounds of Gauss Seidel like convergence
int dynamicInitialConditionRecovery::dynamicFix2()
{
    sim->converge(sim->getSimulationTime(),
                  solver->state_data(),
                  solver->deriv_data(),
                  solver->getSolverMode(),
                  converge_mode::block_iteration,
                  3.0);
    std::vector<double> v;
    int retval = -10;
    sim->getVoltage(v, solver->state_data(), solver->getSolverMode());
    if (std::any_of(v.begin(), v.end(), [](double a) { return (a < 0.7); })) {
        if (!sim->opFlags[prev_setall_pqvlimit]) {
            sim->log(sim, print_level::debug, "setting all load to PQ at V=0.9");
            sim->opFlags.set(disable_flag_updates);
            sim->setAll("load", "pqlowvlimit", 0.9);
            sim->controlFlags.set(voltage_constraints_flag);
            sim->opFlags.set(prev_setall_pqvlimit);
            sim->opFlags.reset(reset_voltage_flag);
            sim->opFlags.reset(disable_flag_updates);
            sim->updateFlags();
            sim->handleRootChange(solver->getSolverMode(), solver);
            stateData sD(sim->getSimulationTime(), solver->state_data(), solver->deriv_data());

            change_code ret = sim->rootCheck(noInputs,
                                             sD,
                                             solver->getSolverMode(),
                                             check_level_t::complete_state_check);
            sim->handleRootChange(solver->getSolverMode(), solver);
            if (ret > change_code::no_change) {
                if (sim->dynamicCheckAndReset(solver->getSolverMode(), ret)) {
                    retval = solver->calcIC(sim->getSimulationTime(),
                                            sim->probeStepTime,
                                            SolverInterface::ic_modes::fixed_diff,
                                            true);
                }
            } else {
                retval = solver->calcIC(sim->getSimulationTime(),
                                        sim->probeStepTime,
                                        SolverInterface::ic_modes::fixed_diff,
                                        true);
            }
        } else {
            stateData sD(sim->getSimulationTime(), solver->state_data(), solver->deriv_data());
            change_code ret = sim->rootCheck(noInputs,
                                             sD,
                                             solver->getSolverMode(),
                                             check_level_t::reversable_only);
            sim->handleRootChange(solver->getSolverMode(), solver);
            if (ret > change_code::non_state_change) {
                if (sim->dynamicCheckAndReset(solver->getSolverMode(), ret)) {
                    retval = solver->calcIC(sim->getSimulationTime(),
                                            sim->probeStepTime,
                                            SolverInterface::ic_modes::fixed_diff,
                                            true);
                }
            } else {
                sim->guessState(sim->getSimulationTime(),
                                solver->state_data(),
                                solver->deriv_data(),
                                solver->getSolverMode());
                retval = solver->calcIC(sim->getSimulationTime(),
                                        sim->probeStepTime,
                                        SolverInterface::ic_modes::fixed_diff,
                                        true);
            }
        }
    } else {  // well lets just try again for giggles
        sim->converge(sim->getSimulationTime(),
                      solver->state_data(),
                      solver->deriv_data(),
                      solver->getSolverMode(),
                      converge_mode::block_iteration,
                      0.01);
        retval = solver->calcIC(sim->getSimulationTime(),
                                sim->probeStepTime,
                                SolverInterface::ic_modes::fixed_diff,
                                true);
    }
    return retval;
}

// check for some low voltage conditions and change the low voltage load conditions
int dynamicInitialConditionRecovery::dynamicFix3()
{
    coreTime timeCurr = sim->getSimulationTime();
    sim->timestep(timeCurr + 0.001, noInputs, solver->getSolverMode());
    sim->dynamicCheckAndReset(solver->getSolverMode());
    /*if (retval == 4)
    {
            double cr = checkResid(sim, timeCurr + 0.001, solver->getSolverMode());
            sim->log(sim, print_level::debug, "algebraic solver attempted");
            retval = sim->algebraicSolve(timeCurr + 0.001);
            if (retval == 0)
            {
                    sim->guessState(timeCurr + 0.001, solver->state_data(), solver->deriv_data(),
    solver->getSolverMode());
                    double cr2 = checkResid(sim, timeCurr + 0.001, solver->getSolverMode());
                    //LOG_DEBUG("tried alg converge from " + std::to_string(cr2) + " to " +
    std::to_string(cr)); retval = solver->calcIC(timeCurr + 0.001, sim->probeStepTime,
    SolverInterface::ic_modes::fixed_diff, true);
            }
            else
            {
                    sim->guessState(timeCurr + 0.001, solver->state_data(), solver->deriv_data(),
    solver->getSolverMode());
                    retval = solver->calcIC(timeCurr + 0.001, sim->probeStepTime,
    SolverInterface::ic_modes::fixed_diff, true);
            }

    }
    */
    int retval = solver->calcIC(sim->getSimulationTime(),
                                sim->probeStepTime,
                                SolverInterface::ic_modes::fixed_diff,
                                true);
    return retval;
}

// Try to disconnect very low voltage buses
int dynamicInitialConditionRecovery::dynamicFix4()
{
    std::vector<double> v;
    sim->getVoltage(v);
    if (std::any_of(v.begin(), v.end(), [](double a) { return (a < 0.1); })) {
        sim->setAll("bus", "lowvdisconnect", 0.03);
        sim->dynamicCheckAndReset(solver->getSolverMode());
    }
    sim->converge(sim->getSimulationTime(),
                  solver->state_data(),
                  solver->deriv_data(),
                  solver->getSolverMode(),
                  converge_mode::block_iteration,
                  0.01);
    int retval = solver->calcIC(sim->getSimulationTime(),
                                sim->probeStepTime,
                                SolverInterface::ic_modes::fixed_diff,
                                true);
    return retval;
}

// Don't know what to do here yet
int dynamicInitialConditionRecovery::dynamicFix5()
{
    sim->converge(sim->getSimulationTime(),
                  solver->state_data(),
                  solver->deriv_data(),
                  solver->getSolverMode(),
                  converge_mode::block_iteration,
                  0.01);
    int retval = solver->calcIC(sim->getSimulationTime(),
                                sim->probeStepTime,
                                SolverInterface::ic_modes::fixed_diff,
                                true);
    return retval;
}

}  // namespace griddyn
