/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WT3G1.h"

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
    constexpr std::array<RenewablePort, 4> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 1},
        {.signal = RenewableSignal::activeCurrentCommand,
         .ioIndex = 2,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveVoltageCommand,
         .ioIndex = 3,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 4> outputPortMap{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
        {.signal = RenewableSignal::initialActiveCurrentCommand,
         .ioIndex = 2,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::initialReactiveVoltageCommand,
         .ioIndex = 3,
         .base = RenewableBase::machine},
    }};
    constexpr index_t electricalActivePower = 0;
    constexpr index_t electricalReactivePower = 1;
    constexpr index_t activeCurrentState = 0;
    constexpr index_t reactiveVoltageState = 1;
    constexpr index_t pllIntegratorState = 2;
    constexpr index_t rotorAngleState = 3;
    constexpr double twoPi60 = 2.0 * 3.14159265358979323846 * 60.0;
}  // namespace

WT3G1::WT3G1(const std::string& objName): TerminalElectricalModel(objName)
{
    m_inputSize = 4;
    m_outputSize = 4;
}
CoreObject* WT3G1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<WT3G1, TerminalElectricalModel>(this, obj);
    if (out != nullptr) {
        out->Xeq = Xeq;
        out->Kpll = Kpll;
        out->Kipll = Kipll;
        out->Pllmax = Pllmax;
        out->Prated = Prated;
        out->heldIp = heldIp;
        out->heldEq = heldEq;
    }
    return out == nullptr ? obj : out;
}
std::span<const RenewablePort> WT3G1::inputPorts() const
{
    return inputPortMap;
}
std::span<const RenewablePort> WT3G1::outputPorts() const
{
    return outputPortMap;
}

void WT3G1::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "xeq") {
        Xeq = val;
    } else if (key == "kpll") {
        Kpll = val;
    } else if (key == "kipll") {
        Kipll = val;
    } else if (key == "pllmax") {
        Pllmax = val;
    } else if (key == "prated") {
        Prated = val;
    } else {
        TerminalElectricalModel::set(param, val, unitType);
    }
}
double WT3G1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "xeq") {
        return Xeq;
    }
    if (key == "kpll") {
        return Kpll;
    }
    if (key == "kipll") {
        return Kipll;
    }
    if (key == "pllmax") {
        return Pllmax;
    }
    if (key == "prated") {
        return Prated;
    }
    return TerminalElectricalModel::get(param, unitType);
}
void WT3G1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const std::array<double, 5> values{Xeq, Kpll, Kipll, Pllmax, Prated};
    if (std::any_of(values.begin(),
                    values.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        Xeq <= 0.0 || Pllmax <= 0.0 || Prated <= 0.0) {
        throw InvalidParameterValue("WT3G1 invalid reactance or PLL parameters");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = 4;
    local.jacSize = 48;
    prevTime = time0;
    (void)flags;
}
void WT3G1::dynObjectInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (inputs.size() < 2 || desiredOutput.size() < 2 || !std::isfinite(inputs[0]) ||
        inputs[0] <= 0.0 || !std::isfinite(inputs[1])) {
        throw InvalidParameterValue("WT3G1 requires terminal voltage, angle, and P/Q");
    }
    const double voltage = inputs[0];
    heldIp = desiredOutput[0] / voltage;
    const double reactiveCurrent = (desiredOutput[1] + (voltage * voltage / Xeq)) / voltage;
    heldEq = -Xeq * reactiveCurrent;
    m_state[electricalActivePower] = desiredOutput[0];
    m_state[electricalReactivePower] = desiredOutput[1];
    m_state[2 + activeCurrentState] = heldIp;
    m_state[2 + reactiveVoltageState] = heldEq;
    m_state[2 + pllIntegratorState] = 0.0;
    m_state[2 + rotorAngleState] = inputs[1];
    fieldSet = {heldIp, heldEq};
}
std::array<double, 2> WT3G1::power(const IOdata& inputs, const double state[]) const
{
    const double angle = inputs[1] - state[rotorAngleState];
    const double voltageReal = inputs[0] * std::cos(angle);
    const double voltageImag = inputs[0] * std::sin(angle);
    const double currentImag = -state[reactiveVoltageState] / Xeq;
    return {(voltageReal * state[activeCurrentState]) + (voltageImag * currentImag),
            (voltageReal * currentImag) - (voltageImag * state[activeCurrentState]) -
                (inputs[0] * inputs[0] / Xeq)};
}
std::array<double, 4> WT3G1::rates(const IOdata& inputs, const double state[]) const
{
    const double ipcmd = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : heldIp;
    const double eqcmd = inputs.size() > 3 && inputs[3] != kNullVal ? inputs[3] : heldEq;
    const double voltageImag = inputs[0] * std::sin(inputs[1] - state[rotorAngleState]);
    const double pllSignal =
        std::clamp(state[pllIntegratorState] + (Kpll * voltageImag / twoPi60), -Pllmax, Pllmax);
    double pllIntegral = Kipll * pllSignal;
    if ((state[pllIntegratorState] <= -Pllmax && pllSignal < 0.0) ||
        (state[pllIntegratorState] >= Pllmax && pllSignal > 0.0)) {
        pllIntegral = 0.0;
    }
    return {(ipcmd - state[activeCurrentState]) / 0.02,
            (eqcmd - state[reactiveVoltageState]) / 0.02,
            pllIntegral,
            twoPi60 * pllSignal};
}
void WT3G1::derivative(const IOdata& inputs,
                       const StateData& stateData,
                       double deriv[],
                       const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto stateRates = rates(inputs, loc.diffStateLoc);
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        loc.destDiffLoc[stateIndex] = stateRates[stateIndex];
    }
}
void WT3G1::residual(const IOdata& inputs,
                     const StateData& stateData,
                     double resid[],
                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto powerValues = power(inputs, loc.diffStateLoc);
        loc.destLoc[electricalActivePower] =
            powerValues[0] - loc.algStateLoc[electricalActivePower];
        loc.destLoc[electricalReactivePower] =
            powerValues[1] - loc.algStateLoc[electricalReactivePower];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
            loc.destDiffLoc[stateIndex] -= loc.dstateLoc[stateIndex];
        }
    }
}
void WT3G1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double update[],
                            const SolverMode& sMode,
                            double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const auto powerValues = power(inputs, loc.diffStateLoc);
    loc.destLoc[electricalActivePower] = powerValues[0];
    loc.destLoc[electricalReactivePower] = powerValues[1];
    (void)alpha;
}
void WT3G1::jacobianElements(const IOdata& inputs,
                             const StateData& stateData,
                             MatrixData<double>& matrixData,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    constexpr double step = 1e-6;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset + electricalActivePower,
                          loc.algOffset + electricalActivePower,
                          -1.0);
        matrixData.assign(loc.algOffset + electricalReactivePower,
                          loc.algOffset + electricalReactivePower,
                          -1.0);
        if (hasDifferential(sMode)) {
            for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
                auto plus = std::array<double, 4>{loc.diffStateLoc[0],
                                                  loc.diffStateLoc[1],
                                                  loc.diffStateLoc[2],
                                                  loc.diffStateLoc[3]};
                auto minus = plus;
                plus[stateIndex] += step;
                minus[stateIndex] -= step;
                const auto upper = power(inputs, plus.data());
                const auto lower = power(inputs, minus.data());
                for (index_t row = 0; row < 2; ++row) {
                    matrixData.assign(loc.algOffset + row,
                                      loc.diffOffset + stateIndex,
                                      (upper[row] - lower[row]) / (2 * step));
                }
            }
        }
    }
    if (hasDifferential(sMode)) {
        for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
            auto plus = std::array<double, 4>{loc.diffStateLoc[0],
                                              loc.diffStateLoc[1],
                                              loc.diffStateLoc[2],
                                              loc.diffStateLoc[3]};
            auto minus = plus;
            plus[stateIndex] += step;
            minus[stateIndex] -= step;
            const auto upper = rates(inputs, plus.data());
            const auto lower = rates(inputs, minus.data());
            for (index_t row = 0; row < 4; ++row) {
                matrixData.assign(loc.diffOffset + row,
                                  loc.diffOffset + stateIndex,
                                  ((upper[row] - lower[row]) / (2 * step)) -
                                      (row == stateIndex ? stateData.cj : 0.0));
            }
        }
    }
    for (std::size_t inputIndex = 0; inputIndex < inputs.size() && inputIndex < inputLocs.size();
         ++inputIndex) {
        if (inputLocs[inputIndex] == kNullLocation || inputs[inputIndex] == kNullVal) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[inputIndex] += step;
        minus[inputIndex] -= step;
        if (hasDifferential(sMode)) {
            const auto upper = rates(plus, loc.diffStateLoc);
            const auto lower = rates(minus, loc.diffStateLoc);
            for (index_t row = 0; row < 4; ++row) {
                matrixData.assign(loc.diffOffset + row,
                                  inputLocs[inputIndex],
                                  (upper[row] - lower[row]) / (2 * step));
            }
        }
        if (hasAlgebraic(sMode)) {
            const auto powerUpper = power(plus, loc.diffStateLoc);
            const auto powerLower = power(minus, loc.diffStateLoc);
            for (index_t row = 0; row < 2; ++row) {
                matrixData.assign(loc.algOffset + row,
                                  inputLocs[inputIndex],
                                  (powerUpper[row] - powerLower[row]) / (2 * step));
            }
        }
    }
}
void WT3G1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    const double timeStep = time - prevTime;
    if (timeStep < 0.0) {
        throw InvalidParameterValue("WT3G1 timestep precedes current time");
    }
    const auto stateRates = rates(inputs, m_state.data() + 2);
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        m_state[2 + stateIndex] += timeStep * stateRates[stateIndex];
    }
    const auto powerValues = power(inputs, m_state.data() + 2);
    m_state[electricalActivePower] = powerValues[0];
    m_state[electricalReactivePower] = powerValues[1];
    prevTime = time;
    (void)sMode;
}
IOdata WT3G1::getOutputs(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    (void)inputs;
    return {loc.algStateLoc[electricalActivePower],
            loc.algStateLoc[electricalReactivePower],
            heldIp,
            heldEq};
}
double WT3G1::getOutput(const IOdata& inputs,
                        const StateData& stateData,
                        const SolverMode& sMode,
                        index_t outputNum) const
{
    return getOutputs(inputs, stateData, sMode).at(static_cast<std::size_t>(outputNum));
}
index_t WT3G1::getOutputLoc(const SolverMode& sMode, index_t outputNum) const
{
    return outputNum < 2 ? offsets.getAlgOffset(sMode) + outputNum : kNullLocation;
}
void WT3G1::outputPartialDerivatives(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(electricalActivePower, alg + electricalActivePower, 1.0);
    matrixData.assign(electricalReactivePower, alg + electricalReactivePower, 1.0);
    (void)inputs;
    (void)stateData;
}
stringVec WT3G1::localStateNames() const
{
    return {"Pe", "Qe", "Ip", "Eq", "PLLint", "delta"};
}
}  // namespace griddyn
