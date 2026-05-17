/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterSEXS.h"

#include "../Exciter.h"
#include "../gridComponentHelperClasses.h"
#include "../gridDynDefinitions.hpp"
#include "../gridPrimary.h"
#include "core/coreDefinitions.hpp"
#include "core/coreObject.h"
#include "core/coreObjectTemplates.hpp"
#include "solvers/solverMode.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace griddyn::exciters {

ExciterSEXS::ExciterSEXS(const std::string& objName): Exciter(objName)
{
    Ka = 12.0;
    Ta = 0.1;
    Tb = 0.46;
    Te = 0.5;
    Vrmin = -0.9;
    Vrmax = 1.0;
}

CoreObject* ExciterSEXS::clone(CoreObject* obj) const
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

    auto* stateValues = m_state.data();
    const auto vErr = Vref + vBias - inputs[voltageInLocation];
    if (Tb != 0.0) {
        stateValues[1] = (stateValues[0] / Ka) - ((Ta / Tb) * vErr);
    } else {
        stateValues[1] = 0.0;
    }

    m_dstate_dt[0] = 0.0;
    m_dstate_dt[1] = 0.0;
}

void ExciterSEXS::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterSEXS::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tb") {
        Tb = val;
    } else if (param == "te") {
        Te = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

stringVec ExciterSEXS::localStateNames() const
{
    return {"ef", "x"};
}

double ExciterSEXS::regulatorOutput(const IOdata& inputs, const double stateX) const
{
    const auto invTb = (Tb != 0.0) ? (1.0 / Tb) : 0.0;
    const auto vErr = Vref + vBias - inputs[voltageInLocation];
    return Ka * (stateX + (Ta * invTb * vErr));
}

void ExciterSEXS::residual(const IOdata& inputs,
                           const stateData& stateData,
                           double resid[],
                           const solverMode& solverMode)
{
    if (!hasDifferential(solverMode)) {
        return;
    }

    derivative(inputs, stateData, resid, solverMode);

    auto offset = offsets.getDiffOffset(solverMode);
    const auto* stateDerivatives = stateData.dstate_dt + offset;
    resid[offset] -= stateDerivatives[0];
    resid[offset + 1] -= stateDerivatives[1];
}

void ExciterSEXS::derivative(const IOdata& inputs,
                             const stateData& stateData,
                             double deriv[],
                             const solverMode& solverMode)
{
    auto locations = offsets.getLocations(stateData, deriv, solverMode, this);
    const auto* exciterState = locations.diffStateLoc;
    auto* derivatives = locations.destDiffLoc;

    const auto invTe = (Te != 0.0) ? (1.0 / Te) : 0.0;
    const auto invTb = (Tb != 0.0) ? (1.0 / Tb) : 0.0;
    const auto vErr = Vref + vBias - inputs[voltageInLocation];
    const double regulatorVoltage = [&]() {
        if (opFlags[outside_vlim]) {
            return opFlags[etrigger_high] ? Vrmax : Vrmin;
        }
        return regulatorOutput(inputs, exciterState[1]);
    }();

    derivatives[0] = (-exciterState[0] + regulatorVoltage) * invTe;
    derivatives[1] = (-exciterState[1] + ((1.0 - (Ta * invTb)) * vErr)) * invTb;
}

void ExciterSEXS::jacobianElements(const IOdata& /*inputs*/,
                                   const stateData& stateData,
                                   matrixData<double>& matrix,
                                   const IOlocs& inputLocs,
                                   const solverMode& solverMode)
{
    if (!hasDifferential(solverMode)) {
        return;
    }

    auto offset = offsets.getDiffOffset(solverMode);
    const auto invTe = (Te != 0.0) ? (1.0 / Te) : 0.0;
    const auto invTb = (Tb != 0.0) ? (1.0 / Tb) : 0.0;
    matrix.assign(offset, offset, -invTe - stateData.cj);

    if (!opFlags[outside_vlim]) {
        matrix.assign(offset, offset + 1, Ka * invTe);
        matrix.assignCheckCol(offset, inputLocs[voltageInLocation], -(Ka * Ta * invTb * invTe));
    }

    matrix.assign(offset + 1, offset + 1, -invTb - stateData.cj);
    matrix.assignCheckCol(offset + 1,
                          inputLocs[voltageInLocation],
                          -((1.0 - (Ta * invTb)) * invTb));
}

void ExciterSEXS::rootTest(const IOdata& inputs,
                           const stateData& stateData,
                           double root[],
                           const solverMode& solverMode)
{
    auto offset = offsets.getDiffOffset(solverMode);
    auto rootOffset = offsets.getRootOffset(solverMode);
    const auto regulatorVoltage = regulatorOutput(inputs, stateData.state[offset + 1]);

    if (opFlags[outside_vlim]) {
        root[rootOffset] =
            opFlags[etrigger_high] ? (Vrmax - regulatorVoltage) : (regulatorVoltage - Vrmin);
    } else {
        root[rootOffset] = std::min(Vrmax - regulatorVoltage, regulatorVoltage - Vrmin) + 0.00001;
        if (regulatorVoltage >= Vrmax) {
            opFlags.set(etrigger_high);
        }
    }
}

void ExciterSEXS::rootTrigger(coreTime time,
                              const IOdata& inputs,
                              const std::vector<int>& rootMask,
                              const solverMode& solverMode)
{
    auto rootOffset = offsets.getRootOffset(solverMode);
    if (rootMask[rootOffset] == 0) {
        return;
    }

    if (opFlags[outside_vlim]) {
        alert(this, JAC_COUNT_INCREASE);
        opFlags.reset(outside_vlim);
        opFlags.reset(etrigger_high);
    } else {
        const auto regulatorVoltage = regulatorOutput(inputs, m_state[1]);
        opFlags.set(outside_vlim);
        if (regulatorVoltage >= Vrmax) {
            opFlags.set(etrigger_high);
        } else {
            opFlags.reset(etrigger_high);
        }
        alert(this, JAC_COUNT_DECREASE);
    }

    const stateData state(time, m_state.data());
    derivative(inputs, state, m_dstate_dt.data(), solverMode);
}

change_code ExciterSEXS::rootCheck(const IOdata& inputs,
                                   const stateData& /*stateData*/,
                                   const solverMode& /*solverMode*/,
                                   check_level_t /*level*/)
{
    const auto regulatorVoltage = regulatorOutput(inputs, m_state[1]);
    auto ret = change_code::no_change;

    if (opFlags[outside_vlim]) {
        if (opFlags[etrigger_high]) {
            if (regulatorVoltage < Vrmax) {
                opFlags.reset(outside_vlim);
                opFlags.reset(etrigger_high);
                alert(this, JAC_COUNT_INCREASE);
                ret = change_code::jacobian_change;
            }
        } else if (regulatorVoltage > Vrmin) {
            opFlags.reset(outside_vlim);
            alert(this, JAC_COUNT_INCREASE);
            ret = change_code::jacobian_change;
        }
    } else if (regulatorVoltage > Vrmax + 0.00001) {
        opFlags.set(etrigger_high);
        opFlags.set(outside_vlim);
        alert(this, JAC_COUNT_DECREASE);
        ret = change_code::jacobian_change;
    } else if (regulatorVoltage < Vrmin - 0.00001) {
        opFlags.reset(etrigger_high);
        opFlags.set(outside_vlim);
        alert(this, JAC_COUNT_DECREASE);
        ret = change_code::jacobian_change;
    }

    return ret;
}

}  // namespace griddyn::exciters
