/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterDC2A.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
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
                           const stateData& sD,
                           double resid[],
                           const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    ExciterDC1A::residual(inputs,
                          sD,
                          resid,
                          sMode);  // use DC1A but overwrite if we are at a limiter
    if (opFlags[outside_vlim]) {
        // auto offset = offsets.getAlgOffset(sMode);
        if (opFlags[etrigger_high]) {
            // resid[offset + 1] = state[offset + 1] -
            // inputs[voltageInLocation]*Vrmax;
        } else {
            // resid[offset + 1] = state[offset + 1] -
            // inputs[voltageInLocation]*Vrmin;
        }
    }
}

void ExciterDC2A::derivative(const IOdata& inputs,
                             const stateData& sD,
                             double deriv[],
                             const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    ExciterDC1A::derivative(inputs,
                            sD,
                            deriv,
                            sMode);  // use DC1A but overwrite if we are at a limiter
    if (opFlags[outside_vlim]) {
        auto offset = offsets.getDiffOffset(sMode);
        if (opFlags[etrigger_high]) {
            // deriv[offset + 1] = state[offset + 1] - inputs[voltageInLocation] *
            // Vrmax;
            deriv[offset + 1] = 0;
        } else {
            deriv[offset + 1] = 0;
            // deriv[offset + 1] = state[offset + 1] - inputs[voltageInLocation] *
            // Vrmin;
        }
    }
}

void ExciterDC2A::limitJacobian(double /*V*/,
                                int VLoc,
                                int refLoc,
                                double cj,
                                matrixData<double>& md)
{
    md.assign(refLoc, refLoc, 1);
    if (opFlags[etrigger_high]) {
        // md.assign(refLoc, VLoc, -Vrmax);
        md.assign(refLoc, VLoc, cj);
    } else {
        // md.assign(refLoc, VLoc, -Vrmin);
        md.assign(refLoc, VLoc, cj);
    }
}

void ExciterDC2A::rootTest(const IOdata& inputs,
                           const stateData& sD,
                           double roots[],
                           const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    int rootOffset = offsets.getRootOffset(sMode);
    const double* es = sD.state + offset;
    double V = inputs[voltageInLocation];
    if (opFlags[outside_vlim]) {
        roots[rootOffset] = ((Vref - V) - es[0] * Kf / Tf + es[3]) * Ka * Tc / Tb +
            es[2] * (Tb - Tc) * Ka / Tb - es[1];
    } else {
        roots[rootOffset] = std::min(Vrmax * V - es[1], es[1] - Vrmin * V) + 0.0001;
        if (es[1] > V * Vrmax) {
            opFlags.set(etrigger_high);
        }
    }
}

change_code ExciterDC2A::rootCheck(const IOdata& inputs,
                                   const stateData& /*sD*/,
                                   const solverMode& /*sMode*/,
                                   check_level_t /*level*/)
{
    double* es = m_state.data();
    double V = inputs[voltageInLocation];
    change_code ret = change_code::no_change;
    if (opFlags[outside_vlim]) {
        double test = ((Vref - V) - es[0] * Kf / Tf + es[3]) * Ka * Tc / Tb +
            es[2] * (Tb - Tc) * Ka / Tb - es[1];
        if (opFlags[etrigger_high]) {
            if (test < 0.0) {
                ret = change_code::jacobian_change;
                opFlags.reset(outside_vlim);
                opFlags.reset(etrigger_high);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > 0.0) {
                ret = change_code::jacobian_change;
                opFlags.reset(outside_vlim);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (es[1] > V * Vrmax + 0.0001) {
            opFlags.set(etrigger_high);
            opFlags.set(outside_vlim);
            es[1] = V * Vrmax;
            ret = change_code::jacobian_change;
            alert(this, JAC_COUNT_DECREASE);
        } else if (es[1] < V * Vrmin - 0.0001) {
            opFlags.reset(etrigger_high);
            opFlags.set(outside_vlim);
            es[1] = V * Vrmin;
            ret = change_code::jacobian_change;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

}  // namespace griddyn::exciters

