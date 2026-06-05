/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Diagnostics.h"

#include "../GridDynSimulation.h"
#include "../solvers/SolverInterface.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/GridRandom.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
std::pair<double, int> checkResid(GridDynSimulation* gds, CoreTime time, const SolverMode& sMode)
{
    return checkResid(gds, time, gds->getSolverInterface(sMode));
}

std::pair<double, int> checkResid(GridDynSimulation* gds,
                                  const std::shared_ptr<SolverInterface>& solverInterface)
{
    return checkResid(gds, gds->getSimulationTime(), solverInterface);
}

std::pair<double, int> checkResid(GridDynSimulation* gds,
                                  CoreTime time,
                                  const std::shared_ptr<SolverInterface>& solverInterface)
{
    const SolverMode& sMode = solverInterface->getSolverMode();
    std::vector<double> resid;
    const double* dstateDt = nullptr;
    auto kSize = solverInterface->size();
    resid.resize(kSize, 0);
    const double* state = solverInterface->stateData();
    assert(kSize == const_cast<const GridDynSimulation*>(gds)->stateSize(sMode));
    if (!isAlgebraicOnly(sMode)) {
        dstateDt = solverInterface->derivData();
    }

    gds->residualFunction(time, state, dstateDt, resid.data(), sMode);
    auto residIterator = resid.begin();
    std::vector<double> signs(kSize, 0);
    if (isDAE(sMode)) {
        signs.assign(kSize, 1);
        gds->getVariableType(signs.data(), sMode);
    }
    const double* signData = signs.data();
    while (residIterator != resid.end()) {
        if (*signData == 1) {
            *residIterator = 0.0;
        }
        ++signData;
        ++residIterator;
    }
    return gmlc::utilities::absMaxLoc(resid);
}

int jacobianCheck(GridDynSimulation* gds,
                  const SolverMode& queryMode,
                  double jacTol,
                  bool useStateNames)
{
    if (isDynamic(queryMode)) {
        if (gds->currentProcessState() < GridDynSimulation::GridState::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < GridDynSimulation::GridState::INITIALIZED) {
        return -1;
    }
    int errors = 0;
    auto solverInterface = gds->getSolverInterface(queryMode);
    const SolverMode& sMode = solverInterface->getSolverMode();
    gds->getSolverReady(solverInterface);
    auto nsize = solverInterface->size();

    if (nsize == 0) {
        return 0;
    }
    double* state = solverInterface->stateData();
    double* dstate = solverInterface->derivData();

    const CoreTime timeCurr = gds->getSimulationTime();
    if ((gds->currentProcessState() <= GridDynSimulation::GridState::DYNAMIC_INITIALIZED) &&
        (timeCurr <= gds->getStartTime())) {
        gds->guessState(timeCurr, state, dstate, solverInterface->getSolverMode());
    }

    std::vector<double> nstate(nsize);
    std::vector<double> ndstate(nsize);
    std::copy(state, state + nsize, nstate.data());
    if (dstate != nullptr) {
        std::copy(dstate, dstate + nsize, ndstate.data());
    }

    MatrixDataSparse<double> tad;
    MatrixDataSparse<double> tad2;
    MatrixDataSparse<double> matrixData;
    tad.reserve(gds->jacSize(sMode));
    matrixData.reserve(gds->jacSize(sMode));
    tad2.reserve(gds->jacSize(sMode));
    const double delta = 1e-8;
    const double delta2 = 1e-10;

    // MatrixDataSparse b2;
    if (jacTol < 0)  // make sure the tolerance is positive
    {
        jacTol = jac_check_tol;
    }

    std::vector<double> resid(nsize);
    std::vector<double> resid2(nsize);
    StateData stateData(timeCurr, nstate.data(), ndstate.data());
    if (sMode.pairedOffsetIndex != kNullLocation) {
        gds->fillExtraStateData(stateData, sMode);
    }
    if (isDifferentialOnly(sMode)) {
        stateData.cj = 0.0;
        gds->derivative(noInputs, stateData, resid.data(), sMode);
        gds->delayedDerivative(noInputs, stateData, resid.data(), sMode);
    } else {
        stateData.cj = 100;
        gds->residual(noInputs, stateData, resid.data(), sMode);
        gds->delayedResidual(noInputs, stateData, resid.data(), sMode);
    }

    gds->jacobianFunction(timeCurr, nstate.data(), ndstate.data(), matrixData, stateData.cj, sMode);

    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }

    for (index_t kk = 0; kk < nsize; ++kk) {
        nstate[kk] += delta;
        if (isDifferentialOnly(sMode)) {
            gds->derivative(noInputs, stateData, resid2.data(), sMode);
            gds->delayedDerivative(noInputs, stateData, resid2.data(), sMode);
        } else {
            gds->residual(noInputs, stateData, resid2.data(), sMode);
            gds->delayedResidual(noInputs, stateData, resid2.data(), sMode);
        }

        // find the changed elements
        for (index_t pp = 0; pp < nsize; ++pp) {
            if (std::abs(resid[pp] - resid2[pp]) > delta * jacTol / 2) {
                tad.assign(pp, kk, (resid2[pp] - resid[pp]) / delta);
            }
        }
        nstate[kk] -= delta;
        nstate[kk] += delta2;
        if (isDifferentialOnly(sMode)) {
            gds->derivative(noInputs, stateData, resid2.data(), sMode);
            gds->delayedDerivative(noInputs, stateData, resid2.data(), sMode);
        } else {
            gds->residual(noInputs, stateData, resid2.data(), sMode);
            gds->delayedResidual(noInputs, stateData, resid2.data(), sMode);
        }

        for (index_t pp = 0; pp < nsize; ++pp) {
            if (std::abs(resid[pp] - resid2[pp]) > delta2 * jacTol / 2) {
                tad2.assign(pp, kk, (resid2[pp] - resid[pp]) / delta2);
            }
        }
        nstate[kk] -= delta2;
        // find the Jacobian elements dependent on the derivatives
        if (isDAE(sMode)) {
            ndstate[kk] += delta;
            gds->residual(noInputs, stateData, resid2.data(), sMode);
            // find the changed elements
            for (index_t pp = 0; pp < nsize; ++pp) {
                if (std::abs(resid[pp] - resid2[pp]) > delta * jacTol / 2) {
                    tad.assign(pp, kk, (resid2[pp] - resid[pp]) / delta * stateData.cj);
                }
            }
            ndstate[kk] -= delta;
            ndstate[kk] += delta2;
            gds->residual(noInputs, stateData, resid2.data(), sMode);
            for (index_t pp = 0; pp < nsize; ++pp) {
                if (std::abs(resid[pp] - resid2[pp]) > delta2 * jacTol / 2) {
                    tad2.assign(pp, kk, (resid2[pp] - resid[pp]) / delta2 * stateData.cj);
                }
            }
            ndstate[kk] -= delta2;
        }
    }

    matrixData.compact();

    tad.compact();

    tad2.compact();

    matrixData.start();
    for (index_t nn = 0; nn < matrixData.size(); ++nn) {
        auto matrixEntry = matrixData.next();
        index_t rowk = matrixEntry.row;
        index_t colk = matrixEntry.col;
        double val1 = matrixEntry.data;
        double val2 = tad.at(rowk, colk);
        double val3 = tad2.at(rowk, colk);

        if ((std::abs(val1 - val2) > jacTol) && (std::abs(val1 - val3) > jacTol) &&
            (std::abs((val1 - val2) / std::max(std::abs(val1), std::abs(val2))) > 2e-4)) {
            // convergence
            if ((((std::abs(val3 - val1) / std::abs(val2 - val1)) > 10.0) &&
                 (std::abs(val1) < jacTol)) ||
                ((std::abs(val3) / std::abs(val2)) > 100.0)) {
                continue;
            }
            if ((std::abs(val3 - val1) / std::abs(val2 - val1)) > 30.0) {
                continue;
            }
            // oscillatory convergence
            if ((val3 / val2 < 0) && (val1 > std::min(val3, val2)) &&
                (val1 < std::max(val3, val2)) && (std::abs(val1) < jacTol)) {
                continue;
            }
            // big number tolerance
            if ((std::abs(val1) > 10) && (std::abs(val2) > 10)) {
                if (std::abs(val2 - val1) < jacTol * val1 / 10.0) {
                    continue;
                }
            }
            ++errors;
            if ((std::abs(val1) > 0.001) || (std::abs(val2) > 0.001)) {
                std::println(
                    "Mismatched Jacobian A [{},{}] jac={:5.4f}, a1={:5.4f} a2={:5.4f} {:4.2f}%",
                    static_cast<unsigned int>(rowk),
                    static_cast<unsigned int>(colk),
                    val1,
                    val2,
                    val3,
                    std::abs((val1 - val2) / val1) * 100);
            } else {
                std::println("Mismatched Jacobian A [{},{}] jac={:6e}, a1={:6e} a2={:6e} {:4.2f}%",
                             static_cast<unsigned int>(rowk),
                             static_cast<unsigned int>(colk),
                             val1,
                             val2,
                             val3,
                             std::abs((val1 - val2) / val1) * 100);
            }
        }
    }

    tad.start();

    for (index_t nn = 0; nn < tad.size(); ++nn) {
        auto matrixEntry = tad.next();
        index_t rowk = matrixEntry.row;
        index_t colk = matrixEntry.col;

        double val1 = matrixData.at(rowk, colk);
        double val2 = matrixEntry.data;
        double val3 = tad2.at(rowk, colk);
        if (val1 != 0) {
            continue;
        }
        if ((std::abs(val1 - val2) > jacTol) && (std::abs(val1 - val3) > jacTol) &&
            (std::abs((val1 - val2) / std::max(std::abs(val1), std::abs(val2))) > 2e-4)) {
            // convergence
            if ((((std::abs(val3) / std::abs(val2)) > 10.0) && (std::abs(val1) < jacTol)) ||
                ((std::abs(val3) / std::abs(val2)) > 100.0)) {
                continue;
            }
            // oscillatory convergence
            if ((val3 / val2 < 0) && (val1 > std::min(val3, val2)) &&
                (val1 < std::max(val3, val2)) && (std::abs(val1) < jacTol)) {
                continue;
            }
            if ((std::abs(val3 - val1) / std::abs(val2 - val1)) > 30.0) {
                continue;
            }
            ++errors;
            std::println("Mismatched Jacobian B [{},{}] jac={}, a1={} a2={:5f} {:4.2f}%",
                         static_cast<unsigned int>(rowk),
                         static_cast<unsigned int>(colk),
                         val1,
                         val2,
                         val3,
                         std::abs((val2 - val1) / val2) * 100);
        }
    }

    return errors;
}

int residualCheck(GridDynSimulation* gds,
                  const SolverMode& sMode,
                  double residTol,
                  bool useStateNames)
{
    return residualCheck(gds, gds->getSimulationTime(), sMode, residTol, useStateNames);
}

int residualCheck(GridDynSimulation* gds,
                  CoreTime time,
                  const SolverMode& sMode,
                  double residTol,
                  bool useStateNames)
{
    if (isDynamic(sMode)) {
        if (gds->currentProcessState() < GridDynSimulation::GridState::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < GridDynSimulation::GridState::INITIALIZED) {
        return -1;
    }
    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }
    int errors = 0;
    auto solverInterface = gds->getSolverInterface(sMode);
    double* state = solverInterface->stateData();
    auto nsize = solverInterface->size();
    assert(nsize == const_cast<const GridDynSimulation*>(gds)->stateSize(sMode));
    if (gds->currentProcessState() == GridDynSimulation::GridState::INITIALIZED) {
        // sMode must be power flow or dc power flow to get here
        gds->guessState(time, state, nullptr, sMode);
    }

    std::vector<double> resid(nsize);
    StateData stateData(time, solverInterface->stateData());
    if (residTol < 0)  // make sure the tolerance is positive
    {
        residTol = resid_check_tol;
    }

    stateData.dstate_dt = (isDAE(sMode)) ? solverInterface->derivData() : nullptr;

    gds->residual(noInputs, stateData, resid.data(), sMode);
    for (index_t kk = 0; kk < nsize; ++kk) {
        if (std::abs(resid[kk]) > residTol) {
            if (useStateNames) {
                std::println("non zero resid[{}]({})={:6e}",
                             static_cast<int>(kk),
                             stv[kk],
                             resid[kk]);
            } else {
                std::println("non-zeros resid[{}]={:6e}", static_cast<int>(kk), resid[kk]);
            }
            ++errors;
        }
    }
    return errors;
}

int algebraicCheck(GridDynSimulation* gds,
                   CoreTime time,
                   const SolverMode& sMode,
                   double algTol,
                   bool useStateNames)
{
    if (isDynamic(sMode)) {
        if (gds->currentProcessState() < GridDynSimulation::GridState::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < GridDynSimulation::GridState::INITIALIZED) {
        return -1;
    }
    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }

    auto solverInterface = gds->getSolverInterface(sMode);
    auto* state = solverInterface->stateData();
    auto nsize = solverInterface->size();
    assert(nsize == const_cast<const GridDynSimulation*>(gds)->stateSize(sMode));
    if (gds->currentProcessState() == GridDynSimulation::GridState::INITIALIZED) {
        // sMode must be power flow or dc power flow to get here
        gds->guessState(time, state, nullptr, sMode);
    } else {
        gds->guessState(time, state, solverInterface->derivData(), sMode);
    }
    std::vector<double> update(nsize);

    if (algTol < 0)  // make sure the tolerance is positive
    {
        algTol = resid_check_tol;
    }
    StateData stateData(time, solverInterface->stateData());
    stateData.dstate_dt = (isDAE(sMode)) ? solverInterface->derivData() : nullptr;

    gds->algebraicUpdate(noInputs, stateData, update.data(), sMode, 1.0);
    std::vector<double> vtype(nsize);

    gds->getVariableType(vtype.data(), sMode);
    int errors = 0;
    for (index_t kk = 0; kk < nsize; ++kk) {
        if (vtype[kk] > 0.01) {
            continue;
        }
        if (std::abs(update[kk] - stateData.state[kk]) > algTol) {
            if (useStateNames) {
                std::println("mismatching updates[{}]({})={:6e} vs {:6e}",
                             static_cast<int>(kk),
                             stv[kk],
                             update[kk],
                             stateData.state[kk]);
            } else {
                std::println("mismatching updates[{}]={:6e} vs {:6e}",
                             static_cast<int>(kk),
                             update[kk],
                             stateData.state[kk]);
            }
            ++errors;
        }
    }
    return errors;
}

int derivativeCheck(GridDynSimulation* gds,
                    CoreTime time,
                    const SolverMode& sMode,
                    double derivTol,
                    bool useStateNames)
{
    if (hasDifferential(sMode)) {
        if (gds->currentProcessState() < GridDynSimulation::GridState::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < GridDynSimulation::GridState::INITIALIZED) {
        return -1;
    }
    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }
    int errors = 0;
    auto solverInterface = gds->getSolverInterface(sMode);
    double* state = solverInterface->stateData();
    auto nsize = solverInterface->size();
    assert(nsize == const_cast<const GridDynSimulation*>(gds)->stateSize(sMode));
    if (gds->currentProcessState() == GridDynSimulation::GridState::INITIALIZED) {
        // sMode must be power flow or dc power flow to get here
        gds->guessState(time, state, nullptr, sMode);
    } else {
        gds->guessState(time, state, solverInterface->derivData(), sMode);
    }
    std::vector<double> deriv(nsize);

    if (derivTol < 0.0)  // make sure the tolerance is positive
    {
        derivTol = resid_check_tol;
    }
    const StateData stateData(time, solverInterface->stateData(), solverInterface->derivData());

    gds->derivative(noInputs, stateData, deriv.data(), sMode);
    std::vector<double> vtype(nsize);

    gds->getVariableType(vtype.data(), sMode);
    for (index_t kk = 0; kk < nsize; ++kk) {
        if (vtype[kk] < 0.1) {
            continue;
        }
        if (std::abs(deriv[kk] - stateData.dstate_dt[kk]) > derivTol) {
            if (useStateNames) {
                std::println("mismatching derivative[{}]({})={:6e}",
                             static_cast<int>(kk),
                             stv[kk],
                             deriv[kk]);
            } else {
                std::println("mismatching derivative[{}]={:6e}", static_cast<int>(kk), deriv[kk]);
            }
            ++errors;
        }
    }
    return errors;
}

void dynamicSolverConvergenceTest(GridDynSimulation* gds,
                                  const SolverMode& sMode,
                                  const std::string& file,
                                  count_t pts,
                                  int mode)
{
    auto solverInterface = gds->getSolverInterface(sMode);
    auto ssize = solverInterface->size();

    double* state = solverInterface->stateData();
    std::ofstream bFile(file.c_str(), std::ios::out | std::ios::binary);

    std::vector<double> baseState(ssize, 0);

    std::vector<double> tempState(baseState);

    std::copy(state, state + ssize, baseState.begin());
    std::vector<double> vStates(ssize, 0);
    gds->getVoltageStates(vStates.data(), sMode);
    bFile.write(reinterpret_cast<char*>(&ssize), sizeof(int));
    double inc = 1.51 / static_cast<double>(pts);
    double limitVal = 1.51;
    bFile.write(reinterpret_cast<char*>(&inc), sizeof(double));
    bFile.write(reinterpret_cast<char*>(&limitVal), sizeof(double));
    auto vsi = gmlc::utilities::vecFindgt(vStates, 0.5);
    auto lstate = vsi.back();
    size_t cvs = vsi.size();
    bFile.write(reinterpret_cast<char*>(&cvs), sizeof(size_t));

    auto tempLevel = solverInterface->get("printlevel");
    solverInterface->set("printLevel", "error");

    switch (mode) {
        case 0:  // sequential all points
        default:
            for (auto& voltageStateIndex : vsi) {
                state[voltageStateIndex] = 1e-12;
            }

            while (state[lstate] < limitVal) {
                bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                std::copy(state, state + ssize, tempState.begin());
                const int retval = solverInterface->calcIC(gds->getSimulationTime(),
                                                           0.001,
                                                           SolverInterface::IcModes::FIXED_DIFF,
                                                           true);
                if (retval < 0) {
                    double rval2 = retval;
                    bFile.write(reinterpret_cast<char*>(&rval2), sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                } else {
                    solverInterface->getCurrentData();
                    bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                }

                state[vsi[0]] += inc;
                int ctr = 0;
                while (state[vsi[ctr]] > limitVal) {
                    state[vsi[ctr]] = 1e-12;

                    ++ctr;
                    state[vsi[ctr]] += inc;
                    std::println("inc {}-{}", ctr, state[vsi[ctr]]);
                    if (ctr == static_cast<int>(cvs) - 1) {
                        break;
                    }
                }
            }
            break;
        case 1:  // random points
        {
            utilities::GridRandom rng(utilities::GridRandom::DistributionType::UNIFORM, 0.0, 1.51);
            std::vector<double> rvals(cvs);
            for (index_t kk = 0; kk < pts; ++kk) {
                rng.getNewValues(rvals, static_cast<count_t>(cvs));
                for (size_t jj = 0; jj < cvs; ++jj) {
                    state[vsi[jj]] = rvals[jj];
                }
                bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                std::copy(state, state + ssize, tempState.begin());
                const int retval = solverInterface->calcIC(gds->getSimulationTime(),
                                                           0.001,
                                                           SolverInterface::IcModes::FIXED_DIFF,
                                                           true);
                if (retval < 0) {
                    double rval2 = retval;
                    bFile.write(reinterpret_cast<char*>(&rval2), sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                } else {
                    solverInterface->getCurrentData();
                    bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                }
            }
        } break;
        case 2:  // all the same sequence of points
        {
            double val = 1e-12;
            while (val < 1.51) {
                for (auto& voltageStateIndex : vsi) {
                    state[voltageStateIndex] = val;
                }
                bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                std::copy(state, state + ssize, tempState.begin());
                const int retval = solverInterface->calcIC(gds->getSimulationTime(),
                                                           0.001,
                                                           SolverInterface::IcModes::FIXED_DIFF,
                                                           true);
                if (retval < 0) {
                    double rval2 = retval;
                    bFile.write(reinterpret_cast<char*>(&rval2), sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                } else {
                    solverInterface->getCurrentData();
                    bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                }
                val += inc;
            }
        } break;
        case 3:  // specific points
        {
            std::vector<std::vector<double>> ptsv{{1, 1, 1}, {0.5, 0.5, 0.5}};

            for (auto& pointSet : ptsv) {
                for (size_t mm = 0; ((mm < pointSet.size()) && (mm < cvs)); ++mm) {
                    state[vsi[mm]] = pointSet[mm];
                }
                std::copy(state, state + ssize, tempState.begin());
                solverInterface->calcIC(gds->getSimulationTime(),
                                        0.001,
                                        SolverInterface::IcModes::FIXED_DIFF,
                                        true);
                std::copy(tempState.begin(), tempState.begin() + ssize, state);
            }
        } break;
    }

    solverInterface->set("printLevel", tempLevel);
    std::copy(baseState.begin(), baseState.begin() + ssize, state);
}

namespace {

    std::vector<int> getRowCounts(MatrixData<double>& matrixData)
    {
        std::vector<int> rowCounts(matrixData.rowLimit());
        const auto matrixSize = static_cast<int>(matrixData.size());
        matrixData.start();
        int entryIndex = 0;
        while (entryIndex < matrixSize) {
            auto matrixEntry = matrixData.next();
            ++rowCounts[matrixEntry.row];
            ++entryIndex;
        }
        return rowCounts;
    }

    std::vector<index_t> getLocalStates(const GridComponent* comp, const SolverMode& sMode)
    {
        std::vector<index_t> localStates;
        const auto& offsets = comp->getOffsets(sMode);
        localStates.reserve(static_cast<size_t>(offsets.local.algSize) + offsets.local.diffSize +
                            offsets.local.vSize + offsets.local.aSize);
        for (index_t stateIndex = 0; stateIndex < offsets.local.algSize; ++stateIndex) {
            localStates.push_back(offsets.algOffset + stateIndex);
        }
        for (index_t stateIndex = 0; stateIndex < offsets.local.diffSize; ++stateIndex) {
            localStates.push_back(offsets.diffOffset + stateIndex);
        }
        for (index_t stateIndex = 0; stateIndex < offsets.local.vSize; ++stateIndex) {
            localStates.push_back(offsets.vOffset + stateIndex);
        }
        for (index_t stateIndex = 0; stateIndex < offsets.local.aSize; ++stateIndex) {
            localStates.push_back(offsets.aOffset + stateIndex);
        }
        return localStates;
    }

    // helper class for aggregating information
    class ObjectCountInfo {
      public:
        std::string mName;
        count_t mTotalStates = 0;
        count_t mLocalStates = 0;
        count_t mLocalJacListed = 0;
        count_t mTotalJacListed = 0;
        count_t mLocalJacActual = 0;
        count_t mTotalJacActual = 0;
        std::vector<ObjectCountInfo> mSubObjectInfo;
    };
    /** function to get the actual Jacobian information about an object*/
    ObjectCountInfo getObjectInformation(const GridComponent* comp,
                                         const SolverMode& sMode,
                                         const std::vector<int>& rowCount)
    {
        struct TraversalFrame {
            const GridComponent* mComponent;
            ObjectCountInfo* mObjectInfo;
            bool mChildrenQueued;
        };

        ObjectCountInfo rootObjectInfo;
        std::vector<TraversalFrame> traversalStack;
        traversalStack.push_back(
            {.mComponent = comp, .mObjectInfo = &rootObjectInfo, .mChildrenQueued = false});

        while (!traversalStack.empty()) {
            auto currentFrame = traversalStack.back();
            traversalStack.pop_back();

            if (!currentFrame.mChildrenQueued) {
                currentFrame.mObjectInfo->mName = currentFrame.mComponent->getName();
                currentFrame.mObjectInfo->mTotalStates = currentFrame.mComponent->stateSize(sMode);

                const auto localStates = getLocalStates(currentFrame.mComponent, sMode);
                currentFrame.mObjectInfo->mLocalStates = static_cast<count_t>(localStates.size());
                currentFrame.mObjectInfo->mTotalJacListed = currentFrame.mComponent->jacSize(sMode);
                for (const auto& stateIndex : localStates) {
                    currentFrame.mObjectInfo->mLocalJacActual += rowCount[stateIndex];
                }

                std::vector<const GridComponent*> childComponents;
                int subObjectIndex = 0;
                auto* subObject = dynamic_cast<GridComponent*>(
                    currentFrame.mComponent->getSubObject("subobject", subObjectIndex));
                while (subObject != nullptr) {
                    childComponents.push_back(subObject);
                    ++subObjectIndex;
                    subObject = dynamic_cast<GridComponent*>(
                        currentFrame.mComponent->getSubObject("subobject", subObjectIndex));
                }

                currentFrame.mObjectInfo->mSubObjectInfo.resize(childComponents.size());
                traversalStack.push_back({.mComponent = currentFrame.mComponent,
                                          .mObjectInfo = currentFrame.mObjectInfo,
                                          .mChildrenQueued = true});
                for (size_t childCount = childComponents.size(); childCount > 0; --childCount) {
                    const auto childIndex = childCount - 1;
                    traversalStack.push_back(
                        {.mComponent = childComponents[childIndex],
                         .mObjectInfo =
                             &currentFrame.mObjectInfo->mSubObjectInfo[childIndex],
                         .mChildrenQueued = false});
                }
                continue;
            }

            currentFrame.mObjectInfo->mLocalJacListed = currentFrame.mObjectInfo->mTotalJacListed;
            currentFrame.mObjectInfo->mTotalJacActual = currentFrame.mObjectInfo->mLocalJacActual;
            for (const auto& subObjectInfo : currentFrame.mObjectInfo->mSubObjectInfo) {
                currentFrame.mObjectInfo->mLocalJacListed -= subObjectInfo.mTotalJacListed;
                currentFrame.mObjectInfo->mTotalJacActual += subObjectInfo.mTotalJacActual;
            }
        }
        return rootObjectInfo;
    }

    void printObjCountInfo(const ObjectCountInfo& objectInfo, int clevel, int maxLevel)
    {
        std::vector<std::pair<const ObjectCountInfo*, int>> objectStack;
        objectStack.emplace_back(&objectInfo, clevel);

        while (!objectStack.empty()) {
            const auto [currentObjectInfo, currentLevel] = objectStack.back();
            objectStack.pop_back();

            for (int indentLevel = 0; indentLevel < currentLevel; ++indentLevel) {
                std::print("  ");
            }
            std::println("{}:: st {}({}) list {}({}) NNZ {}({})",
                         currentObjectInfo->mName,
                         currentObjectInfo->mTotalStates,
                         currentObjectInfo->mLocalStates,
                         currentObjectInfo->mTotalJacListed,
                         currentObjectInfo->mLocalJacListed,
                         currentObjectInfo->mTotalJacActual,
                         currentObjectInfo->mLocalJacActual);

            if (currentLevel < maxLevel) {
                for (const auto& subObjectInfo :
                     std::views::reverse(currentObjectInfo->mSubObjectInfo)) {
                    objectStack.emplace_back(&subObjectInfo, currentLevel + 1);
                }
            }
        }
    }

    void printStateSizesPretty(const GridComponent* comp, const SolverMode& sMode)
    {
        std::vector<std::pair<const GridComponent*, std::string>> componentStack;
        componentStack.emplace_back(comp, "");
        while (!componentStack.empty()) {
            auto [currentComponent, inset] = std::move(componentStack.back());
            componentStack.pop_back();

            const auto& offsets = currentComponent->getOffsets(sMode);
            std::println("{}{}:: ssize={}, alg={}, diff={}, local={}",
                         inset,
                         currentComponent->getName(),
                         currentComponent->stateSize(sMode),
                         currentComponent->algSize(sMode),
                         currentComponent->diffSize(sMode),
                         offsets.local.totalSize());

            int subObjectIndex = 0;
            std::vector<const GridComponent*> subObjects;
            auto* subObject = dynamic_cast<GridComponent*>(
                currentComponent->getSubObject("subobject", subObjectIndex));
            while (subObject != nullptr) {
                subObjects.push_back(subObject);
                ++subObjectIndex;
                subObject = dynamic_cast<GridComponent*>(
                    currentComponent->getSubObject("subobject", subObjectIndex));
            }

            for (const auto* childComponent : std::views::reverse(subObjects)) {
                componentStack.emplace_back(childComponent, inset + "   ");
            }
        }
    }

}  // namespace

void jacobianAnalysis(MatrixData<double>& matrixData,
                      GridDynSimulation* gds,
                      const SolverMode& sMode,
                      int level)
{
    auto rowCounts = getRowCounts(matrixData);
    auto objectInfo = getObjectInformation(gds, sMode, rowCounts);
    printObjCountInfo(objectInfo, 0, level);
}

bool checkObjectEquivalence(const CoreObject* obj1, const CoreObject* obj2, bool printMessage)
{
    if ((obj1 == nullptr) || (obj2 == nullptr)) {
        if (printMessage) {
            std::println("at least one object is null");
        }
        return false;
    }
    if (typeid(*obj1) != typeid(*obj2)) {
        if (printMessage) {
            std::println("object 1 name ({}) not matching type of object 2({})",
                         obj1->getName(),
                         obj2->getName());
        }
        return false;
    }
    if (obj1->getName() != obj2->getName()) {
        if (printMessage) {
            std::println("object 1 name ({}) not matching object 2({})",
                         obj1->getName(),
                         obj2->getName());
        }
        return false;
    }

    if (obj1->getParent()->getName() !=
        obj2->getParent()->getName()) {  // these do not affect equivalence but should be noted
        if (printMessage) {
            std::println("object 1 ({}) has a different parent than object 2({})",
                         obj1->getName(),
                         obj2->getName());
        }
    }

    if (obj1 == obj2) {  // these do not affect equivalence but should be noted
        if (printMessage) {
            std::println("object 1 and object 2 ({}) have same id", obj1->getName());
        }
        return true;
    }
    if (obj1->get("subobjectcount") != obj2->get("subobjectcount")) {
        if (printMessage) {
            std::println("object 1 ({}) has a different number of subobjects than object 2({})",
                         obj1->getName(),
                         obj2->getName());
        }
        return false;
    }
    bool result = true;
    std::vector<std::pair<const CoreObject*, const CoreObject*>> objectStack;
    objectStack.emplace_back(obj1, obj2);

    while (!objectStack.empty()) {
        const auto [currentObject1, currentObject2] = objectStack.back();
        objectStack.pop_back();

        int subObjectIndex = 0;
        const CoreObject* subObject1 = currentObject1->getSubObject("subobject", subObjectIndex);
        while (subObject1 != nullptr) {
            const CoreObject* subObject2 = currentObject2->find(subObject1->getName());
            if (subObject2 == nullptr) {
                if (printMessage) {
                    std::println("object 2 ({}) does not have a subobject named {}",
                                 currentObject1->getName(),
                                 subObject1->getName());
                }
                result = false;
            } else {
                if (typeid(*subObject1) != typeid(*subObject2)) {
                    if (printMessage) {
                        std::println("object 1 name ({}) not matching type of object 2({})",
                                     subObject1->getName(),
                                     subObject2->getName());
                    }
                    result = false;
                } else if (subObject1->getName() != subObject2->getName()) {
                    if (printMessage) {
                        std::println("object 1 name ({}) not matching object 2({})",
                                     subObject1->getName(),
                                     subObject2->getName());
                    }
                    result = false;
                } else {
                    objectStack.emplace_back(subObject1, subObject2);
                }
            }

            ++subObjectIndex;
            subObject1 = currentObject1->getSubObject("subobject", subObjectIndex);
        }
    }

    return result;
}

void printStateSizes(const GridComponent* comp, const SolverMode& sMode)
{
    printStateSizesPretty(comp, sMode);
}
}  // namespace griddyn
