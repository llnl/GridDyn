/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "PowerFlowErrorRecovery.h"

#include "../GridDynSimulation.h"
#include "../solvers/SolverInterface.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace griddyn {
powerFlowErrorRecovery::powerFlowErrorRecovery(GridDynSimulation* gds,
                                               std::shared_ptr<SolverInterface> solverData):
    sim(gds), solver(std::move(solverData))
{
}
powerFlowErrorRecovery::~powerFlowErrorRecovery() = default;

powerFlowErrorRecovery::RecoveryReturnCodes powerFlowErrorRecovery::attemptFix(int error_code)
{
    if ((error_code == -11) &&
        (sim->currentProcessState() !=
         GridDynSimulation::GridState::INITIALIZED)) {  // something possibly went wrong in the
                                                        // initial setup try a full
                                                        // reinitialization
        sim->reInitpFlow(solver->getSolverMode(), ChangeCode::state_count_change);
        return (attempt_number > 3) ? RecoveryReturnCodes::OUT_OF_OPTIONS :
                                      RecoveryReturnCodes::MORE_OPTIONS;
    }
    if (error_code == SOLVER_INVALID_STATE_ERROR) {
        lowVoltageFix();
    }
    while (attempt_number < 5) {
        ++attempt_number;
        switch (attempt_number) {
            case 1:
                if (powerFlowFix1()) {
                    return RecoveryReturnCodes::MORE_OPTIONS;
                }
                break;
            case 2:
                if (powerFlowFix2()) {
                    return RecoveryReturnCodes::MORE_OPTIONS;
                }
                break;
            case 3:
                if (powerFlowFix3()) {
                    return RecoveryReturnCodes::MORE_OPTIONS;
                }
                break;
            case 4:
                if (powerFlowFix4()) {
                    return RecoveryReturnCodes::MORE_OPTIONS;
                }
                break;
            case 5:
                powerFlowFix5();
                return RecoveryReturnCodes::MORE_OPTIONS;
            default:
                break;
        }
    }
    return RecoveryReturnCodes::OUT_OF_OPTIONS;
}

void powerFlowErrorRecovery::reset()
{
    attempt_number = 0;
}

void powerFlowErrorRecovery::updateInfo(std::shared_ptr<SolverInterface> solverData)
{
    solver = std::move(solverData);
}
int powerFlowErrorRecovery::attempts() const
{
    return attempt_number;
}
// Try any non-reversible adjustments which might be out there
bool powerFlowErrorRecovery::powerFlowFix1()
{
    if (!sim->opFlags[has_powerflow_adjustments]) {
        return false;
    }
    sim->updateLocalCache();
    const ChangeCode eval =
        sim->powerFlowAdjust(noInputs, lower_flags(sim->controlFlags), CheckLevel::full_check);
    if (eval > ChangeCode::non_state_change) {
        sim->checkNetwork(GridDynSimulation::NetworkCheckType::SIMPLIFIED);
        sim->reInitpFlow(solver->getSolverMode(), eval);
        return true;
    }
    return false;
}

// try a few rounds of Gauss Seidel like convergence
bool powerFlowErrorRecovery::powerFlowFix2()
{
    sim->guessState(sim->getSimulationTime(),
                    solver->state_data(),
                    nullptr,
                    solver->getSolverMode());
    sim->converge(sim->getSimulationTime(),
                  solver->state_data(),
                  nullptr,
                  solver->getSolverMode(),
                  ConvergeMode::block_iteration,
                  0.1);
    sim->setState(sim->getSimulationTime(), solver->state_data(), nullptr, solver->getSolverMode());
    if (sim->opFlags[has_powerflow_adjustments]) {
        sim->updateLocalCache();
        const ChangeCode eval = sim->powerFlowAdjust(noInputs,
                                                     lower_flags(sim->controlFlags),
                                                     CheckLevel::reversable_only);
        sim->reInitpFlow(solver->getSolverMode(), eval);
    }
    return true;
}

// check for some low voltage conditions and change the low voltage load conditions
bool powerFlowErrorRecovery::powerFlowFix3()
{
    std::vector<double> voltages;
    sim->getVoltage(voltages);
    if (std::any_of(voltages.begin(), voltages.end(), [](double voltageValue) {
            return (voltageValue < 0.7);
        })) {
        sim->guessState(sim->getSimulationTime(),
                        solver->state_data(),
                        nullptr,
                        solver->getSolverMode());
        sim->converge(sim->getSimulationTime(),
                      solver->state_data(),
                      nullptr,
                      solver->getSolverMode(),
                      ConvergeMode::single_iteration,
                      0);
        sim->setState(sim->getSimulationTime(),
                      solver->state_data(),
                      nullptr,
                      solver->getSolverMode());
        if (!sim->opFlags[prev_setall_pqvlimit]) {
            sim->setAll("load", "pqlowvlimit", 1.0);
            sim->controlFlags.set(voltage_constraints_flag);
            sim->opFlags.set(prev_setall_pqvlimit);
            sim->log(sim, PrintLevel::DEBUG, "pq adjust on load");
        }
        sim->updateLocalCache();
        sim->powerFlowAdjust(noInputs, lower_flags(sim->controlFlags), CheckLevel::reversable_only);
        sim->reInitpFlow(solver->getSolverMode(), ChangeCode::state_count_change);
        sim->guessState(sim->getSimulationTime(),
                        solver->state_data(),
                        nullptr,
                        solver->getSolverMode());
        sim->converge(sim->getSimulationTime(),
                      solver->state_data(),
                      nullptr,
                      solver->getSolverMode(),
                      ConvergeMode::block_iteration,
                      0.1);
        sim->setState(sim->getSimulationTime(),
                      solver->state_data(),
                      nullptr,
                      solver->getSolverMode());
        sim->updateLocalCache();
        ChangeCode adjustmentEval = sim->powerFlowAdjust(noInputs,
                                                         lower_flags(sim->controlFlags),
                                                         CheckLevel::reversable_only);
        while (adjustmentEval > ChangeCode::no_change) {
            sim->reInitpFlow(solver->getSolverMode(), adjustmentEval);
            sim->guessState(sim->getSimulationTime(),
                            solver->state_data(),
                            nullptr,
                            solver->getSolverMode());
            sim->converge(sim->getSimulationTime(),
                          solver->state_data(),
                          nullptr,
                          solver->getSolverMode(),
                          ConvergeMode::single_iteration,
                          0);
            sim->setState(sim->getSimulationTime(),
                          solver->state_data(),
                          nullptr,
                          solver->getSolverMode());
            sim->updateLocalCache();
            adjustmentEval = sim->powerFlowAdjust(noInputs,
                                                  lower_flags(sim->controlFlags),
                                                  CheckLevel::reversable_only);
        }
        return true;
    }
    return false;
}

// Try to disconnect very low voltage buses
bool powerFlowErrorRecovery::powerFlowFix4()
{
    std::vector<double> voltages;
    sim->getVoltage(voltages);
    if (std::any_of(voltages.begin(), voltages.end(), [](double voltageValue) {
            return (voltageValue < 0.1);
        })) {
        sim->setAll("bus", "lowvdisconnect", 0.03);
        sim->reInitpFlow(solver->getSolverMode());
        return true;
    }
    return false;
}

bool powerFlowErrorRecovery::lowVoltageFix()
{
    const ChangeCode eval = sim->powerFlowAdjust(noInputs,
                                                 lower_flags(sim->controlFlags),
                                                 CheckLevel::low_voltage_check);
    if (eval > ChangeCode::no_change) {
        sim->checkNetwork(GridDynSimulation::NetworkCheckType::SIMPLIFIED);
        sim->reInitpFlow(solver->getSolverMode(), eval);
        return true;
    }
    return false;
}

// Don't know what to do here yet
bool powerFlowErrorRecovery::powerFlowFix5()
{
    const ChangeCode eval =
        sim->powerFlowAdjust(noInputs, lower_flags(sim->controlFlags), CheckLevel::high_angle_trip);
    if (eval > ChangeCode::no_change) {
        sim->checkNetwork(GridDynSimulation::NetworkCheckType::SIMPLIFIED);
        sim->reInitpFlow(solver->getSolverMode(), eval);
        return true;
    }
    return false;
}
}  // namespace griddyn
