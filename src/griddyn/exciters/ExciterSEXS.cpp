/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterSEXS.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>

namespace griddyn {
namespace exciters {

    ExciterSEXS::ExciterSEXS(const std::string& objName): Exciter(objName)
    {
        Ka = 12.0;
        Ta = 0.1;
        Tb = 0.46;
        Te = 0.5;
        Vrmin = -0.9;
        Vrmax = 1.0;
    }

    coreObject* ExciterSEXS::clone(coreObject* obj) const
    {
        auto* gdE = cloneBase<ExciterSEXS, Exciter>(this, obj);
        if (gdE == nullptr) {
            return obj;
        }
        gdE->Te = Te;
        gdE->Tb = Tb;
        return gdE;
    }

    void ExciterSEXS::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
    {
        offsets.local().local.diffSize = 2;
        offsets.local().local.jacSize = 6;
        checkForLimits();
    }

    void ExciterSEXS::dynObjectInitializeB(const IOdata& inputs,
                                           const IOdata& desiredOutput,
                                           IOdata& fieldSet)
    {
        Exciter::dynObjectInitializeB(inputs, desiredOutput, fieldSet);

        auto* gs = m_state.data();
        const auto vErr = Vref + vBias - inputs[voltageInLocation];
        if (Tb != 0.0) {
            gs[1] = gs[0] / Ka - (Ta / Tb) * vErr;
        } else {
            gs[1] = 0.0;
        }

        m_dstate_dt[0] = 0.0;
        m_dstate_dt[1] = 0.0;
    }

    void ExciterSEXS::set(const std::string& param, const std::string& val)
    {
        Exciter::set(param, val);
    }

    void ExciterSEXS::set(const std::string& param, double val, units::unit unitType)
    {
        if (param == "tb") {
            Tb = val;
        } else if (param == "te") {
            Te = val;
        } else {
            Exciter::set(param, val, unitType);
        }
    }

    static const stringVec sexsFields{"ef", "x"};

    stringVec ExciterSEXS::localStateNames() const
    {
        return sexsFields;
    }

    double ExciterSEXS::regulatorOutput(const IOdata& inputs, const double stateX) const
    {
        const auto invTb = (Tb != 0.0) ? (1.0 / Tb) : 0.0;
        const auto vErr = Vref + vBias - inputs[voltageInLocation];
        return Ka * (stateX + Ta * invTb * vErr);
    }

    void ExciterSEXS::residual(const IOdata& inputs,
                               const stateData& sD,
                               double resid[],
                               const solverMode& sMode)
    {
        if (!hasDifferential(sMode)) {
            return;
        }

        derivative(inputs, sD, resid, sMode);

        auto offset = offsets.getDiffOffset(sMode);
        const auto* esp = sD.dstate_dt + offset;
        resid[offset] -= esp[0];
        resid[offset + 1] -= esp[1];
    }

    void ExciterSEXS::derivative(const IOdata& inputs,
                                 const stateData& sD,
                                 double deriv[],
                                 const solverMode& sMode)
    {
        auto loc = offsets.getLocations(sD, deriv, sMode, this);
        const auto* es = loc.diffStateLoc;
        auto* d = loc.destDiffLoc;

        const auto invTe = (Te != 0.0) ? (1.0 / Te) : 0.0;
        const auto invTb = (Tb != 0.0) ? (1.0 / Tb) : 0.0;
        const auto vErr = Vref + vBias - inputs[voltageInLocation];
        const auto vr = opFlags[outside_vlim] ? ((opFlags[etrigger_high]) ? Vrmax : Vrmin) :
                                                regulatorOutput(inputs, es[1]);

        d[0] = (-es[0] + vr) * invTe;
        d[1] = (-es[1] + (1.0 - Ta * invTb) * vErr) * invTb;
    }

    void ExciterSEXS::jacobianElements(const IOdata& /*inputs*/,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       const IOlocs& inputLocs,
                                       const solverMode& sMode)
    {
        if (!hasDifferential(sMode)) {
            return;
        }

        auto offset = offsets.getDiffOffset(sMode);
        const auto invTe = (Te != 0.0) ? (1.0 / Te) : 0.0;
        const auto invTb = (Tb != 0.0) ? (1.0 / Tb) : 0.0;
        md.assign(offset, offset, -invTe - sD.cj);

        if (!opFlags[outside_vlim]) {
            md.assign(offset, offset + 1, Ka * invTe);
            md.assignCheckCol(offset, inputLocs[voltageInLocation], -Ka * Ta * invTb * invTe);
        }

        md.assign(offset + 1, offset + 1, -invTb - sD.cj);
        md.assignCheckCol(offset + 1, inputLocs[voltageInLocation], -(1.0 - Ta * invTb) * invTb);
    }

    void ExciterSEXS::rootTest(const IOdata& inputs,
                               const stateData& sD,
                               double root[],
                               const solverMode& sMode)
    {
        auto offset = offsets.getDiffOffset(sMode);
        auto rootOffset = offsets.getRootOffset(sMode);
        const auto vr = regulatorOutput(inputs, sD.state[offset + 1]);

        if (opFlags[outside_vlim]) {
            root[rootOffset] = opFlags[etrigger_high] ? (Vrmax - vr) : (vr - Vrmin);
        } else {
            root[rootOffset] = std::min(Vrmax - vr, vr - Vrmin) + 0.00001;
            if (vr >= Vrmax) {
                opFlags.set(etrigger_high);
            }
        }
    }

    void ExciterSEXS::rootTrigger(coreTime time,
                                  const IOdata& inputs,
                                  const std::vector<int>& rootMask,
                                  const solverMode& sMode)
    {
        auto rootOffset = offsets.getRootOffset(sMode);
        if (rootMask[rootOffset] == 0) {
            return;
        }

        if (opFlags[outside_vlim]) {
            alert(this, JAC_COUNT_INCREASE);
            opFlags.reset(outside_vlim);
            opFlags.reset(etrigger_high);
        } else {
            const auto vr = regulatorOutput(inputs, m_state[1]);
            opFlags.set(outside_vlim);
            if (vr >= Vrmax) {
                opFlags.set(etrigger_high);
            } else {
                opFlags.reset(etrigger_high);
            }
            alert(this, JAC_COUNT_DECREASE);
        }

        stateData state(time, m_state.data());
        derivative(inputs, state, m_dstate_dt.data(), sMode);
    }

    change_code ExciterSEXS::rootCheck(const IOdata& inputs,
                                       const stateData& /*sD*/,
                                       const solverMode& /*sMode*/,
                                       check_level_t /*level*/)
    {
        const auto vr = regulatorOutput(inputs, m_state[1]);
        auto ret = change_code::no_change;

        if (opFlags[outside_vlim]) {
            if (opFlags[etrigger_high]) {
                if (vr < Vrmax) {
                    opFlags.reset(outside_vlim);
                    opFlags.reset(etrigger_high);
                    alert(this, JAC_COUNT_INCREASE);
                    ret = change_code::jacobian_change;
                }
            } else if (vr > Vrmin) {
                opFlags.reset(outside_vlim);
                alert(this, JAC_COUNT_INCREASE);
                ret = change_code::jacobian_change;
            }
        } else if (vr > Vrmax + 0.00001) {
            opFlags.set(etrigger_high);
            opFlags.set(outside_vlim);
            alert(this, JAC_COUNT_DECREASE);
            ret = change_code::jacobian_change;
        } else if (vr < Vrmin - 0.00001) {
            opFlags.reset(etrigger_high);
            opFlags.set(outside_vlim);
            alert(this, JAC_COUNT_DECREASE);
            ret = change_code::jacobian_change;
        }

        return ret;
    }

}  // namespace exciters
}  // namespace griddyn
