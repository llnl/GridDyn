/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WT4G1.h"

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
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::activeCurrentCommand,
         .ioIndex = 1,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveCurrentCommand,
         .ioIndex = 2,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 2> outputPortMap{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};
    constexpr index_t activePowerOutput = 0;
    constexpr index_t reactivePowerOutput = 1;
    constexpr index_t activeCurrentState = 0;
    constexpr index_t reactiveCurrentState = 1;
    constexpr index_t voltageFilterState = 2;

    double activeCurrentGain(double voltage)
    {
        if (voltage < 0.4) {
            return 0.0;
        }
        if (voltage <= 0.8) {
            return 1.25 * voltage * voltage;
        }
        return voltage;
    }
}  // namespace

WT4G1::WT4G1(const std::string& name): TerminalElectricalModel(name)
{
    m_inputSize = 3;
}

CoreObject* WT4G1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<WT4G1, TerminalElectricalModel>(this, obj);
    if (out != nullptr) {
        out->TIqcmd = TIqcmd;
        out->TIpCmd = TIpCmd;
        out->Vlvpl1 = Vlvpl1;
        out->Vlvpl2 = Vlvpl2;
        out->Glvpl = Glvpl;
        out->Vhvrcr = Vhvrcr;
        out->Curhvrcr = Curhvrcr;
        out->RIpLvpl = RIpLvpl;
        out->TLvpl = TLvpl;
        out->heldIp = heldIp;
        out->heldIq = heldIq;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> WT4G1::inputPorts() const
{
    return inputPortMap;
}
std::span<const RenewablePort> WT4G1::outputPorts() const
{
    return outputPortMap;
}

void WT4G1::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tiqcmd") {
        TIqcmd = val;
    } else if (key == "tipcmd") {
        TIpCmd = val;
    } else if (key == "vlvpl1") {
        Vlvpl1 = val;
    } else if (key == "vlvpl2") {
        Vlvpl2 = val;
    } else if (key == "glvpl") {
        Glvpl = val;
    } else if (key == "vhvrcr") {
        Vhvrcr = val;
    } else if (key == "curhvrcr") {
        Curhvrcr = val;
    } else if (key == "riplvpl") {
        RIpLvpl = val;
    } else if (key == "tlvpl") {
        TLvpl = val;
    } else {
        TerminalElectricalModel::set(param, val, unitType);
    }
}

double WT4G1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tiqcmd") {
        return TIqcmd;
    }
    if (key == "tipcmd") {
        return TIpCmd;
    }
    if (key == "vlvpl1") {
        return Vlvpl1;
    }
    if (key == "vlvpl2") {
        return Vlvpl2;
    }
    if (key == "glvpl") {
        return Glvpl;
    }
    if (key == "vhvrcr") {
        return Vhvrcr;
    }
    if (key == "curhvrcr") {
        return Curhvrcr;
    }
    if (key == "riplvpl") {
        return RIpLvpl;
    }
    if (key == "tlvpl") {
        return TLvpl;
    }
    return TerminalElectricalModel::get(param, unitType);
}

void WT4G1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const std::array<double, 9> values{
        TIqcmd, TIpCmd, Vlvpl1, Vlvpl2, Glvpl, Vhvrcr, Curhvrcr, RIpLvpl, TLvpl};
    if (std::any_of(values.begin(),
                    values.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        TIqcmd <= 0.0 || TIpCmd <= 0.0 || TLvpl <= 0.0 || Vlvpl2 <= Vlvpl1 || Glvpl < 0.0 ||
        RIpLvpl < 0.0) {
        throw InvalidParameterValue("WT4G1 invalid time constants or limits");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = 3;
    local.jacSize = 30;
    prevTime = time0;
    (void)flags;
}

double WT4G1::activeLimit(double voltage) const
{
    if (voltage < Vlvpl1) {
        return 0.0;
    }
    if (voltage > Vlvpl2) {
        return kBigNum;
    }
    return Glvpl * (voltage - Vlvpl1) / (Vlvpl2 - Vlvpl1);
}

double WT4G1::activeVoltageLimit(double voltage, double current)
{
    if (voltage < 0.4) {
        return 0.0;
    }
    return voltage > 0.8 ? current : 1.25 * voltage * current;
}

void WT4G1::dynObjectInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (inputs.empty() || desiredOutput.size() < 2 || !std::isfinite(inputs[0]) ||
        inputs[0] <= 0.0) {
        throw InvalidParameterValue("WT4G1 requires finite terminal voltage and P/Q");
    }
    heldIp = desiredOutput[0] / inputs[0];
    heldIq = desiredOutput[1] / inputs[0];
    m_state[activePowerOutput] = desiredOutput[0];
    m_state[reactivePowerOutput] = desiredOutput[1];
    m_state[2 + activeCurrentState] = heldIp;
    m_state[2 + reactiveCurrentState] = heldIq;
    m_state[2 + voltageFilterState] = inputs[0];
    fieldSet = {heldIp, heldIq};
}

std::array<double, 3> WT4G1::rates(const IOdata& inputs, const double state[]) const
{
    const double voltage = inputs[0];
    const double activeCurrentCommand =
        inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : heldIp;
    const double reactiveCurrentCommand =
        inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : heldIq;
    const double activeCurrentTarget =
        std::min(activeCurrentCommand, activeLimit(state[voltageFilterState]));
    const double reactiveCurrentTarget = voltage > Vhvrcr ? Curhvrcr : reactiveCurrentCommand;
    return {std::min((activeCurrentTarget - state[activeCurrentState]) / TIpCmd, RIpLvpl),
            (reactiveCurrentTarget - state[reactiveCurrentState]) / TIqcmd,
            (voltage - state[voltageFilterState]) / TLvpl};
}

void WT4G1::derivative(const IOdata& inputs,
                       const StateData& stateData,
                       double deriv[],
                       const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = rates(inputs, loc.diffStateLoc);
    for (index_t index = 0; index < 3; ++index) {
        loc.destDiffLoc[index] = result[index];
    }
}

void WT4G1::residual(const IOdata& inputs,
                     const StateData& stateData,
                     double resid[],
                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[activePowerOutput] =
            (inputs[0] * activeVoltageLimit(inputs[0], loc.diffStateLoc[activeCurrentState])) -
            loc.algStateLoc[activePowerOutput];
        loc.destLoc[reactivePowerOutput] = (inputs[0] * loc.diffStateLoc[reactiveCurrentState]) -
            loc.algStateLoc[reactivePowerOutput];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t stateIndex = 0; stateIndex < 3; ++stateIndex) {
            loc.destDiffLoc[stateIndex] -= loc.dstateLoc[stateIndex];
        }
    }
}

void WT4G1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double update[],
                            const SolverMode& sMode,
                            double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[activePowerOutput] =
        inputs[0] * activeVoltageLimit(inputs[0], loc.diffStateLoc[activeCurrentState]);
    loc.destLoc[reactivePowerOutput] = inputs[0] * loc.diffStateLoc[reactiveCurrentState];
    (void)alpha;
}

void WT4G1::jacobianElements(const IOdata& inputs,
                             const StateData& stateData,
                             MatrixData<double>& matrixData,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    constexpr double step = 1e-6;
    if (hasAlgebraic(sMode)) {
        const double voltage = inputs[0];
        const double current = loc.diffStateLoc[activeCurrentState];
        matrixData.assign(loc.algOffset + activePowerOutput,
                          loc.algOffset + activePowerOutput,
                          -1.0);
        matrixData.assign(loc.algOffset + reactivePowerOutput,
                          loc.algOffset + reactivePowerOutput,
                          -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset + activePowerOutput,
                              loc.diffOffset + activeCurrentState,
                              activeCurrentGain(voltage));
            matrixData.assign(loc.algOffset + reactivePowerOutput,
                              loc.diffOffset + reactiveCurrentState,
                              voltage);
        }
        if (!inputLocs.empty()) {
            const auto activePower = [current](double voltageValue) {
                return voltageValue * activeVoltageLimit(voltageValue, current);
            };
            matrixData.assignCheckCol(loc.algOffset + activePowerOutput,
                                      inputLocs[0],
                                      (activePower(voltage + step) - activePower(voltage - step)) /
                                          (2 * step));
            matrixData.assignCheckCol(loc.algOffset + reactivePowerOutput,
                                      inputLocs[0],
                                      loc.diffStateLoc[reactiveCurrentState]);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t col = 0; col < 3; ++col) {
        auto plus =
            std::array<double, 3>{loc.diffStateLoc[0], loc.diffStateLoc[1], loc.diffStateLoc[2]};
        auto minus = plus;
        plus[col] += step;
        minus[col] -= step;
        const auto upperRates = rates(inputs, plus.data());
        const auto lowerRates = rates(inputs, minus.data());
        for (index_t rowIndex = 0; rowIndex < 3; ++rowIndex) {
            matrixData.assign(loc.diffOffset + rowIndex,
                              loc.diffOffset + col,
                              ((upperRates[rowIndex] - lowerRates[rowIndex]) / (2 * step)) -
                                  (rowIndex == col ? stateData.cj : 0.0));
        }
    }
    for (std::size_t col = 0; col < inputs.size() && col < inputLocs.size(); ++col) {
        if (inputLocs[col] == kNullLocation || inputs[col] == kNullVal) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[col] += step;
        minus[col] -= step;
        const auto upperRates = rates(plus, loc.diffStateLoc);
        const auto lowerRates = rates(minus, loc.diffStateLoc);
        for (index_t rowIndex = 0; rowIndex < 3; ++rowIndex) {
            matrixData.assign(loc.diffOffset + rowIndex,
                              inputLocs[col],
                              (upperRates[rowIndex] - lowerRates[rowIndex]) / (2 * step));
        }
    }
}

void WT4G1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    const double timeStep = time - prevTime;
    if (timeStep < 0.0) {
        throw InvalidParameterValue("WT4G1 timestep precedes current time");
    }
    const auto stateRates = rates(inputs, m_state.data());
    for (index_t stateIndex = 0; stateIndex < 3; ++stateIndex) {
        m_state[2 + stateIndex] += timeStep * stateRates[stateIndex];
    }
    m_state[activePowerOutput] =
        inputs[0] * activeVoltageLimit(inputs[0], m_state[2 + activeCurrentState]);
    m_state[reactivePowerOutput] = inputs[0] * m_state[2 + reactiveCurrentState];
    prevTime = time;
    (void)sMode;
}
IOdata WT4G1::getOutputs(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    (void)inputs;
    return {loc.algStateLoc[activePowerOutput], loc.algStateLoc[reactivePowerOutput]};
}

void WT4G1::outputPartialDerivatives(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(activePowerOutput, alg + activePowerOutput, 1.0);
    matrixData.assign(reactivePowerOutput, alg + reactivePowerOutput, 1.0);
    (void)inputs;
    (void)stateData;
}

stringVec WT4G1::localStateNames() const
{
    return {"Pe", "Qe", "Ip", "Iq", "Vf"};
}
}  // namespace griddyn
