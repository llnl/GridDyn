/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterOEL3C.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {

ExcitationLimiterOEL3C::ExcitationLimiterOEL3C(const std::string& objName):
    ExcitationLimiter(objName)
{
    setRole(Role::OVER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 2;
    offsets.local().local.jacSize = 12;
}

CoreObject* ExcitationLimiterOEL3C::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterOEL3C, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->itfpu = itfpu;
        result->kscale = kscale;
        result->tf = tf;
        result->k1 = k1;
        result->koel = koel;
        result->toel = toel;
        result->kpoel = kpoel;
        result->voelMax1 = voelMax1;
        result->voelMin1 = voelMin1;
        result->voelMax2 = voelMax2;
        result->voelMin2 = voelMin2;
        result->prevTime = 0.0;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterOEL3C::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "oelinput" || param == "oel_input") {
        if (val != 0.0) {
            throw InvalidParameterValue(
                "OEL3C currently supports OELInput=0 (machine field current)");
        }
    } else if (param == "itfpu" || param == "itf_pu") {
        itfpu = val;
    } else if (param == "kscale" || param == "k_scale") {
        kscale = val;
    } else if (param == "tf") {
        tf = val;
    } else if (param == "k1") {
        if (!std::isfinite(val) || (val != 1.0 && val != 2.0)) {
            throw InvalidParameterValue("OEL3C K1 must be 1 or 2");
        }
        k1 = static_cast<int>(val);
    } else if (param == "koel" || param == "k_oel") {
        koel = val;
    } else if (param == "toel" || param == "t_oel") {
        toel = val;
    } else if (param == "kpoel" || param == "kp_oel") {
        kpoel = val;
    } else if (param == "voelmax1" || param == "voel_max1") {
        voelMax1 = val;
    } else if (param == "voelmin1" || param == "voel_min1") {
        voelMin1 = val;
    } else if (param == "voelmax2" || param == "voel_max2") {
        voelMax2 = val;
    } else if (param == "voelmin2" || param == "voel_min2") {
        voelMin2 = val;
    } else {
        ExcitationLimiter::set(param, val, unitType);
    }
}

double ExcitationLimiterOEL3C::get(std::string_view param, units::unit unitType) const
{
    if (param == "oelinput" || param == "oel_input") {
        return 0.0;
    }
    if (param == "itfpu" || param == "itf_pu") {
        return itfpu;
    }
    if (param == "kscale" || param == "k_scale") {
        return kscale;
    }
    if (param == "tf") {
        return tf;
    }
    if (param == "k1") {
        return static_cast<double>(k1);
    }
    if (param == "koel" || param == "k_oel") {
        return koel;
    }
    if (param == "toel" || param == "t_oel") {
        return toel;
    }
    if (param == "kpoel" || param == "kp_oel") {
        return kpoel;
    }
    if (param == "voelmax1" || param == "voel_max1") {
        return voelMax1;
    }
    if (param == "voelmin1" || param == "voel_min1") {
        return voelMin1;
    }
    if (param == "voelmax2" || param == "voel_max2") {
        return voelMax2;
    }
    if (param == "voelmin2" || param == "voel_min2") {
        return voelMin2;
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterOEL3C::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(itfpu) || !std::isfinite(kscale) || !std::isfinite(tf) ||
        !std::isfinite(koel) || !std::isfinite(toel) || !std::isfinite(kpoel) ||
        !std::isfinite(voelMax1) || !std::isfinite(voelMin1) || !std::isfinite(voelMax2) ||
        !std::isfinite(voelMin2) || itfpu <= 0.0 || kscale <= 0.0 || tf < 0.0 || koel <= 0.0 ||
        toel <= 0.0 || kpoel < 0.0 || voelMin1 > voelMax1 || voelMin1 > 0.0 ||
        voelMin2 > voelMax2 || voelMax2 > 0.0 || voelMin2 > 0.0) {
        throw InvalidParameterValue("OEL3C parameters and signed VOEL limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 2;
    offsets.local().local.jacSize = 12;
    prevTime = time0;
}

double ExcitationLimiterOEL3C::fieldCurrent(const IOdata& inputs)
{
    if (inputs.size() < excitationLimiterInputCount ||
        !std::isfinite(inputs[limiterFieldCurrentInLocation]) ||
        std::abs(inputs[limiterFieldCurrentInLocation]) > 1e20) {
        throw InvalidParameterValue("OEL3C requires a compatible machine field-current signal");
    }
    return inputs[limiterFieldCurrentInLocation];
}

ExcitationLimiterOEL3C::Evaluation ExcitationLimiterOEL3C::evaluate(double scaledFieldCurrent,
                                                                    double integral) const
{
    const double normalized = std::max(scaledFieldCurrent, 0.0) / itfpu;
    Evaluation result;
    if (normalized > 1.0) {
        result.error = std::pow(normalized, k1) - 1.0;
        result.errorSlope = static_cast<double>(k1) * std::pow(normalized, k1 - 1) / itfpu;
    }
    // IEEE's internal VOEL correction is nonpositive; generator routes carry
    // the positive magnitude consumed by the exciter's summing point.
    const double raw = (-kpoel * result.error) + integral;
    const bool unsaturated = raw > voelMin2 && raw < voelMax2;
    const double internal = std::clamp(raw, voelMin2, voelMax2);
    result.action = -internal;
    if (unsaturated) {
        result.actionErrorSlope = kpoel;
        result.actionIntegralSlope = -1.0;
    }
    return result;
}

double ExcitationLimiterOEL3C::integralRate(double integral, double error) const
{
    const double rate = (-koel / toel) * error;
    if ((integral <= voelMin1 && rate < 0.0) || (integral >= voelMax1 && rate > 0.0)) {
        return 0.0;
    }
    return rate;
}

void ExcitationLimiterOEL3C::dynObjectInitializeB(const IOdata& inputs,
                                                  const IOdata& /*desiredOutput*/,
                                                  IOdata& fieldSet)
{
    m_state[1] = kscale * fieldCurrent(inputs);
    m_state[2] = 0.0;
    m_state[0] = evaluate(m_state[1], m_state[2]).action;
    fieldSet = {m_state[0]};
}

void ExcitationLimiterOEL3C::residual(const IOdata& inputs,
                                      const StateData& stateData,
                                      double resid[],
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const double measured = (tf > 0.0) ? loc.diffStateLoc[0] : kscale * fieldCurrent(inputs);
    const auto result = evaluate(measured, loc.diffStateLoc[1]);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = result.action - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        loc.destDiffLoc[0] = (tf > 0.0) ?
            (((kscale * fieldCurrent(inputs)) - loc.diffStateLoc[0]) / tf) - loc.dstateLoc[0] :
            -loc.dstateLoc[0];
        loc.destDiffLoc[1] = integralRate(loc.diffStateLoc[1], result.error) - loc.dstateLoc[1];
    }
}

void ExcitationLimiterOEL3C::derivative(const IOdata& inputs,
                                        const StateData& stateData,
                                        double deriv[],
                                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double measured = (tf > 0.0) ? loc.diffStateLoc[0] : kscale * fieldCurrent(inputs);
    const auto result = evaluate(measured, loc.diffStateLoc[1]);
    loc.destDiffLoc[0] =
        (tf > 0.0) ? ((kscale * fieldCurrent(inputs)) - loc.diffStateLoc[0]) / tf : 0.0;
    loc.destDiffLoc[1] = integralRate(loc.diffStateLoc[1], result.error);
}

void ExcitationLimiterOEL3C::algebraicUpdate(const IOdata& inputs,
                                             const StateData& stateData,
                                             double update[],
                                             const SolverMode& sMode,
                                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const double measured = (tf > 0.0) ? loc.diffStateLoc[0] : kscale * fieldCurrent(inputs);
    loc.destLoc[0] = evaluate(measured, loc.diffStateLoc[1]).action;
}

void ExcitationLimiterOEL3C::jacobianElements(const IOdata& inputs,
                                              const StateData& stateData,
                                              MatrixData<double>& matrixData,
                                              const IOlocs& inputLocs,
                                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto result = evaluate(tf > 0.0 ? loc.diffStateLoc[0] : kscale * fieldCurrent(inputs),
                                 loc.diffStateLoc[1]);
    const double rate = (-koel / toel) * result.error;
    const bool blockedAtMinimum = loc.diffStateLoc[1] <= voelMin1 && rate < 0.0;
    const bool blockedAtMaximum = loc.diffStateLoc[1] >= voelMax1 && rate > 0.0;
    const bool integrate = !blockedAtMinimum && !blockedAtMaximum;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            if (result.actionIntegralSlope != 0.0) {
                matrixData.assign(loc.algOffset, loc.diffOffset + 1, result.actionIntegralSlope);
            }
            if (tf > 0.0 && result.actionErrorSlope * result.errorSlope != 0.0) {
                matrixData.assign(loc.algOffset,
                                  loc.diffOffset,
                                  result.actionErrorSlope * result.errorSlope);
            } else if (tf == 0.0 && result.actionErrorSlope * result.errorSlope != 0.0) {
                matrixData.assignCheckCol(loc.algOffset,
                                          inputLocs[limiterFieldCurrentInLocation],
                                          result.actionErrorSlope * result.errorSlope * kscale);
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(loc.diffOffset, loc.diffOffset, (tf > 0.0 ? -1.0 / tf : 0.0) - stateData.cj);
    if (tf > 0.0 && inputLocs[limiterFieldCurrentInLocation] != kNullLocation) {
        matrixData.assignCheckCol(loc.diffOffset,
                                  inputLocs[limiterFieldCurrentInLocation],
                                  kscale / tf);
    }
    matrixData.assign(loc.diffOffset + 1, loc.diffOffset + 1, -stateData.cj);
    if (integrate && result.errorSlope != 0.0) {
        const double rateSlope = (-koel / toel) * result.errorSlope;
        if (tf > 0.0) {
            matrixData.assign(loc.diffOffset + 1, loc.diffOffset, rateSlope);
        } else if (inputLocs[limiterFieldCurrentInLocation] != kNullLocation) {
            matrixData.assignCheckCol(loc.diffOffset + 1,
                                      inputLocs[limiterFieldCurrentInLocation],
                                      rateSlope * kscale);
        }
    }
}

void ExcitationLimiterOEL3C::timestep(CoreTime time,
                                      const IOdata& inputs,
                                      const SolverMode& /*sMode*/)
{
    const double step = time - prevTime;
    const double field = kscale * fieldCurrent(inputs);
    if (tf > 0.0) {
        m_state[1] += (step * (field - m_state[1])) / tf;
    } else {
        m_state[1] = field;
    }
    const auto result = evaluate(m_state[1], m_state[2]);
    m_state[2] =
        std::clamp(m_state[2] + (step * integralRate(m_state[2], result.error)),
                   voelMin1,
                   voelMax1);
    m_state[0] = evaluate(m_state[1], m_state[2]).action;
    prevTime = time;
}

stringVec ExcitationLimiterOEL3C::localStateNames() const
{
    return {"voel", "field_current_filter", "integral"};
}
}  // namespace griddyn::limiters
