/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorReheat.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::governors {
using units::unit;

GovernorReheat::GovernorReheat(const std::string& objName): Governor(objName)
{
    // IEEE Standard Governor / PSS/E IEESGO defaults.
    K = 16.667;
    T1 = 0.1;
    T2 = 0.0;
    T3 = 0.1;
    // Generator governor-speed inputs are in per unit, unlike the legacy
    // generic Governor default (which stores a rad/s frequency reference).
    Wref = 1.0;
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 5;
    offsets.local().local.jacSize = 18;
    // This realization owns all IEESGO states and does not use Governor's
    // generic filter, deadband, or throttle sub-blocks.
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}

CoreObject* GovernorReheat::clone(CoreObject* obj) const
{
    auto* gov = cloneBase<GovernorReheat, Governor>(this, obj);
    if (gov == nullptr) {
        return obj;
    }

    gov->T4 = T4;
    gov->T5 = T5;
    gov->T6 = T6;
    gov->K2 = K2;
    gov->K3 = K3;

    return gov;
}

// destructor
GovernorReheat::~GovernorReheat() = default;

void GovernorReheat::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(K) || !std::isfinite(T1) || !std::isfinite(T2) || !std::isfinite(T3) ||
        !std::isfinite(T4) || !std::isfinite(T5) || !std::isfinite(T6) || !std::isfinite(K2) ||
        !std::isfinite(K3) || !std::isfinite(Pmax) || !std::isfinite(Pmin) || (K <= 0.0) ||
        (T1 <= 0.0) || (T3 <= 0.0) || (T4 <= 0.0) || (T5 <= 0.0) || (T6 <= 0.0) || (K2 < 0.0) ||
        (K2 > 1.0) || (K3 < 0.0) || (K3 > 1.0) || (Pmax < Pmin)) {
        throw InvalidParameterValue("IEESGO parameters");
    }
    Governor::dynObjectInitializeA(time0, flags);
}

// initial conditions
void GovernorReheat::dynObjectInitializeB(const IOdata& /*inputs*/,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    const double power = std::clamp(desiredOutput[0], Pmin, Pmax);
    m_state[0] = power;
    m_state[1] = power;
    m_state[2] = power;
    m_state[3] = power;
    m_state[4] = K2 * power;
    m_state[5] = K3 * K2 * power;

    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = power;
}

// residual
void GovernorReheat::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto* state = loc.diffStateLoc;
    loc.destLoc[0] =
        loc.algStateLoc[0] - (((1.0 - K2) * state[2]) + ((1.0 - K3) * state[3]) + state[4]);
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t ii = 0; ii < 5; ++ii) {
        loc.destDiffLoc[ii] -= loc.dstateLoc[ii];
    }
}

void GovernorReheat::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto* state = loc.diffStateLoc;
    const double controllerInput =
        inputs[govpSetInLocation] + (K * (Wref - inputs[govOmegaInLocation]));
    loc.destDiffLoc[0] = (controllerInput - state[0]) / T1;
    loc.destDiffLoc[1] = (state[0] - state[1]) / T3;
    const double valve =
        std::clamp(((T2 / T3) * state[0]) + ((1.0 - (T2 / T3)) * state[1]), Pmin, Pmax);
    loc.destDiffLoc[2] = (valve - state[2]) / T4;
    loc.destDiffLoc[3] = ((K2 * state[2]) - state[3]) / T5;
    loc.destDiffLoc[4] = ((K3 * state[3]) - state[4]) / T6;
}

void GovernorReheat::jacobianElements(const IOdata& /*inputs*/,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, nullptr, sMode, this);
    const index_t diff = loc.diffOffset;
    matrixData.assign(loc.algOffset, loc.algOffset, 1.0);
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    matrixData.assign(loc.algOffset, diff + 2, -(1.0 - K2));
    matrixData.assign(loc.algOffset, diff + 3, -(1.0 - K3));
    matrixData.assign(loc.algOffset, diff + 4, -1.0);
    matrixData.assignCheckCol(diff, inputLocs[govpSetInLocation], 1.0 / T1);
    if (inputLocs[govOmegaInLocation] != kNullLocation) {
        matrixData.assign(diff, inputLocs[govOmegaInLocation], -K / T1);
    }
    matrixData.assign(diff, diff, (-1.0 / T1) - stateData.cj);
    matrixData.assign(diff + 1, diff, 1.0 / T3);
    matrixData.assign(diff + 1, diff + 1, (-1.0 / T3) - stateData.cj);
    const auto* state = loc.diffStateLoc;
    const double unlimitedValve = ((T2 / T3) * state[0]) + ((1.0 - (T2 / T3)) * state[1]);
    if ((unlimitedValve > Pmin) && (unlimitedValve < Pmax)) {
        matrixData.assign(diff + 2, diff, T2 / (T3 * T4));
        matrixData.assign(diff + 2, diff + 1, (1.0 - (T2 / T3)) / T4);
    }
    matrixData.assign(diff + 2, diff + 2, (-1.0 / T4) - stateData.cj);
    matrixData.assign(diff + 3, diff + 2, K2 / T5);
    matrixData.assign(diff + 3, diff + 3, (-1.0 / T5) - stateData.cj);
    matrixData.assign(diff + 4, diff + 3, K3 / T6);
    matrixData.assign(diff + 4, diff + 4, (-1.0 / T6) - stateData.cj);
}

index_t GovernorReheat::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode);
    }
    const index_t differentialOffset = offsets.getDiffOffset(sMode);
    if (field == "filter") {
        return differentialOffset;
    }
    if (field == "governor") {
        return differentialOffset + 1;
    }
    if (field == "steamchest") {
        return differentialOffset + 2;
    }
    if (field == "reheater") {
        return differentialOffset + 3;
    }
    if (field == "crossover") {
        return differentialOffset + 4;
    }
    return kInvalidLocation;
}

// set parameters
void GovernorReheat::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorReheat::set(std::string_view param, double val, unit unitType)
{
    if ((param == "t1") || (param == "t2") || (param == "t3") || (param == "t4") ||
        (param == "t5") || (param == "t6") || (param == "k1") || (param == "k2") ||
        (param == "k3") || (param == "pmax") || (param == "pmin")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("IEESGO parameter must be finite");
        }
        if (param == "k1") {
            K = val;
        } else if (param == "k2") {
            K2 = val;
        } else if (param == "k3") {
            K3 = val;
        } else if (param == "t4") {
            T4 = val;
        } else if (param == "t5") {
            T5 = val;
        } else if (param == "t6") {
            T6 = val;
        } else {
            Governor::set(param, val, unitType);
        }
    } else {
        Governor::set(param, val, unitType);
    }
}

double GovernorReheat::get(std::string_view param, unit unitType) const
{
    if (param == "k1") {
        return K;
    }
    if (param == "k2") {
        return K2;
    }
    if (param == "k3") {
        return K3;
    }
    if (param == "t4") {
        return T4;
    }
    if (param == "t5") {
        return T5;
    }
    if (param == "t6") {
        return T6;
    }
    return Governor::get(param, unitType);
}
}  // namespace griddyn::governors
