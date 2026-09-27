/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "BusROCOF.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 1> inputPortMap{{
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 0},
    }};
    constexpr std::array<RenewablePort, 2> outputPortMap{{
        {.signal = RenewableSignal::frequencyDeviation, .ioIndex = 0},
        {.signal = RenewableSignal::rateOfChangeOfFrequency, .ioIndex = 1},
    }};
    constexpr index_t deviationOutput = 0, rocofOutput = 1;
    constexpr index_t angleLag = 0, angleWashout = 1, frequencyWashout = 2;
}  // namespace

BusROCOF::BusROCOF(const std::string& name): RenewableComponent(name)
{
    m_inputSize = 1;
    m_outputSize = 2;
}

CoreObject* BusROCOF::clone(CoreObject* obj) const
{
    auto* out = cloneBase<BusROCOF, RenewableComponent>(this, obj);
    if (out != nullptr) {
        out->Tf = Tf;
        out->Tw = Tw;
        out->Tr = Tr;
        out->fn = fn;
        out->initialAngle = initialAngle;
        out->lastAngle = lastAngle;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> BusROCOF::inputPorts() const
{
    return inputPortMap;
}

std::span<const RenewablePort> BusROCOF::outputPorts() const
{
    return outputPortMap;
}

void BusROCOF::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tf") {
        Tf = val;
    } else if (key == "tw") {
        Tw = val;
    } else if (key == "tr") {
        Tr = val;
    } else if (key == "fn") {
        fn = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double BusROCOF::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tf") {
        return Tf;
    }
    if (key == "tw") {
        return Tw;
    }
    if (key == "tr") {
        return Tr;
    }
    if (key == "fn") {
        return fn;
    }
    return RenewableComponent::get(param, unitType);
}

void BusROCOF::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Tf) || !std::isfinite(Tw) || !std::isfinite(Tr) || !std::isfinite(fn) ||
        Tf <= 0.0 || Tw <= 0.0 || Tr <= 0.0 || fn <= 0.0) {
        throw InvalidParameterValue("BusROCOF requires positive Tf, Tw, Tr, and fn");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = 3;
    local.jacSize = 17;
    prevTime = time0;
}

void BusROCOF::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& /*desiredOutput*/,
                                    IOdata& fieldSet)
{
    if (inputs.empty() || !std::isfinite(inputs[0])) {
        throw InvalidParameterValue("BusROCOF requires initial bus angle");
    }
    initialAngle = inputs[0];
    lastAngle = inputs[0];
    m_state[deviationOutput] = 0.0;
    m_state[rocofOutput] = 0.0;
    m_state[2 + angleLag] = 0.0;
    m_state[2 + angleWashout] = 0.0;
    m_state[2 + frequencyWashout] = 1.0;
    fieldSet = {0.0, 0.0};
}

double BusROCOF::deviation(const double state[]) const
{
    return (state[angleLag] - state[angleWashout]) / (2.0 * std::numbers::pi * fn * Tw);
}

std::array<double, 3> BusROCOF::rates(double angle, const double state[]) const
{
    return {(angle - initialAngle - state[angleLag]) / Tf,
            (state[angleLag] - state[angleWashout]) / Tw,
            (1.0 + deviation(state) - state[frequencyWashout]) / Tr};
}

void BusROCOF::derivative(const IOdata& inputs,
                          const StateData& stateData,
                          double deriv[],
                          const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = rates(inputs[0], loc.diffStateLoc);
    for (index_t index = 0; index < 3; ++index) {
        loc.destDiffLoc[index] = result[index];
    }
}

void BusROCOF::residual(const IOdata& inputs,
                        const StateData& stateData,
                        double resid[],
                        const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const double frequencyDeviation = deviation(loc.diffStateLoc);
    const double dfdt = (1.0 + frequencyDeviation - loc.diffStateLoc[frequencyWashout]) / Tr;
    if (hasAlgebraic(sMode)) {
        loc.destLoc[deviationOutput] = frequencyDeviation - loc.algStateLoc[deviationOutput];
        loc.destLoc[rocofOutput] = dfdt - loc.algStateLoc[rocofOutput];
    }
    if (hasDifferential(sMode)) {
        const auto result = rates(inputs[0], loc.diffStateLoc);
        for (index_t index = 0; index < 3; ++index) {
            loc.destDiffLoc[index] = result[index] - loc.dstateLoc[index];
        }
    }
}

void BusROCOF::algebraicUpdate(const IOdata& /*inputs*/,
                               const StateData& stateData,
                               double update[],
                               const SolverMode& sMode,
                               double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const double frequencyDeviation = deviation(loc.diffStateLoc);
    loc.destLoc[deviationOutput] = frequencyDeviation;
    loc.destLoc[rocofOutput] = (1.0 + frequencyDeviation - loc.diffStateLoc[frequencyWashout]) / Tr;
}

void BusROCOF::jacobianElements(const IOdata& /*inputs*/,
                                const StateData& stateData,
                                MatrixData<double>& matrixData,
                                const IOlocs& inputLocs,
                                const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const index_t alg = loc.algOffset;
    const index_t diff = loc.diffOffset;
    const double gain = 1.0 / (2.0 * std::numbers::pi * fn * Tw);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg + deviationOutput, alg + deviationOutput, -1.0);
        matrixData.assign(alg + rocofOutput, alg + rocofOutput, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(alg + deviationOutput, diff + angleLag, gain);
            matrixData.assign(alg + deviationOutput, diff + angleWashout, -gain);
            matrixData.assign(alg + rocofOutput, diff + angleLag, gain / Tr);
            matrixData.assign(alg + rocofOutput, diff + angleWashout, -gain / Tr);
            matrixData.assign(alg + rocofOutput, diff + frequencyWashout, -1.0 / Tr);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(diff + angleLag, diff + angleLag, (-1.0 / Tf) - stateData.cj);
    if (!inputLocs.empty()) {
        matrixData.assignCheckCol(diff + angleLag, inputLocs[0], 1.0 / Tf);
    }
    matrixData.assign(diff + angleWashout, diff + angleLag, 1.0 / Tw);
    matrixData.assign(diff + angleWashout, diff + angleWashout, (-1.0 / Tw) - stateData.cj);
    matrixData.assign(diff + frequencyWashout, diff + angleLag, gain / Tr);
    matrixData.assign(diff + frequencyWashout, diff + angleWashout, -gain / Tr);
    matrixData.assign(diff + frequencyWashout, diff + frequencyWashout, (-1.0 / Tr) - stateData.cj);
}

void BusROCOF::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime = time - prevTime;
    if (deltaTime < 0.0) {
        throw InvalidParameterValue("BusROCOF timestep precedes current time");
    }
    const std::array<double, 3> old{m_state[2], m_state[3], m_state[4]};
    const auto intermediate = [&](const std::array<double, 3>& slope, double scale) {
        std::array<double, 3> state{};
        for (index_t index = 0; index < 3; ++index) {
            state[index] = old[index] + (scale * deltaTime * slope[index]);
        }
        return state;
    };
    const double middleAngle = 0.5 * (lastAngle + inputs[0]);
    const auto initialSlope = rates(lastAngle, old.data());
    const auto secondStageState = intermediate(initialSlope, 0.5);
    const auto secondSlope = rates(middleAngle, secondStageState.data());
    const auto thirdStageState = intermediate(secondSlope, 0.5);
    const auto thirdSlope = rates(middleAngle, thirdStageState.data());
    const auto fourthStageState = intermediate(thirdSlope, 1.0);
    const auto fourthSlope = rates(inputs[0], fourthStageState.data());
    for (index_t index = 0; index < 3; ++index) {
        m_state[2 + index] += (deltaTime / 6.0) *
            (initialSlope[index] + (2.0 * secondSlope[index]) + (2.0 * thirdSlope[index]) +
             fourthSlope[index]);
    }
    const double frequencyDeviation = deviation(m_state.data() + 2);
    m_state[deviationOutput] = frequencyDeviation;
    m_state[rocofOutput] = (1.0 + frequencyDeviation - m_state[2 + frequencyWashout]) / Tr;
    lastAngle = inputs[0];
    prevTime = time;
}

stringVec BusROCOF::localStateNames() const
{
    return {"df", "dfdt", "angle_lag", "angle_washout", "frequency_washout"};
}

}  // namespace griddyn
