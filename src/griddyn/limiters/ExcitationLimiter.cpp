/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "../ExcitationLimiter.h"

#include "ExcitationLimiterMNLEX2.h"
#include "ExcitationLimiterOEL3C.h"
#include "ExcitationLimiterOEL4C.h"
#include "ExcitationLimiterUEL1.h"
#include "ExcitationLimiterUEL2C.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    static TypeFactory<ExcitationLimiter>
        gExcitationLimiterFactory("excitationlimiter",
                                  std::to_array<std::string_view>({"limiter"}));
    static ChildTypeFactory<limiters::ExcitationLimiterMNLEX2, ExcitationLimiter>
        gMnlex2Factory("excitationlimiter", "mnlex2");
    static ChildTypeFactory<limiters::ExcitationLimiterOEL3C, ExcitationLimiter>
        gOel3cFactory("excitationlimiter", "oel3c");
    static ChildTypeFactory<limiters::ExcitationLimiterOEL4C, ExcitationLimiter>
        gOel4cFactory("excitationlimiter", "oel4c");
    static ChildTypeFactory<limiters::ExcitationLimiterUEL1, ExcitationLimiter>
        gUel1Factory("excitationlimiter", "uel1");
    static ChildTypeFactory<limiters::ExcitationLimiterUEL2C, ExcitationLimiter>
        gUel2cFactory("excitationlimiter", "uel2c");
}  // namespace

ExcitationLimiter::ExcitationLimiter(const std::string& objName): GridSubModel(objName)
{
    m_inputSize = excitationLimiterInputCount;
    offsets.local().local.algSize = 1;
    offsets.local().local.jacSize = 6;
}

CoreObject* ExcitationLimiter::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiter, GridSubModel>(this, obj);
    if (result != nullptr) {
        result->limiterRole = limiterRole;
        result->threshold = threshold;
        result->gain = gain;
        result->maximumAction = maximumAction;
        result->thresholdSet = thresholdSet;
        result->roleInitialized = false;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiter::setRole(Role role)
{
    if (!supportsRole(role)) {
        throw InvalidParameterValue("excitation limiter does not support the requested role");
    }
    // DynamicGenerator assigns a role-specific slot when it attaches us.
    if (role != limiterRole && (roleInitialized || locIndex != kNullLocation)) {
        throw InvalidParameterValue(
            "set limiter role before attachment; replacement requires a full dynamic reset");
    }
    limiterRole = role;
}

void ExcitationLimiter::set(std::string_view param, std::string_view val)
{
    if (param == "role") {
        if ((val == "uel") || (val == "under")) {
            setRole(Role::UNDER);
        } else if ((val == "oel") || (val == "over")) {
            setRole(Role::OVER);
        } else {
            throw InvalidParameterValue("excitation limiter role");
        }
    } else {
        GridSubModel::set(param, val);
    }
}

void ExcitationLimiter::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "threshold") {
        threshold = val;
        thresholdSet = true;
    } else if (param == "gain") {
        gain = val;
    } else if ((param == "maxaction") || (param == "max_action")) {
        maximumAction = val;
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

double ExcitationLimiter::get(std::string_view param, units::unit unitType) const
{
    if (param == "threshold") {
        return threshold;
    }
    if (param == "gain") {
        return gain;
    }
    if ((param == "maxaction") || (param == "max_action")) {
        return maximumAction;
    }
    return GridSubModel::get(param, unitType);
}

void ExcitationLimiter::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!thresholdSet || !std::isfinite(threshold) || !std::isfinite(gain) ||
        !std::isfinite(maximumAction) || gain <= 0.0 || maximumAction <= 0.0) {
        throw InvalidParameterValue("excitation limiter threshold, gain, or maximum action");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 0;
    offsets.local().local.jacSize = 6;
    prevTime = time0;
    roleInitialized = true;
}

ExcitationLimiter::Evaluation ExcitationLimiter::evaluate(const IOdata& inputs) const
{
    if (inputs.size() < excitationLimiterInputCount) {
        throw InvalidParameterValue("excitation limiter input vector");
    }
    Evaluation result;
    double error = 0.0;
    if (limiterRole == Role::OVER) {
        const double fieldCurrent = inputs[limiterFieldCurrentInLocation];
        if (!std::isfinite(fieldCurrent) || std::abs(fieldCurrent) > 1e20) {
            throw InvalidParameterValue("OEL requires a compatible field-current signal");
        }
        error = fieldCurrent - threshold;
        result.derivatives[limiterFieldCurrentInLocation] = gain;
    } else {
        for (auto index :
             {limiterIdInLocation, limiterIqInLocation, limiterVdInLocation, limiterVqInLocation}) {
            if (!std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
                throw InvalidParameterValue("UEL requires compatible current and voltage signals");
            }
        }
        const double directCurrent = inputs[limiterIdInLocation];
        const double iq = inputs[limiterIqInLocation];
        const double vd = inputs[limiterVdInLocation];
        const double vq = inputs[limiterVqInLocation];
        const double reactiveInjection = (directCurrent * vq) - (iq * vd);
        error = threshold - reactiveInjection;
        result.derivatives[limiterIdInLocation] = -gain * vq;
        result.derivatives[limiterIqInLocation] = gain * vd;
        result.derivatives[limiterVdInLocation] = gain * iq;
        result.derivatives[limiterVqInLocation] = -gain * directCurrent;
    }
    const double rawAction = gain * error;
    result.action = std::clamp(rawAction, 0.0, maximumAction);
    if (rawAction <= 0.0 || rawAction >= maximumAction) {
        result.derivatives.fill(0.0);
    }
    return result;
}

void ExcitationLimiter::dynObjectInitializeB(const IOdata& inputs,
                                             const IOdata& /*desiredOutput*/,
                                             IOdata& fieldSet)
{
    m_state[0] = evaluate(inputs).action;
    fieldSet = {m_state[0]};
}

void ExcitationLimiter::residual(const IOdata& inputs,
                                 const StateData& stateData,
                                 double resid[],
                                 const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    loc.destLoc[0] = evaluate(inputs).action - loc.algStateLoc[0];
}

void ExcitationLimiter::algebraicUpdate(const IOdata& inputs,
                                        const StateData& stateData,
                                        double update[],
                                        const SolverMode& sMode,
                                        double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = evaluate(inputs).action;
}

void ExcitationLimiter::jacobianElements(const IOdata& inputs,
                                         const StateData& stateData,
                                         MatrixData<double>& matrixData,
                                         const IOlocs& inputLocs,
                                         const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, sMode, this);
    matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
    const auto evaluation = evaluate(inputs);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        if (evaluation.derivatives[index] != 0.0) {
            matrixData.assignCheckCol(loc.algOffset,
                                      inputLocs[index],
                                      evaluation.derivatives[index]);
        }
    }
}

void ExcitationLimiter::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    m_state[0] = evaluate(inputs).action;
    prevTime = time;
}

const std::vector<stringVec>& ExcitationLimiter::inputNames() const
{
    static const std::vector<stringVec> names{
        {"xadifd", "fieldcurrent"}, {"id"}, {"iq"}, {"vd"}, {"vq"}};
    return names;
}

const std::vector<stringVec>& ExcitationLimiter::outputNames() const
{
    static const std::vector<stringVec> names{{"action", "limiteroutput"}};
    return names;
}
}  // namespace griddyn
