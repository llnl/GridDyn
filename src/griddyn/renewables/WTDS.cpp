/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WTDS.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 3> inputPortMap{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::mechanicalPower,
         .ioIndex = 1,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::speedReference, .ioIndex = 2, .required = false},
    }};
    constexpr std::array<RenewablePort, 2> outputPortMap{{
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 0},
        {.signal = RenewableSignal::turbineSpeed, .ioIndex = 1},
    }};
}  // namespace

WTDS::WTDS(const std::string& name): RenewableComponent(name)
{
    m_inputSize = 3;
    m_outputSize = 2;
}

CoreObject* WTDS::clone(CoreObject* obj) const
{
    auto* out = cloneBase<WTDS, RenewableComponent>(this, obj);
    if (out != nullptr) {
        out->H = H;
        out->D = D;
        out->w0 = w0;
        out->initialPower = initialPower;
        out->operatingSpeed = operatingSpeed;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> WTDS::inputPorts() const
{
    return inputPortMap;
}

std::span<const RenewablePort> WTDS::outputPorts() const
{
    return outputPortMap;
}

void WTDS::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "h") {
        H = val;
    } else if (key == "d") {
        D = val;
    } else if (key == "w0") {
        w0 = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double WTDS::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "h") {
        return H;
    }
    if (key == "d") {
        return D;
    }
    if (key == "w0") {
        return w0;
    }
    return RenewableComponent::get(param, unitType);
}

void WTDS::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(H) || !std::isfinite(D) || !std::isfinite(w0) || H <= 0.0 || D < 0.0 ||
        w0 <= 0.0) {
        throw InvalidParameterValue(
            "WTDS requires positive inertia and speed, nonnegative damping");
    }
    auto& local = offsets.local().local;
    local.diffSize = 1;
    local.jacSize = 5;
    prevTime = time0;
}

void WTDS::dynObjectInitializeB(const IOdata& inputs,
                                const IOdata& /*desiredOutput*/,
                                IOdata& fieldSet)
{
    if (inputs.empty() || !std::isfinite(inputs[0])) {
        throw InvalidParameterValue("WTDS requires initial electrical power");
    }
    initialPower = inputs[0];
    operatingSpeed = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : w0;
    if (!std::isfinite(operatingSpeed) || operatingSpeed <= 0.0) {
        throw InvalidParameterValue("WTDS initial speed reference must be positive");
    }
    m_state[0] = operatingSpeed;
    fieldSet = {operatingSpeed, operatingSpeed};
}

double WTDS::rate(const IOdata& inputs, double speed) const
{
    const double mechanicalPower =
        inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : initialPower;
    const double speedReference =
        inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : operatingSpeed;
    return (((mechanicalPower - inputs[0]) / std::max(speed, 0.01)) -
            (D * (speed - speedReference))) /
        (2.0 * H);
}

void WTDS::derivative(const IOdata& inputs,
                      const StateData& stateData,
                      double deriv[],
                      const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    loc.destDiffLoc[0] = rate(inputs, loc.diffStateLoc[0]);
}

void WTDS::residual(const IOdata& inputs,
                    const StateData& stateData,
                    double resid[],
                    const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    loc.destDiffLoc[0] = rate(inputs, loc.diffStateLoc[0]) - loc.dstateLoc[0];
}

void WTDS::jacobianElements(const IOdata& inputs,
                            const StateData& stateData,
                            MatrixData<double>& matrixData,
                            const IOlocs& inputLocs,
                            const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double speed = loc.diffStateLoc[0];
    const double mechanicalPower =
        inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : initialPower;
    const double speedDenominator = std::max(speed, 0.01);
    const double invInertia = 1.0 / (2.0 * H);
    const double powerSlope =
        speed > 0.01 ? -(mechanicalPower - inputs[0]) / (speedDenominator * speedDenominator) : 0.0;
    const index_t row = loc.diffOffset;
    matrixData.assign(row, row, ((powerSlope - D) * invInertia) - stateData.cj);
    if (!inputLocs.empty()) {
        matrixData.assignCheckCol(row, inputLocs[0], -invInertia / speedDenominator);
    }
    if (inputLocs.size() > 1 && inputs.size() > 1 && inputs[1] != kNullVal) {
        matrixData.assignCheckCol(row, inputLocs[1], invInertia / speedDenominator);
    }
    if (inputLocs.size() > 2 && inputs.size() > 2 && inputs[2] != kNullVal) {
        matrixData.assignCheckCol(row, inputLocs[2], D * invInertia);
    }
}

void WTDS::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime = time - prevTime;
    if (deltaTime < 0.0) {
        throw InvalidParameterValue("WTDS timestep precedes current time");
    }
    m_state[0] += deltaTime * rate(inputs, m_state[0]);
    prevTime = time;
}

double WTDS::getOutput(const IOdata& /*inputs*/,
                       const StateData& stateData,
                       const SolverMode& sMode,
                       index_t outputNum) const
{
    if (outputNum < 0 || outputNum > 1) {
        return kNullVal;
    }
    return offsets.getLocations(stateData, sMode, this).diffStateLoc[0];
}

index_t WTDS::getOutputLoc(const SolverMode& sMode, index_t outputNum) const
{
    return outputNum >= 0 && outputNum <= 1 ? offsets.getDiffOffset(sMode) : kNullLocation;
}

stringVec WTDS::localStateNames() const
{
    return {"w"};
}

}  // namespace griddyn
