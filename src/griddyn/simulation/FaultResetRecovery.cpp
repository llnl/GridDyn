/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FaultResetRecovery.h"

#include "../GridDynSimulation.h"
#include "../solvers/SolverInterface.h"
#include "Diagnostics.h"
#include <memory>
#include <utility>
#include <vector>

namespace griddyn {
static bool checkResetVoltages(const std::vector<double>& prev, const std::vector<double>& curr);

faultResetRecovery::faultResetRecovery(GridDynSimulation* gds,
                                       std::shared_ptr<SolverInterface> solverData):
    sim(gds), solver(std::move(solverData))
{
    sim->getVoltage(initVolts);
}
faultResetRecovery::~faultResetRecovery() = default;

bool faultResetRecovery::hasMoreFixes() const
{
    return (attempt_number < 5);
}

int faultResetRecovery::attemptFix()
{
    int retval = -101;
    std::vector<double> nVolts;
    while (hasMoreFixes()) {
        ++attempt_number;
        switch (attempt_number) {
            case 1:
                retval = faultResetFix1();
                break;
            case 2:
                retval = faultResetFix2(ResetLevels::LOW_VOLTAGE_DYN0);
                break;
            case 3:
                retval = faultResetFix2(ResetLevels::LOW_VOLTAGE_DYN1);
                break;
            case 4:
                retval = faultResetFix2(ResetLevels::LOW_VOLTAGE_DYN2);
                break;
            case 5:
                retval = faultResetFix3();
                break;
            default:
                break;
        }
        if (retval == FUNCTION_EXECUTION_SUCCESS) {
            solver->getCurrentData();
            sim->getVoltage(nVolts, solver->stateData(), solver->getSolverMode());
            if (!checkResetVoltages(initVolts, nVolts)) {
                sim->log(sim, PrintLevel::SUMMARY, "bad voltage reset");
                retval = -47;
            } else {
                return FUNCTION_EXECUTION_SUCCESS;
            }
        }
    }
    return retval;
}

void faultResetRecovery::reset()
{
    attempt_number = 0;
}

void faultResetRecovery::updateInfo(std::shared_ptr<SolverInterface> solverData)
{
    solver = std::move(solverData);
}
int faultResetRecovery::attempts() const
{
    return attempt_number;
}
// Try any non-reversable adjustments which might be out there
int faultResetRecovery::faultResetFix1()
{
    int retval = -101;

    sim->reset(ResetLevels::LOW_VOLTAGE_DYN0);

    retval = sim->handleStateChange(solver->getSolverMode());
    if (retval != HANDLER_NO_RETURN) {
        return retval;
    }
    // auto err = jacobianCheck(sMode, -1, true);
    //      dynamicSolverConvergenceTest(sMode, "convFile.dat",0,3);
    double* states = solver->stateData();
    const CoreTime timeCurr = sim->getSimulationTime();
    sim->guessState(timeCurr, states, solver->derivData(), solver->getSolverMode());
    std::vector<double> vstates(solver->size(), 0);
    sim->getVoltageStates(vstates.data(), solver->getSolverMode());
    for (index_t pp = 0; pp < solver->size(); ++pp) {
        if (vstates[pp] > 0.1) {
            if (states[pp] < 0.9) {
                states[pp] = 1.0;
            }
        }
    }

    return solver->calcIC(timeCurr, sim->probeStepTime, SolverInterface::IcModes::FIXED_DIFF, true);
}

int faultResetRecovery::faultResetFix2(ResetLevels rlevel)
{
    sim->reset(rlevel);
    int retval = -101;
    retval = sim->handleStateChange(solver->getSolverMode());
    if (retval != HANDLER_NO_RETURN) {
        return retval;
    }
    const CoreTime timeCurr = sim->getSimulationTime();
    sim->guessState(timeCurr, solver->stateData(), solver->derivData(), solver->getSolverMode());
    // int mmatch = jacobianCheck(sim, solver->getSolverMode());

    retval =
        solver->calcIC(timeCurr, sim->probeStepTime, SolverInterface::IcModes::FIXED_DIFF, true);
    if (retval != 0) {
        // try local converge with mode which only deals with low voltage buses
        sim->converge(timeCurr,
                      solver->stateData(),
                      solver->derivData(),
                      solver->getSolverMode(),
                      ConvergeMode::VOLTAGE_ONLY,
                      0.05);
        // std::vector<double> cVolts;
        // sim->getVoltage(cVolts, solver->stateData(), solver->getSolverMode());
        retval = solver->calcIC(timeCurr,
                                sim->probeStepTime,
                                SolverInterface::IcModes::FIXED_DIFF,
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
    double* states = solver->stateData();
    int retval = -10;
    for (int vv = 1; vv < 10; ++vv) {
        const double voltageIncrement = static_cast<double>(vv) * 0.1;
        int voltageIndex = 0;
        logging::logTo(sim, sim, PrintLevel::DEBUG, "increment voltage by {}", voltageIncrement);
        for (index_t pp = 0; pp < solver->size(); ++pp) {
            if (vstates[pp] != 0.0) {
                states[pp] =
                    voltageIncrement + ((1.0 - voltageIncrement) * initVolts[voltageIndex]);
                ++voltageIndex;
            }
        }
        //  dynData->printStates(true);
        retval = solver->calcIC(sim->getSimulationTime(),
                                sim->probeStepTime,
                                SolverInterface::IcModes::FIXED_DIFF,
                                true);

        if (retval == 0) {
            solver->getCurrentData();
            sim->getVoltage(nVolts, solver->stateData(), solver->getSolverMode());
            if (!checkResetVoltages(initVolts, nVolts)) {
                sim->log(sim, PrintLevel::SUMMARY, "bad voltage reset");
                retval = -47;
            } else {
                return FUNCTION_EXECUTION_SUCCESS;
            }
        } else {
            sim->converge(sim->getSimulationTime(),
                          solver->stateData(),
                          solver->derivData(),
                          solver->getSolverMode(),
                          ConvergeMode::BLOCK_ITERATION,
                          0.1);
            // dynData->printStates(true);
            retval = solver->calcIC(sim->getSimulationTime(),
                                    sim->probeStepTime,
                                    SolverInterface::IcModes::FIXED_DIFF,
                                    true);
            if (retval == 0) {
                solver->getCurrentData();
                sim->setState(sim->getSimulationTime() + sim->probeStepTime,
                              solver->stateData(),
                              solver->derivData(),
                              solver->getSolverMode());
                sim->getVoltage(nVolts);
                if (!checkResetVoltages(initVolts, nVolts)) {
                    sim->log(sim, PrintLevel::NORMAL, "bad voltage reset");
                    retval = -47;
                } else {
                    return 0;
                }
            }
        }
    }
    return retval;
}

static bool checkResetVoltages(const std::vector<double>& prev, const std::vector<double>& curr)
{
    bool acond = false;
    int bcond = 0;
    for (size_t jj = 0; jj < prev.size(); ++jj) {
        if ((prev[jj] < 0.001) && (curr[jj] > 0.1)) {
            acond = true;
        }
        if ((prev[jj] > 0.1) && (curr[jj] < 0.001)) {
            ++bcond;
        }
    }
    if (acond && bcond > 0) {
        return false;
    }
    if (bcond > 1) {
        return false;
    }
    return true;
}

}  // namespace griddyn
