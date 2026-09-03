/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterDC2A.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

// only differences from dc1a are gains and voltage limit is a function of
// terminal voltage

namespace griddyn::exciters {
ExciterDC2A::ExciterDC2A(const std::string& objName): ExciterDC1A(objName)
{
    // default values
    Ka = 300;
    Ta = 0.01;
    Ke = 1.0;
    Te = 1.33;
    Kf = .1;
    Tf = 0.675;
    Tc = 0;
    Tb = 1.0;  // can't be zero
    // PSS/E-form saturation points derived from the historic GridDyn
    // exponential fit's calibration points.
    E1 = 2.29;
    Se1 = 0.117;
    E2 = 3.05;
    Se2 = 0.279;
    Vrmin = -4.9;
    Vrmax = 4.95;
    offsets.local().local.jacSize = 15;
}

// cloning function
CoreObject* ExciterDC2A::clone(CoreObject* obj) const
{
    auto* gdE = cloneBase<ExciterDC2A, ExciterDC1A>(this, obj);
    if (gdE == nullptr) {
        return obj;
    }
    return gdE;
}

// residual
void ExciterDC2A::residual(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double resid[],
                           const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    ExciterDC1A::residual(inputs,
                          stateDataValue,
                          resid,
                          sMode);  // use DC1A but overwrite if we are at a limiter
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        // DC2A keeps the limiter residual clamped once the voltage-dependent limit is active.
    }
}

void ExciterDC2A::derivative(const IOdata& inputs,
                             const StateData& stateDataValue,
                             double deriv[],
                             const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    ExciterDC1A::derivative(inputs,
                            stateDataValue,
                            deriv,
                            sMode);  // use DC1A but overwrite if we are at a limiter
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        auto offset = offsets.getDiffOffset(sMode);
        deriv[offset + 1] = 0;
    }
}

void ExciterDC2A::limitJacobian(double /*V*/,
                                int voltageLoc,
                                int refLoc,
                                double cjValue,
                                MatrixData<double>& matrixDataValue)
{
    matrixDataValue.assign(refLoc, refLoc, 1);
    matrixDataValue.assign(refLoc, voltageLoc, cjValue);
}

void ExciterDC2A::rootTest(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double roots[],
                           const SolverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    const int rootOffset = offsets.getRootOffset(sMode);
    const double* exciterState = stateDataValue.state + offset;
    const double voltage = measuredVoltage(inputs, exciterState);
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        roots[rootOffset] =
            ((((Vref + vBias - voltage) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka *
             Tc / Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) - exciterState[1];
    } else {
        roots[rootOffset] = std::min((regulatorUpperLimit() * voltage) - exciterState[1],
                                     exciterState[1] - (Vrmin * voltage)) +
            0.0001;
        if (exciterState[1] > (voltage * regulatorUpperLimit())) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

ChangeCode ExciterDC2A::rootCheck(const IOdata& inputs,
                                  const StateData& /*sD*/,
                                  const SolverMode& /*sMode*/,
                                  CheckLevel /*level*/)
{
    double* exciterState = m_state.data();
    const double voltage = measuredVoltage(inputs, exciterState);
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double test =
            ((((Vref + vBias - voltage) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka *
             Tc / Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) - exciterState[1];
        if (opFlags[TRIGGER_HIGH]) {
            if (test < 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                opFlags.reset(TRIGGER_HIGH);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (exciterState[1] > ((voltage * regulatorUpperLimit()) + 0.0001)) {
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            exciterState[1] = voltage * regulatorUpperLimit();
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (exciterState[1] < ((voltage * Vrmin) - 0.0001)) {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            exciterState[1] = voltage * Vrmin;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

}  // namespace griddyn::exciters
