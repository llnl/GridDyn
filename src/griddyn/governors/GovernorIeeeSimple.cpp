/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorIeeeSimple.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "core/ObjectFactory.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::governors {
using units::puMW;
using units::unit;
namespace {
    constexpr double powerLimitRootTolerance = 1e-5;
}

GovernorIeeeSimple::GovernorIeeeSimple(const std::string& objName): Governor(objName)
{
    // default values
    K = 16.667;
    T1 = 0.1;
    T2 = 0.15;
    T3 = 0.05;
    offsets.local().local.algSize = 0;
    offsets.local().local.diffSize = 2;
    offsets.local().local.jacSize = 6;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}

const ModelDescription& GovernorIeeeSimple::getModelDescription() const
{
    static const auto description = ModelDescription::define<GovernorIeeeSimple>(
        "GovernorIeeeSimple",
        &Governor::getModelDescription(),
        {parameter({"pup"}, &GovernorIeeeSimple::Pup, units::puMW, "Upper ramp limit"),
         parameter({"pdown"}, &GovernorIeeeSimple::Pdown, units::puMW, "Lower ramp limit"),
         state("pm", units::puMW, "Mechanical power"),
         state("x", units::one, "Governor lag state")});
    return description;
}

CoreObject* GovernorIeeeSimple::clone(CoreObject* obj) const
{
    auto* gov = cloneBase<GovernorIeeeSimple, Governor>(this, obj);
    if (gov == nullptr) {
        return obj;
    }
    gov->T3 = T3;
    gov->Pup = Pup;
    gov->Pdown = Pdown;
    return gov;
}

// destructor
GovernorIeeeSimple::~GovernorIeeeSimple() = default;
void GovernorIeeeSimple::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    Governor::dynObjectInitializeA(time0, flags);
    if ((Pmax < 5000) || (Pmin > -5000)) {
        offsets.local().local.diffRoots++;
        opFlags.set(USES_POWER_LIMITS);
    }
}

// initial conditions
void GovernorIeeeSimple::dynObjectInitializeB(const IOdata& /*inputs*/,
                                              const IOdata& desiredOutput,
                                              IOdata& fieldSet)
{
    const double originalUpperLimit = Pmax;
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0]) ||
        !adjustInitialUpperLimit(desiredOutput[0], "IEEE simple initial output")) {
        throw InvalidParameterValue("IEEE simple initial output outside upper limit");
    }
    if (Wref < 0) {
        Wref = systemBaseFrequency;
    }
    m_state[1] = 0;
    m_state[0] = desiredOutput[0];
    fieldSet[1] = desiredOutput[0];
    if (opFlags[USES_POWER_LIMITS]) {
        const double correction = std::max(0.0, Pmax - originalUpperLimit);
        const double margin = std::max(0.2 * correction, 10.0 * powerLimitRootTolerance * T3);
        if (m_state[0] >= Pmax) {
            initialSetpointOverdrive = margin;
            opFlags.set(POWER_LIMIT_HIGH);
            opFlags.set(POWER_LIMITED);
        } else if (m_state[0] <= Pmin) {
            initialSetpointOverdrive = -margin;
            opFlags.reset(POWER_LIMIT_HIGH);
            opFlags.set(POWER_LIMITED);
        }
    }
}

// residual
void GovernorIeeeSimple::residual(const IOdata& inputs,
                                  const StateData& stateData,
                                  double resid[],
                                  const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    derivative(inputs, stateData, resid, sMode);

    locations.destDiffLoc[0] -= locations.dstateLoc[0];
    locations.destDiffLoc[1] -= locations.dstateLoc[1];
}

void GovernorIeeeSimple::derivative(const IOdata& inputs,
                                    const StateData& stateData,
                                    double deriv[],
                                    const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);

    const double* state = locations.diffStateLoc;
    const double omega = inputs[govOmegaInLocation];
    if (opFlags[POWER_LIMITED]) {
        locations.destDiffLoc[0] = 0;
    } else {
        locations.destDiffLoc[0] = (effectivePowerSetpoint(inputs) - state[0] - (K * state[1]) -
                                    (K * T2 * (omega - 1.0) / T1)) /
            T3;
    }

    locations.destDiffLoc[1] = (-state[1] + ((1 - (T2 / T1)) * (omega - 1.0))) / T1;
}

void GovernorIeeeSimple::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    GovernorIeeeSimple::derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double elapsed = time - prevTime;
    m_state[0] += elapsed * m_dstate_dt[0];
    m_state[1] += elapsed * m_dstate_dt[1];
    if (opFlags[POWER_LIMITED]) {
    } else {
        if (m_state[0] > Pmax) {
            opFlags.set(POWER_LIMITED);
            opFlags.set(POWER_LIMIT_HIGH);
            m_state[0] = Pmax;
        }
    }

    prevTime = time;
}

void GovernorIeeeSimple::jacobianElements(const IOdata& /*inputs*/,
                                          const StateData& stateData,
                                          MatrixData<double>& matrixData,
                                          const IOlocs& inputLocs,
                                          const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, nullptr, sMode, this);

    const auto stateIndex = locations.diffOffset;
    if (inputLocs[govOmegaInLocation] != kNullLocation) {
        if (!opFlags[POWER_LIMITED]) {
            matrixData.assign(stateIndex, inputLocs[govOmegaInLocation], -K * T2 / (T1 * T3));
        }
        matrixData.assign(stateIndex + 1, inputLocs[govOmegaInLocation], (T1 - T2) / (T1 * T1));
    }
    if (opFlags[POWER_LIMITED]) {
        // The active limiter residual is -dPm/dt, so its DAE diagonal is -cj.
        matrixData.assign(stateIndex, stateIndex, -stateData.cj);
    } else {
        matrixData.assign(stateIndex, stateIndex, (-1 / T3) - stateData.cj);
        matrixData.assign(stateIndex, stateIndex + 1, -K / T3);

        matrixData.assignCheck(stateIndex, inputLocs[govpSetInLocation], 1 / T3);
    }
    matrixData.assign(stateIndex + 1, stateIndex + 1, (-1 / T1) - stateData.cj);
}

index_t GovernorIeeeSimple::findIndex(std::string_view field, const SolverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "pm") {
        ret = offsets.getDiffOffset(sMode);
    } else if (field == "x") {
        ret = offsets.getDiffOffset(sMode);
        ret = (ret != kNullLocation) ? ret + 1 : ret;
    }
    return ret;
}

void GovernorIeeeSimple::rootTest(const IOdata& inputs,
                                  const StateData& stateData,
                                  double roots[],
                                  const SolverMode& sMode)
{
    const auto rootOffset = offsets.getRootOffset(sMode);
    if (opFlags[USES_POWER_LIMITS]) {
        const auto locations = offsets.getLocations(stateData, nullptr, sMode, this);

        const double mechanicalPower = locations.diffStateLoc[0];

        if (!opFlags[POWER_LIMITED]) {
            roots[rootOffset] =
                std::min(Pmax - mechanicalPower, mechanicalPower - Pmin) + powerLimitRootTolerance;
            if (mechanicalPower >= Pmax) {
                opFlags.set(POWER_LIMIT_HIGH);
            } else if (mechanicalPower <= Pmin) {
                opFlags.reset(POWER_LIMIT_HIGH);
            }
        } else {
            const double omega = inputs[govOmegaInLocation];
            const double releaseRate =
                (effectivePowerSetpoint(inputs) - mechanicalPower -
                 (K * locations.diffStateLoc[1]) - (K * T2 * (omega - 1.0) / T1)) /
                T3;
            roots[rootOffset] =
                (opFlags[POWER_LIMIT_HIGH] ? releaseRate : -releaseRate) + powerLimitRootTolerance;
        }
    }
}

void GovernorIeeeSimple::rootTrigger(CoreTime /*time*/,
                                     const IOdata& inputs,
                                     const std::vector<int>& rootMask,
                                     const SolverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    /*if (opFlags.test (uses_deadband))
  {
    if (rootMask[rootOffset])
      {
        Governor::rootTrigger (time, inputs, rootMask, sMode);
      }
    ++rootOffset;
  }
      */
    if (opFlags.test(USES_POWER_LIMITS)) {
        if (rootMask[rootOffset] != 0) {
            if (opFlags.test(POWER_LIMITED)) {
                opFlags.reset(POWER_LIMITED);
                opFlags.reset(POWER_LIMIT_HIGH);
                alert(this, JAC_COUNT_INCREASE);
            } else {
                opFlags.set(POWER_LIMITED);
                alert(this, JAC_COUNT_DECREASE);
            }

            derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
        }
        ++rootOffset;
    }
}

// set parameters
void GovernorIeeeSimple::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorIeeeSimple::set(std::string_view param, double val, unit unitType)
{
    if (param == "t3") {
        Governor::getModelDescription().trySetLocal(*this, param, val, unitType);
        return;
    }
    if (param != "pup" && param != "pdown" && param != "ramplimit") {
        Governor::set(param, val, unitType);
        return;
    }
    if (!std::isfinite(val)) {
        throw InvalidParameterValue(param);
    }
    const auto limit = convert(val, unitType, puMW, systemBasePower);
    if (param != "pdown") {
        GovernorIeeeSimple::getModelDescription().trySetLocal(*this, "pup", limit);
    }
    if (param != "pup") {
        GovernorIeeeSimple::getModelDescription().trySetLocal(*this, "pdown", limit);
    }
}

double GovernorIeeeSimple::get(std::string_view param, unit unitType) const
{
    if (param == "pup") {
        return convert(Pup, puMW, unitType, systemBasePower);
    }
    if (param == "pdown") {
        return convert(Pdown, puMW, unitType, systemBasePower);
    }
    return Governor::get(param, unitType);
}

}  // namespace griddyn::governors
