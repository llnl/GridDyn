/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "diagnostics.h"

#include "../gridDynSimulation.h"
#include "../solvers/solverInterface.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/gridRandom.h"
#include "utilities/matrixDataSparse.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
std::pair<double, int> checkResid(gridDynSimulation* gds, coreTime time, const solverMode& sMode)
{
    return checkResid(gds, time, gds->getSolverInterface(sMode));
}

std::pair<double, int> checkResid(gridDynSimulation* gds,
                                  const std::shared_ptr<SolverInterface>& sd)
{
    return checkResid(gds, gds->getSimulationTime(), sd);
}

std::pair<double, int>
    checkResid(gridDynSimulation* gds, coreTime time, const std::shared_ptr<SolverInterface>& sd)
{
    const solverMode& sMode = sd->getSolverMode();
    std::vector<double> resid;
    double* dstate_dt = nullptr;
    auto kSize = sd->size();
    resid.resize(kSize, 0);
    double* state = sd->state_data();
    assert(kSize == const_cast<const gridDynSimulation*>(gds)->stateSize(sMode));
    if (!isAlgebraicOnly(sMode)) {
        dstate_dt = sd->deriv_data();
    }

    gds->residualFunction(time, state, dstate_dt, resid.data(), sMode);
    auto rb = resid.begin();
    std::vector<double> signs(kSize, 0);
    if (isDAE(sMode)) {
        signs.assign(kSize, 1);
        gds->getVariableType(signs.data(), sMode);
    }
    double* sdata = signs.data();
    while (rb != resid.end()) {
        if (*sdata == 1) {
            *rb = 0.0;
        }
        ++sdata;
        ++rb;
    }
    return gmlc::utilities::absMaxLoc(resid);
}

int JacobianCheck(gridDynSimulation* gds,
                  const solverMode& queryMode,
                  double jacTol,
                  bool useStateNames)
{
    if (isDynamic(queryMode)) {
        if (gds->currentProcessState() < gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < gridDynSimulation::gridState_t::INITIALIZED) {
        return -1;
    }
    int errors = 0;
    auto sd = gds->getSolverInterface(queryMode);
    const solverMode& sMode = sd->getSolverMode();
    gds->getSolverReady(sd);
    auto nsize = sd->size();

    if (nsize == 0) {
        return 0;
    }
    double* state = sd->state_data();
    double* dstate = sd->deriv_data();

    coreTime timeCurr = gds->getSimulationTime();
    if ((gds->currentProcessState() <= gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED) &&
        (timeCurr <= gds->getStartTime())) {
        gds->guessState(timeCurr, state, dstate, sd->getSolverMode());
    }

    std::vector<double> nstate(nsize);
    std::vector<double> ndstate(nsize);
    std::copy(state, state + nsize, nstate.data());
    if (dstate != nullptr) {
        std::copy(dstate, dstate + nsize, ndstate.data());
    }

    matrixDataSparse<double> tad, tad2, md;
    tad.reserve(gds->jacSize(sMode));
    md.reserve(gds->jacSize(sMode));
    tad2.reserve(gds->jacSize(sMode));
    double delta = 1e-8;
    double delta2 = 1e-10;

    // matrixDataSparse b2;
    if (jacTol < 0)  // make sure the tolerance is positive
    {
        jacTol = jac_check_tol;
    }

    std::vector<double> resid(nsize);
    std::vector<double> resid2(nsize);
    stateData sD(timeCurr, nstate.data(), ndstate.data());
    if (sMode.pairedOffsetIndex != kNullLocation) {
        gds->fillExtraStateData(sD, sMode);
    }
    if (isDifferentialOnly(sMode)) {
        sD.cj = 0.0;
        gds->derivative(noInputs, sD, resid.data(), sMode);
        gds->delayedDerivative(noInputs, sD, resid.data(), sMode);
    } else {
        sD.cj = 100;
        gds->residual(noInputs, sD, resid.data(), sMode);
        gds->delayedResidual(noInputs, sD, resid.data(), sMode);
    }

    gds->jacobianFunction(timeCurr, nstate.data(), ndstate.data(), md, sD.cj, sMode);

    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }

    for (index_t kk = 0; kk < nsize; ++kk) {
        nstate[kk] += delta;
        if (isDifferentialOnly(sMode)) {
            gds->derivative(noInputs, sD, resid2.data(), sMode);
            gds->delayedDerivative(noInputs, sD, resid2.data(), sMode);
        } else {
            gds->residual(noInputs, sD, resid2.data(), sMode);
            gds->delayedResidual(noInputs, sD, resid2.data(), sMode);
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
            gds->derivative(noInputs, sD, resid2.data(), sMode);
            gds->delayedDerivative(noInputs, sD, resid2.data(), sMode);
        } else {
            gds->residual(noInputs, sD, resid2.data(), sMode);
            gds->delayedResidual(noInputs, sD, resid2.data(), sMode);
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
            gds->residual(noInputs, sD, resid2.data(), sMode);
            // find the changed elements
            for (index_t pp = 0; pp < nsize; ++pp) {
                if (std::abs(resid[pp] - resid2[pp]) > delta * jacTol / 2) {
                    tad.assign(pp, kk, (resid2[pp] - resid[pp]) / delta * sD.cj);
                }
            }
            ndstate[kk] -= delta;
            ndstate[kk] += delta2;
            gds->residual(noInputs, sD, resid2.data(), sMode);
            for (index_t pp = 0; pp < nsize; ++pp) {
                if (std::abs(resid[pp] - resid2[pp]) > delta2 * jacTol / 2) {
                    tad2.assign(pp, kk, (resid2[pp] - resid[pp]) / delta2 * sD.cj);
                }
            }
            ndstate[kk] -= delta2;
        }
    }

    md.compact();

    tad.compact();

    tad2.compact();

    md.start();
    for (index_t nn = 0; nn < md.size(); ++nn) {
        auto el = md.next();
        index_t rowk = el.row;
        index_t colk = el.col;
        double val1 = el.data;
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
        auto el = tad.next();
        index_t rowk = el.row;
        index_t colk = el.col;

        double val1 = md.at(rowk, colk);
        double val2 = el.data;
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

int residualCheck(gridDynSimulation* gds,
                  const solverMode& sMode,
                  double residTol,
                  bool useStateNames)
{
    return residualCheck(gds, gds->getSimulationTime(), sMode, residTol, useStateNames);
}

int residualCheck(gridDynSimulation* gds,
                  coreTime time,
                  const solverMode& sMode,
                  double residTol,
                  bool useStateNames)
{
    if (isDynamic(sMode)) {
        if (gds->currentProcessState() < gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < gridDynSimulation::gridState_t::INITIALIZED) {
        return -1;
    }
    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }
    int errors = 0;
    auto sd = gds->getSolverInterface(sMode);
    double* state = sd->state_data();
    auto nsize = static_cast<count_t>(sd->size());
    assert(nsize == const_cast<const gridDynSimulation*>(gds)->stateSize(sMode));
    if (gds->currentProcessState() == gridDynSimulation::gridState_t::INITIALIZED) {
        // sMode must be power flow or dc power flow to get here
        gds->guessState(time, state, nullptr, sMode);
    }

    std::vector<double> resid(nsize);
    stateData sD(time, sd->state_data());
    if (residTol < 0)  // make sure the tolerance is positive
    {
        residTol = resid_check_tol;
    }

    sD.dstate_dt = (isDAE(sMode)) ? sd->deriv_data() : nullptr;

    gds->residual(noInputs, sD, resid.data(), sMode);
    for (index_t kk = 0; kk < nsize; ++kk) {
        if (std::abs(resid[kk]) > residTol) {
            if (useStateNames) {
                std::println("non zero resid[{}]({})={:6e}",
                             static_cast<int>(kk),
                             stv[kk].c_str(),
                             resid[kk]);
            } else {
                std::println("non-zeros resid[{}]={:6e}", static_cast<int>(kk), resid[kk]);
            }
            ++errors;
        }
    }
    return errors;
}

int algebraicCheck(gridDynSimulation* gds,
                   coreTime time,
                   const solverMode& sMode,
                   double algTol,
                   bool useStateNames)
{
    if (isDynamic(sMode)) {
        if (gds->currentProcessState() < gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < gridDynSimulation::gridState_t::INITIALIZED) {
        return -1;
    }
    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }

    auto sd = gds->getSolverInterface(sMode);
    auto state = sd->state_data();
    auto nsize = static_cast<count_t>(sd->size());
    assert(nsize == const_cast<const gridDynSimulation*>(gds)->stateSize(sMode));
    if (gds->currentProcessState() == gridDynSimulation::gridState_t::INITIALIZED) {
        // sMode must be power flow or dc power flow to get here
        gds->guessState(time, state, nullptr, sMode);
    } else {
        gds->guessState(time, state, sd->deriv_data(), sMode);
    }
    std::vector<double> update(nsize);

    if (algTol < 0)  // make sure the tolerance is positive
    {
        algTol = resid_check_tol;
    }
    stateData sD(time, sd->state_data());
    sD.dstate_dt = (isDAE(sMode)) ? sd->deriv_data() : nullptr;

    gds->algebraicUpdate(noInputs, sD, update.data(), sMode, 1.0);
    std::vector<double> vtype(nsize);

    gds->getVariableType(vtype.data(), sMode);
    int errors = 0;
    for (index_t kk = 0; kk < nsize; ++kk) {
        if (vtype[kk] > 0.01) {
            continue;
        }
        if (std::abs(update[kk] - sD.state[kk]) > algTol) {
            if (useStateNames) {
                std::println("mismatching updates[{}]({})={:6e} vs {:6e}",
                             static_cast<int>(kk),
                             stv[kk].c_str(),
                             update[kk],
                             sD.state[kk]);
            } else {
                std::println("mismatching updates[{}]={:6e} vs {:6e}",
                             static_cast<int>(kk),
                             update[kk],
                             sD.state[kk]);
            }
            ++errors;
        }
    }
    return errors;
}

int derivativeCheck(gridDynSimulation* gds,
                    coreTime time,
                    const solverMode& sMode,
                    double derivTol,
                    bool useStateNames)
{
    if (hasDifferential(sMode)) {
        if (gds->currentProcessState() < gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED) {
            return -1;
        }
    } else if (gds->currentProcessState() < gridDynSimulation::gridState_t::INITIALIZED) {
        return -1;
    }
    stringVec stv;
    if (useStateNames) {
        gds->getStateName(stv, sMode);
    }
    int errors = 0;
    auto sd = gds->getSolverInterface(sMode);
    double* state = sd->state_data();
    auto nsize = static_cast<count_t>(sd->size());
    assert(nsize == const_cast<const gridDynSimulation*>(gds)->stateSize(sMode));
    if (gds->currentProcessState() == gridDynSimulation::gridState_t::INITIALIZED) {
        // sMode must be power flow or dc power flow to get here
        gds->guessState(time, state, nullptr, sMode);
    } else {
        gds->guessState(time, state, sd->deriv_data(), sMode);
    }
    std::vector<double> deriv(nsize);

    if (derivTol < 0.0)  // make sure the tolerance is positive
    {
        derivTol = resid_check_tol;
    }
    stateData sD(time, sd->state_data(), sd->deriv_data());

    gds->derivative(noInputs, sD, deriv.data(), sMode);
    std::vector<double> vtype(nsize);

    gds->getVariableType(vtype.data(), sMode);
    for (index_t kk = 0; kk < nsize; ++kk) {
        if (vtype[kk] < 0.1) {
            continue;
        }
        if (std::abs(deriv[kk] - sD.dstate_dt[kk]) > derivTol) {
            if (useStateNames) {
                std::println("mismatching derivative[{}]({})={:6e}",
                             static_cast<int>(kk),
                             stv[kk].c_str(),
                             deriv[kk]);
            } else {
                std::println("mismatching derivative[{}]={:6e}", static_cast<int>(kk), deriv[kk]);
            }
            ++errors;
        }
    }
    return errors;
}

void dynamicSolverConvergenceTest(gridDynSimulation* gds,
                                  const solverMode& sMode,
                                  const std::string& file,
                                  count_t pts,
                                  int mode)
{
    auto sd = gds->getSolverInterface(sMode);
    auto ssize = sd->size();

    double* state = sd->state_data();
    std::ofstream bFile(file.c_str(), std::ios::out | std::ios::binary);

    std::vector<double> baseState(ssize, 0);

    std::vector<double> tempState(baseState);

    std::copy(state, state + ssize, baseState.begin());
    std::vector<double> vStates(ssize, 0);
    std::vector<double> nStates(ssize, -kBigNum);
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

    auto tempLevel = sd->get("printlevel");
    sd->set("printLevel", "error");

    switch (mode) {
        case 0:  // sequential all points
        default:
            for (auto& v : vsi) {
                state[v] = 1e-12;
            }

            while (state[lstate] < limitVal) {
                bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                std::copy(state, state + ssize, tempState.begin());
                int retval = sd->calcIC(gds->getSimulationTime(),
                                        0.001,
                                        SolverInterface::ic_modes::fixed_diff,
                                        true);
                if (retval < 0) {
                    double rval2 = retval;
                    bFile.write(reinterpret_cast<char*>(&rval2), sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                } else {
                    sd->getCurrentData();
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
            utilities::gridRandom rng(utilities::gridRandom::dist_type_t::uniform, 0.0, 1.51);
            std::vector<double> rvals(cvs);
            for (index_t kk = 0; kk < pts; ++kk) {
                rng.getNewValues(rvals, static_cast<count_t>(cvs));
                for (size_t jj = 0; jj < cvs; ++jj) {
                    state[vsi[jj]] = rvals[jj];
                }
                bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                std::copy(state, state + ssize, tempState.begin());
                int retval = sd->calcIC(gds->getSimulationTime(),
                                        0.001,
                                        SolverInterface::ic_modes::fixed_diff,
                                        true);
                if (retval < 0) {
                    double rval2 = retval;
                    bFile.write(reinterpret_cast<char*>(&rval2), sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                } else {
                    sd->getCurrentData();
                    bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                }
            }
        } break;
        case 2:  // all the same sequence of points
        {
            double val = 1e-12;
            while (val < 1.51) {
                for (auto& v : vsi) {
                    state[v] = val;
                }
                bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                std::copy(state, state + ssize, tempState.begin());
                int retval = sd->calcIC(gds->getSimulationTime(),
                                        0.001,
                                        SolverInterface::ic_modes::fixed_diff,
                                        true);
                if (retval < 0) {
                    double rval2 = retval;
                    bFile.write(reinterpret_cast<char*>(&rval2), sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                } else {
                    sd->getCurrentData();
                    bFile.write(reinterpret_cast<char*>(state), ssize * sizeof(double));
                    std::copy(tempState.begin(), tempState.begin() + ssize, state);
                }
                val += inc;
            }
        } break;
        case 3:  // specific points
        {
            std::vector<std::vector<double>> ptsv{{1, 1, 1}, {0.5, 0.5, 0.5}};

            for (auto& v : ptsv) {
                for (size_t mm = 0; ((mm < v.size()) && (mm < cvs)); ++mm) {
                    state[vsi[mm]] = v[mm];
                }
                std::copy(state, state + ssize, tempState.begin());
                sd->calcIC(gds->getSimulationTime(),
                           0.001,
                           SolverInterface::ic_modes::fixed_diff,
                           true);
                std::copy(tempState.begin(), tempState.begin() + ssize, state);
            }
        } break;
    }

    sd->set("printLevel", tempLevel);
    std::copy(baseState.begin(), baseState.begin() + ssize, state);
}

std::vector<int> getRowCounts(matrixData<double>& md)
{
    std::vector<int> rowcnt(md.rowLimit());
    auto sz = static_cast<int>(md.size());
    md.start();
    int ii = 0;
    while (ii < sz) {
        auto el = md.next();
        ++rowcnt[el.row];
        ++ii;
    }
    return rowcnt;
}

std::vector<index_t> getLocalStates(const gridComponent* comp, const solverMode& sMode)
{
    std::vector<index_t> st;
    auto& off = comp->getOffsets(sMode);
    st.reserve(static_cast<size_t>(off.local.algSize) + off.local.diffSize + off.local.vSize +
               off.local.aSize);
    for (index_t ii = 0; ii < off.local.algSize; ++ii) {
        st.push_back(off.algOffset + ii);
    }
    for (index_t ii = 0; ii < off.local.diffSize; ++ii) {
        st.push_back(off.diffOffset + ii);
    }
    for (index_t ii = 0; ii < off.local.vSize; ++ii) {
        st.push_back(off.vOffset + ii);
    }
    for (index_t ii = 0; ii < off.local.aSize; ++ii) {
        st.push_back(off.aOffset + ii);
    }
    return st;
}

// helper class for aggregating information
class objectCountInfo {
  public:
    std::string name;
    count_t totalStates = 0;
    count_t localStates = 0;
    count_t localJacListed = 0;
    count_t totalJacListed = 0;
    count_t localJacActual = 0;
    count_t totalJacActual = 0;
    std::vector<objectCountInfo> subObjectInfo;
};
/** function to get the actual Jacobian information about an object*/
objectCountInfo getObjectInformation(const gridComponent* comp,
                                     const solverMode& sMode,
                                     const std::vector<int>& rowCount)
{
    objectCountInfo objI;
    objI.name = comp->getName();
    objI.totalStates = comp->stateSize(sMode);

    auto lcStates = getLocalStates(comp, sMode);
    objI.localStates = static_cast<count_t>(lcStates.size());
    objI.totalJacListed = comp->jacSize(sMode);
    for (auto& st : lcStates) {
        objI.localJacActual += rowCount[st];
    }

    auto subobj = comp->getSubObject("subobject", 0);
    int ii = 0;
    while (subobj != nullptr) {
        objI.subObjectInfo.push_back(
            getObjectInformation(static_cast<gridComponent*>(subobj), sMode, rowCount));
        ++ii;
        subobj = comp->getSubObject("subobject", ii);
    }
    objI.localJacListed = objI.totalJacListed;
    objI.totalJacActual = objI.localJacActual;
    for (auto& sui : objI.subObjectInfo) {
        objI.localJacListed -= sui.totalJacListed;
        objI.totalJacActual += sui.totalJacActual;
    }
    return objI;
}

void printObjCountInfo(const objectCountInfo& oi, int clevel, int maxLevel)
{
    for (int ii = 0; ii < clevel; ++ii) {
        std::print("  ");
    }
    std::println("{}:: st {}({}) list {}({}) NNZ {}({})",
                 oi.name.c_str(),
                 oi.totalStates,
                 oi.localStates,
                 oi.totalJacListed,
                 oi.localJacListed,
                 oi.totalJacActual,
                 oi.localJacActual);
    if (clevel < maxLevel) {
        for (auto& soi : oi.subObjectInfo) {
            printObjCountInfo(soi, clevel + 1, maxLevel);
        }
    }
}

void jacobianAnalysis(matrixData<double>& md,
                      gridDynSimulation* gds,
                      const solverMode& sMode,
                      int level)
{
    auto rc = getRowCounts(md);
    auto oi = getObjectInformation(gds, sMode, rc);
    printObjCountInfo(oi, 0, level);
}

bool checkObjectEquivalence(const coreObject* obj1, const coreObject* obj2, bool printMessage)
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
                         obj1->getName().c_str(),
                         obj2->getName().c_str());
        }
        return false;
    }
    if (obj1->getName() != obj2->getName()) {
        if (printMessage) {
            std::println("object 1 name ({}) not matching object 2({})",
                         obj1->getName().c_str(),
                         obj2->getName().c_str());
        }
        return false;
    }

    if (obj1->getParent()->getName() !=
        obj2->getParent()->getName()) {  // these do not affect equivalence but should be noted
        if (printMessage) {
            std::println("object 1 ({}) has a different parent than object 2({})",
                         obj1->getName().c_str(),
                         obj2->getName().c_str());
        }
    }

    if (obj1 == obj2) {  // these do not affect equivalence but should be noted
        if (printMessage) {
            std::println("object 1 and object 2 ({}) have same id", obj1->getName().c_str());
        }
        return true;
    }
    if (obj1->get("subobjectcount") != obj2->get("subobjectcount")) {
        if (printMessage) {
            std::println("object 1 ({}) has a different number of subobjects than object 2({})",
                         obj1->getName().c_str(),
                         obj2->getName().c_str());
        }
        return false;
    }
    int ii = 0;
    coreObject* sub1 = obj1->getSubObject("subobject", ii);
    bool result = true;
    while (sub1 != nullptr) {
        coreObject* sub2 = obj2->find(sub1->getName());
        if (sub2 == nullptr) {
            if (printMessage) {
                std::println("object 2 ({}) does not have a subobject named {}",
                             obj1->getName().c_str(),
                             sub1->getName().c_str());
            }
            continue;
        }
        auto res = checkObjectEquivalence(sub1, sub2, printMessage);
        ++ii;
        sub1 = obj1->getSubObject("subobject", ii);
        if (!res) {
            result = false;
        }
    }

    return result;
}

void printStateSizesPretty(const gridComponent* obj,
                           const solverMode& sMode,
                           const std::string& inset)
{
    auto& off = obj->getOffsets(sMode);
    std::println("{}{}:: ssize={}, alg={}, diff={}, local={}",
                 inset.c_str(),
                 obj->getName().c_str(),
                 obj->stateSize(sMode),
                 obj->algSize(sMode),
                 obj->diffSize(sMode),
                 off.local.totalSize());
    auto subObj = dynamic_cast<gridComponent*>(obj->getSubObject("subobject", 0));
    int ii = 1;
    while (subObj != nullptr) {
        printStateSizesPretty(subObj, sMode, inset + "   ");
        subObj = dynamic_cast<gridComponent*>(obj->getSubObject("subobject", ii));
        ++ii;
    }
}

void printStateSizes(const gridComponent* comp, const solverMode& sMode)
{
    printStateSizesPretty(comp, sMode, "");
}
}  // namespace griddyn
