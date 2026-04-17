/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterIEEEtype2.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::exciters {
    ExciterIEEEtype2::ExciterIEEEtype2(const std::string& objName): ExciterIEEEtype1(objName) {}

    // cloning function
    coreObject* ExciterIEEEtype2::clone(coreObject* obj) const
    {
        auto* gdE = cloneBase<ExciterIEEEtype2, ExciterIEEEtype1>(this, obj);
        if (gdE == nullptr) {
            return obj;
        }

        gdE->Tf2 = Tf2;
        return gdE;
    }

    void ExciterIEEEtype2::dynObjectInitializeA(coreTime /*time*/, std::uint32_t /*flags*/)
    {
        offsets.local().local.diffSize = 4;
        offsets.local().local.jacSize = 16;
        checkForLimits();
    }

    // initial conditions
    void ExciterIEEEtype2::dynObjectInitializeB(const IOdata& inputs,
                                                const IOdata& desiredOutput,
                                                IOdata& fieldSet)
    {
        Exciter::dynObjectInitializeB(
            inputs,
            desiredOutput,
            fieldSet);  // this will dynInitializeB the field state if need be
        double* gs = m_state.data();
        gs[1] = (Ke + Aex * exp(Bex * gs[0])) * gs[0];  // Vr
        gs[2] = 0;  // X1
        gs[3] = gs[1];  // X2

        vBias = inputs[voltageInLocation] + gs[1] / Ka - Vref;
        fieldSet[exciterVsetInLocation] = Vref;
    }

    // residual
    void ExciterIEEEtype2::residual(const IOdata& inputs,
                                    const stateData& sD,
                                    double resid[],
                                    const solverMode& sMode)
    {
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        auto offset = offsets.getDiffOffset(sMode);
        const double* es = sD.state + offset;
        const double* esp = sD.dstate_dt + offset;
        double* rv = resid + offset;
        rv[0] = (-(Ke + Aex * exp(Bex * es[0])) * es[0] + es[1]) / Te - esp[0];
        if (opFlags[outside_vlim]) {
            if (opFlags[etrigger_high]) {
                rv[1] = esp[1];
            } else {
                rv[1] = esp[1];
            }
        } else {
            rv[1] =
                (-es[1] + Ka * Kf * es[2] + Ka * (Vref + vBias - inputs[voltageInLocation])) / Ta -
                esp[1];
        }
        rv[2] = (-es[2] + es[1] / Tf2 - es[3] / Tf2) / Tf - esp[2];
        rv[3] = (-es[3] + es[1]) / Tf2 - esp[3];
    }

    void ExciterIEEEtype2::derivative(const IOdata& inputs,
                                      const stateData& sD,
                                      double deriv[],
                                      const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, deriv, sMode, this);
        const double* es = Loc.diffStateLoc;
        double* d = Loc.destDiffLoc;
        d[0] = (-(Ke + Aex * exp(Bex * es[0])) * es[0] + es[1]) / Te;
        if (opFlags[outside_vlim]) {
            d[1] = 0;
        } else {
            d[1] =
                (-es[1] + Ka * Kf * es[2] + Ka * (Vref + vBias - inputs[voltageInLocation])) / Ta;
        }
        d[2] = (-es[2] + es[1] / Tf2 - es[3] / Tf2) / Tf;
        d[3] = (-es[3] + es[1]) / Tf2;
    }

    // compute the bus element contributions

    // Jacobian
    void ExciterIEEEtype2::jacobianElements(const IOdata& /*inputs*/,
                                            const stateData& sD,
                                            matrixData<double>& md,
                                            const IOlocs& inputLocs,
                                            const solverMode& sMode)
    {
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        auto offset = offsets.getDiffOffset(sMode);
        int refI = offset;
        double temp1;
        auto VLoc = inputLocs[0];
        // use the md.assign Macro defined in basicDefs
        // md.assign(arrayIndex, RowIndex, ColIndex, value)

        // Ef
        temp1 =
            -(Ke + Aex * exp(Bex * sD.state[offset]) * (1.0 + Bex * sD.state[offset])) / Te - sD.cj;
        md.assign(refI, refI, temp1);
        md.assign(refI, refI + 1, 1.0 / Te);

        if (opFlags[outside_vlim]) {
            md.assign(refI + 1, refI + 1, sD.cj);
        } else {
            // Vr
            if (VLoc != kNullLocation) {
                md.assign(refI + 1, VLoc, -Ka / Ta);
            }
            md.assign(refI + 1, refI + 1, -1.0 / Ta - sD.cj);
            md.assign(refI + 1, refI + 2, Ka * Kf / Ta);
        }

        // X1
        md.assign(refI + 2, refI + 1, 1.0 / (Tf * Tf2));
        md.assign(refI + 2, refI + 2, -1.0 / Tf - sD.cj);
        md.assign(refI + 2, refI + 3, -1.0 / (Tf * Tf2));

        // X2
        md.assign(refI + 3, refI + 1, 1.0 / Tf2);
        md.assign(refI + 3, refI + 3, -1.0 / Tf2 - sD.cj);

        // printf("%f\n",sD.cj);
    }

    static const stringVec ieeeType2Fields{"ef", "vr", "x1", "x2"};

    stringVec ExciterIEEEtype2::localStateNames() const
    {
        return ieeeType2Fields;
    }
    void ExciterIEEEtype2::rootTest(const IOdata& inputs,
                                    const stateData& sD,
                                    double roots[],
                                    const solverMode& sMode)
    {
        auto offset = offsets.getAlgOffset(sMode);
        int rootOffset = offsets.getRootOffset(sMode);
        const double* es = sD.state + offset;

        if (opFlags[outside_vlim]) {
            roots[rootOffset] =
                Ka * Kf * es[2] + Ka * (Vref + vBias - inputs[voltageInLocation]) - es[1];
        } else {
            roots[rootOffset] = std::min(Vrmax - es[1], es[1] - Vrmin) + 0.0001;
            if (es[1] > Vrmax) {
                opFlags.set(etrigger_high);
            }
        }
    }

    change_code ExciterIEEEtype2::rootCheck(const IOdata& inputs,
                                            const stateData& /*sD*/,
                                            const solverMode& /*sMode*/,
                                            check_level_t /*level*/)
    {
        double* es = m_state.data();
        change_code ret = change_code::no_change;
        if (opFlags[outside_vlim]) {
            double test = Ka * Kf * es[2] + Ka * (Vref + vBias - inputs[voltageInLocation]) - es[1];
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
            if (es[1] > Vrmax + 0.0001) {
                opFlags.set(etrigger_high);
                opFlags.set(outside_vlim);
                es[1] = Vrmax;
                ret = change_code::jacobian_change;
                alert(this, JAC_COUNT_DECREASE);
            } else if (es[1] < Vrmin - 0.0001) {
                opFlags.reset(etrigger_high);
                opFlags.set(outside_vlim);
                es[1] = Vrmin;
                ret = change_code::jacobian_change;
                alert(this, JAC_COUNT_DECREASE);
            }
        }

        return ret;
    }

    void ExciterIEEEtype2::set(const std::string& param, const std::string& val)
    {
        return ExciterIEEEtype1::set(param, val);
    }

    // set parameters
    void ExciterIEEEtype2::set(const std::string& param, double val, units::unit unitType)
    {
        if (param == "tf1") {
            Tf = val;
        } else if (param == "tf2") {
            Tf2 = val;
        } else {
            ExciterIEEEtype1::set(param, val, unitType);
        }
    }

}  // namespace griddyn::exciters
