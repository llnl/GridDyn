/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterDC2A.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
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
    Aex = 0.0085;  // (3.05,0.279) and (2.29,0.117)
    Bex = 1.1435;
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
                           const stateData& stateDataValue,
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
    if (opFlags[outsideVoltageLimits]) {
        // DC2A keeps the limiter residual clamped once the voltage-dependent limit is active.
    }
}

void ExciterDC2A::derivative(const IOdata& inputs,
                             const stateData& stateDataValue,
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
    if (opFlags[outsideVoltageLimits]) {
        auto offset = offsets.getDiffOffset(sMode);
        deriv[offset + 1] = 0;
    }
}

void ExciterDC2A::limitJacobian(double /*V*/,
                                int voltageLoc,
                                int refLoc,
                                double cjValue,
                                matrixData<double>& matrixDataValue)
{
    matrixDataValue.assign(refLoc, refLoc, 1);
    matrixDataValue.assign(refLoc, voltageLoc, cjValue);
}

void ExciterDC2A::rootTest(const IOdata& inputs,
                           const stateData& stateDataValue,
                           double roots[],
                           const SolverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    const int rootOffset = offsets.getRootOffset(sMode);
    const double* exciterState = stateDataValue.state + offset;
    const double voltage = inputs[voltageInLocation];
    if (opFlags[outsideVoltageLimits]) {
        roots[rootOffset] =
            ((((Vref - voltage) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka * Tc /
             Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) - exciterState[1];
    } else {
        roots[rootOffset] =
            std::min((Vrmax * voltage) - exciterState[1], exciterState[1] - (Vrmin * voltage)) +
            0.0001;
        if (exciterState[1] > (voltage * Vrmax)) {
            opFlags.set(triggerHigh);
        }
    }
}

ChangeCode ExciterDC2A::rootCheck(const IOdata& inputs,
                                  const stateData& /*sD*/,
                                  const SolverMode& /*sMode*/,
                                  CheckLevel /*level*/)
{
    double* exciterState = m_state.data();
    const double voltage = inputs[voltageInLocation];
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[outsideVoltageLimits]) {
        const double test =
            ((((Vref - voltage) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka * Tc /
             Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) - exciterState[1];
        if (opFlags[triggerHigh]) {
            if (test < 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(outsideVoltageLimits);
                opFlags.reset(triggerHigh);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(outsideVoltageLimits);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (exciterState[1] > ((voltage * Vrmax) + 0.0001)) {
            opFlags.set(triggerHigh);
            opFlags.set(outsideVoltageLimits);
            exciterState[1] = voltage * Vrmax;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (exciterState[1] < ((voltage * Vrmin) - 0.0001)) {
            opFlags.reset(triggerHigh);
            opFlags.set(outsideVoltageLimits);
            exciterState[1] = voltage * Vrmin;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

}  // namespace griddyn::exciters
