/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "faultResetRecovery.h"

#include "../gridDynSimulation.h"
#include "../solvers/solverInterface.h"
#include "diagnostics.h"
#include <memory>
#include <utility>
#include <vector>

namespace griddyn {
bool checkResetVoltages(const std::vector<double>& prev, const std::vector<double>& curr);

faultResetRecovery::faultResetRecovery(gridDynSimulation* gds, std::shared_ptr<SolverInterface> sd):
    sim(gds), solver(std::move(sd))
{
    sim->getVoltage(initVolts);
}
faultResetRecovery::~faultResetRecovery() = default;

bool faultResetRecovery::hasMoreFixes() const
{
    return (attempt_number < 7);
}

int faultResetRecovery::attemptFix()
{
    int retval = -101;
    std::vector<double> nVolts;
    while (attempt_number < 7) {
        ++attempt_number;
        switch (attempt_number) {
            case 1:
                retval = faultResetFix1();
                break;
            case 2:
                retval = faultResetFix2(reset_levels::low_voltage_dyn0);
                break;
            case 3:
                retval = faultResetFix2(reset_levels::low_voltage_dyn1);
                break;
            case 4:
                retval = faultResetFix2(reset_levels::low_voltage_dyn2);
                break;
            case 5:
                retval = faultResetFix3();
                break;
            case 6:
                retval = faultResetFix4();
                break;
            default:
                break;
        }
        if (retval == FUNCTION_EXECUTION_SUCCESS) {
            solver->getCurrentData();
            sim->getVoltage(nVolts, solver->state_data(), solver->getSolverMode());
            if (!checkResetVoltages(initVolts, nVolts)) {
                sim->log(sim, print_level::summary, "bad voltage reset");
                retval = -47;
            }
        }
    }
    return retval;
}

void faultResetRecovery::reset()
{
    attempt_number = 0;
}

void faultResetRecovery::updateInfo(std::shared_ptr<SolverInterface> sd)
{
    solver = std::move(sd);
}
int faultResetRecovery::attempts() const
{
    return attempt_number;
}
// Try any non-reversable adjustments which might be out there
int faultResetRecovery::faultResetFix1()
{
    int retval = -101;

    sim->reset(reset_levels::low_voltage_dyn0);

    if ((retval = sim->handleStateChange(solver->getSolverMode())) != HANDLER_NO_RETURN) {
        return retval;
    }
    // auto err = JacobianCheck(sMode, -1, true);
    //      dynamicSolverConvergenceTest(sMode, "convFile.dat",0,3);
    double* states = solver->state_data();
    coreTime timeCurr = sim->getSimulationTime();
    sim->guessState(timeCurr, states, solver->deriv_data(), solver->getSolverMode());
    std::vector<double> vstates(solver->size(), 0);
    sim->getVoltageStates(vstates.data(), solver->getSolverMode());
    for (index_t pp = 0; pp < solver->size(); ++pp) {
        if (vstates[pp] > 0.1) {
            if (states[pp] < 0.9) {
                states[pp] = 1.0;
            }
        }
    }

    return solver->calcIC(timeCurr,
                          sim->probeStepTime,
                          SolverInterface::ic_modes::fixed_diff,
                          true);
}

int faultResetRecovery::faultResetFix2(reset_levels rlevel)
{
    sim->reset(rlevel);
    int retval = -101;
    if ((retval = sim->handleStateChange(solver->getSolverMode())) != HANDLER_NO_RETURN) {
        return retval;
    }
    coreTime timeCurr = sim->getSimulationTime();
    sim->guessState(timeCurr, solver->state_data(), solver->deriv_data(), solver->getSolverMode());
    // int mmatch = JacobianCheck(sim, solver->getSolverMode());

    retval =
        solver->calcIC(timeCurr, sim->probeStepTime, SolverInterface::ic_modes::fixed_diff, true);
    if (retval != 0) {
        // try local converge with mode which only deals with low voltage buses
        sim->converge(timeCurr,
                      solver->state_data(),
                      solver->deriv_data(),
                      solver->getSolverMode(),
                      converge_mode::voltage_only,
                      0.05);
        // std::vector<double> cVolts;
        // sim->getVoltage(cVolts, solver->state_data(), solver->getSolverMode());
        retval = solver->calcIC(timeCurr,
                                sim->probeStepTime,
                                SolverInterface::ic_modes::fixed_diff,
                                true);
    }
    return retval;
}

// check for some low voltage conditions and change the low voltage load conditions
int faultResetRecovery::faultResetFix3()
{
    std::vector<double> vstates(solver->size(), 0);
    std::vector<double> nVolts;
    sim->getVoltageStates(vstates.data(), solver->getSolverMode());
    double* states = solver->state_data();
    int retval = -10;
    for (int vv = 1; vv < 10; ++vv) {
        double rv1 = static_cast<double>(vv) * 0.1;
        int kk = 0;
        sim->log(sim, print_level::debug, "increment voltage by " + std::to_string(rv1));
        for (index_t pp = 0; pp < solver->size(); ++pp) {
            if (vstates[pp] != 0.0) {
                states[pp] = rv1 + (1.0 - rv1) * initVolts[kk];
                ++kk;
            }
        }
        //  dynData->printStates(true);
        retval = solver->calcIC(sim->getSimulationTime(),
                                sim->probeStepTime,
                                SolverInterface::ic_modes::fixed_diff,
                                true);

        if (retval == 0) {
            solver->getCurrentData();
            sim->getVoltage(nVolts, solver->state_data(), solver->getSolverMode());
            if (!checkResetVoltages(initVolts, nVolts)) {
                sim->log(sim, print_level::summary, "bad voltage reset");
                retval = -47;
            }
        } else {
            sim->converge(sim->getSimulationTime(),
                          solver->state_data(),
                          solver->deriv_data(),
                          solver->getSolverMode(),
                          converge_mode::block_iteration,
                          0.1);
            // dynData->printStates(true);
            retval = solver->calcIC(sim->getSimulationTime(),
                                    sim->probeStepTime,
                                    SolverInterface::ic_modes::fixed_diff,
                                    true);
            if (retval == 0) {
                solver->getCurrentData();
                sim->setState(sim->getSimulationTime() + sim->probeStepTime,
                              solver->state_data(),
                              solver->deriv_data(),
                              solver->getSolverMode());
                sim->getVoltage(nVolts);
                if (!checkResetVoltages(initVolts, nVolts)) {
                    sim->log(sim, print_level::normal, "bad voltage reset");
                    retval = -47;
                } else {
                    return 0;
                }
            }
        }
    }
    return retval;
}

// Don't know what to do here yet
int faultResetRecovery::faultResetFix4()
{
    return 0;
}

bool checkResetVoltages(const std::vector<double>& prev, const std::vector<double>& curr)
{
    bool Acond = false;
    int Bcond = 0;
    for (size_t jj = 0; jj < prev.size(); ++jj) {
        if ((prev[jj] < 0.001) && (curr[jj] > 0.1)) {
            Acond = true;
        }
        if ((prev[jj] > 0.1) && (curr[jj] < 0.001)) {
            ++Bcond;
        }
    }
    if ((Acond) && (Bcond > 0)) {
        return false;
    }
    if (Bcond > 1) {
        return false;
    }
    return true;
}

}  // namespace griddyn
