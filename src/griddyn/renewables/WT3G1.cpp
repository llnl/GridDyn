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
    constexpr index_t pe = 0, qe = 1, ip = 0, eq = 1, pllInt = 2, delta = 3;
    constexpr double twoPi60 = 2.0 * 3.14159265358979323846 * 60.0;
}  // namespace

WT3G1::WT3G1(const std::string& name): TerminalElectricalModel(name)
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
void WT3G1::dynObjectInitializeA(CoreTime time0, std::uint32_t)
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
    const double iq = (desiredOutput[1] + voltage * voltage / Xeq) / voltage;
    heldEq = -Xeq * iq;
    m_state[pe] = desiredOutput[0];
    m_state[qe] = desiredOutput[1];
    m_state[2 + ip] = heldIp;
    m_state[2 + eq] = heldEq;
    m_state[2 + pllInt] = 0.0;
    m_state[2 + delta] = inputs[1];
    fieldSet = {heldIp, heldEq};
}
std::array<double, 2> WT3G1::power(const IOdata& inputs, const double state[]) const
{
    const double angle = inputs[1] - state[delta];
    const double vx = inputs[0] * std::cos(angle);
    const double vy = inputs[0] * std::sin(angle);
    const double iy = -state[eq] / Xeq;
    return {vx * state[ip] + vy * iy, vx * iy - vy * state[ip] - (inputs[0] * inputs[0] / Xeq)};
}
std::array<double, 4> WT3G1::rates(const IOdata& inputs, const double state[]) const
{
    const double ipcmd = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : heldIp;
    const double eqcmd = inputs.size() > 3 && inputs[3] != kNullVal ? inputs[3] : heldEq;
    const double vy = inputs[0] * std::sin(inputs[1] - state[delta]);
    const double pll = std::clamp(state[pllInt] + Kpll * vy / twoPi60, -Pllmax, Pllmax);
    const double integral = state[pllInt] <= -Pllmax && pll < 0.0 ? 0.0 :
        state[pllInt] >= Pllmax && pll > 0.0                      ? 0.0 :
                                                                    Kipll * pll;
    return {(ipcmd - state[ip]) / 0.02, (eqcmd - state[eq]) / 0.02, integral, twoPi60 * pll};
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
    const auto r = rates(inputs, loc.diffStateLoc);
    for (index_t i = 0; i < 4; ++i) {
        loc.destDiffLoc[i] = r[i];
    }
}
void WT3G1::residual(const IOdata& inputs,
                     const StateData& stateData,
                     double resid[],
                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto pq = power(inputs, loc.diffStateLoc);
        loc.destLoc[pe] = pq[0] - loc.algStateLoc[pe];
        loc.destLoc[qe] = pq[1] - loc.algStateLoc[qe];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t i = 0; i < 4; ++i) {
            loc.destDiffLoc[i] -= loc.dstateLoc[i];
        }
    }
}
void WT3G1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double update[],
                            const SolverMode& sMode,
                            double)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const auto pq = power(inputs, loc.diffStateLoc);
    loc.destLoc[pe] = pq[0];
    loc.destLoc[qe] = pq[1];
}
void WT3G1::jacobianElements(const IOdata& inputs,
                             const StateData& stateData,
                             MatrixData<double>& matrixData,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    constexpr double h = 1e-6;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset + pe, loc.algOffset + pe, -1.0);
        matrixData.assign(loc.algOffset + qe, loc.algOffset + qe, -1.0);
        for (index_t c = 0; c < 4; ++c) {
            auto plus = std::array<double, 4>{loc.diffStateLoc[0],
                                              loc.diffStateLoc[1],
                                              loc.diffStateLoc[2],
                                              loc.diffStateLoc[3]};
            auto minus = plus;
            plus[c] += h;
            minus[c] -= h;
            const auto up = power(inputs, plus.data()), down = power(inputs, minus.data());
            for (index_t row = 0; row < 2; ++row) {
                matrixData.assign(loc.algOffset + row,
                                  loc.diffOffset + c,
                                  (up[row] - down[row]) / (2 * h));
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t c = 0; c < 4; ++c) {
        auto plus = std::array<double, 4>{loc.diffStateLoc[0],
                                          loc.diffStateLoc[1],
                                          loc.diffStateLoc[2],
                                          loc.diffStateLoc[3]};
        auto minus = plus;
        plus[c] += h;
        minus[c] -= h;
        const auto up = rates(inputs, plus.data()), down = rates(inputs, minus.data());
        for (index_t row = 0; row < 4; ++row) {
            matrixData.assign(loc.diffOffset + row,
                              loc.diffOffset + c,
                              (up[row] - down[row]) / (2 * h) - (row == c ? stateData.cj : 0.0));
        }
    }
    for (std::size_t c = 0; c < inputs.size() && c < inputLocs.size(); ++c) {
        if (inputLocs[c] == kNullLocation || inputs[c] == kNullVal) {
            continue;
        }
        auto plus = inputs, minus = inputs;
        plus[c] += h;
        minus[c] -= h;
        const auto up = rates(plus, loc.diffStateLoc), down = rates(minus, loc.diffStateLoc);
        for (index_t row = 0; row < 4; ++row) {
            matrixData.assign(loc.diffOffset + row, inputLocs[c], (up[row] - down[row]) / (2 * h));
        }
        if (hasAlgebraic(sMode)) {
            const auto pup = power(plus, loc.diffStateLoc), pdown = power(minus, loc.diffStateLoc);
            for (index_t row = 0; row < 2; ++row) {
                matrixData.assign(loc.algOffset + row,
                                  inputLocs[c],
                                  (pup[row] - pdown[row]) / (2 * h));
            }
        }
    }
}
void WT3G1::timestep(CoreTime time, const IOdata& inputs, const SolverMode&)
{
    const double dt = time - prevTime;
    if (dt < 0.0) {
        throw InvalidParameterValue("WT3G1 timestep precedes current time");
    }
    const auto r = rates(inputs, m_state.data() + 2);
    for (index_t i = 0; i < 4; ++i) {
        m_state[2 + i] += dt * r[i];
    }
    const auto pq = power(inputs, m_state.data() + 2);
    m_state[pe] = pq[0];
    m_state[qe] = pq[1];
    prevTime = time;
}
IOdata WT3G1::getOutputs(const IOdata&, const StateData& stateData, const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[pe], loc.algStateLoc[qe], heldIp, heldEq};
}
double WT3G1::getOutput(const IOdata&,
                        const StateData& stateData,
                        const SolverMode& sMode,
                        index_t index) const
{
    return getOutputs({}, stateData, sMode).at(static_cast<std::size_t>(index));
}
index_t WT3G1::getOutputLoc(const SolverMode& sMode, index_t index) const
{
    return index < 2 ? offsets.getAlgOffset(sMode) + index : kNullLocation;
}
void WT3G1::outputPartialDerivatives(const IOdata&,
                                     const StateData&,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(pe, alg + pe, 1.0);
    matrixData.assign(qe, alg + qe, 1.0);
}
stringVec WT3G1::localStateNames() const
{
    return {"Pe", "Qe", "Ip", "Eq", "PLLint", "delta"};
}
}  // namespace griddyn
