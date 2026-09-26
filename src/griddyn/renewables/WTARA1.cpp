/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WTARA1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 1> inputPortMap{{
        {.signal = RenewableSignal::pitchAngle,
         .ioIndex = 0,
         .base = RenewableBase::none,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 2> outputPortMap{{
        {.signal = RenewableSignal::mechanicalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::initialPitchAngle, .ioIndex = 1},
    }};
}  // namespace

WTARA1::WTARA1(const std::string& name): RenewableComponent(name)
{
    m_inputSize = 1;
    m_outputSize = 2;
}

CoreObject* WTARA1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<WTARA1, RenewableComponent>(this, obj);
    if (out != nullptr) {
        out->Ka = Ka;
        out->theta0 = theta0;
        out->initialPower = initialPower;
    }
    return out;
}

std::span<const RenewablePort> WTARA1::inputPorts() const
{
    return inputPortMap;
}
std::span<const RenewablePort> WTARA1::outputPorts() const
{
    return outputPortMap;
}

void WTARA1::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "ka") {
        Ka = val;
    } else if (key == "theta0") {
        theta0 = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double WTARA1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "ka") {
        return Ka;
    }
    if (key == "theta0") {
        return theta0;
    }
    return RenewableComponent::get(param, unitType);
}

void WTARA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Ka) || !std::isfinite(theta0) || Ka < 0) {
        throw InvalidParameterValue("WTARA1 invalid pitch gain or initial angle");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.jacSize = 3;
    prevTime = time0;
}

double WTARA1::mechanicalPower(const IOdata& inputs) const
{
    const double theta = inputs.empty() || inputs[0] == kNullVal ? theta0 : inputs[0];
    return initialPower - (Ka * (theta - theta0));
}

void WTARA1::dynObjectInitializeB(const IOdata& /*inputs*/,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("WTARA1 requires initial mechanical power");
    }
    initialPower = desiredOutput[0];
    m_state[0] = initialPower;
    m_state[1] = theta0;
    fieldSet = {initialPower, theta0};
}

void WTARA1::residual(const IOdata& inputs,
                      const StateData& stateData,
                      double resid[],
                      const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    loc.destLoc[0] = mechanicalPower(inputs) - loc.algStateLoc[0];
    loc.destLoc[1] = theta0 - loc.algStateLoc[1];
}

void WTARA1::algebraicUpdate(const IOdata& inputs,
                             const StateData& stateData,
                             double update[],
                             const SolverMode& sMode,
                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = mechanicalPower(inputs);
    loc.destLoc[1] = theta0;
}

void WTARA1::jacobianElements(const IOdata& inputs,
                              const StateData& /*stateDataValue*/,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(alg, alg, -1.0);
    matrixData.assign(alg + 1, alg + 1, -1.0);
    if (!inputLocs.empty() && !inputs.empty() && inputs[0] != kNullVal) {
        matrixData.assignCheckCol(alg, inputLocs[0], -Ka);
    }
}

void WTARA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    if (time < prevTime) {
        throw InvalidParameterValue("WTARA1 timestep precedes current time");
    }
    m_state[0] = mechanicalPower(inputs);
    prevTime = time;
}

stringVec WTARA1::localStateNames() const
{
    return {"Pm", "theta0"};
}

}  // namespace griddyn
