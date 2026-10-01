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
    constexpr index_t pe = 0, qe = 1, ip = 0, iq = 1, vf = 2;
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

void WT4G1::dynObjectInitializeA(CoreTime time0, std::uint32_t)
{
    const std::array<double, 9> values{
        TIqcmd, TIpCmd, Vlvpl1, Vlvpl2, Glvpl, Vhvrcr, Curhvrcr, RIpLvpl, TLvpl};
    if (std::any_of(values.begin(), values.end(), [](double v) { return !std::isfinite(v); }) ||
        TIqcmd <= 0.0 || TIpCmd <= 0.0 || TLvpl <= 0.0 || Vlvpl2 <= Vlvpl1 || Glvpl < 0.0 ||
        RIpLvpl < 0.0) {
        throw InvalidParameterValue("WT4G1 invalid time constants or limits");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = 3;
    local.jacSize = 30;
    prevTime = time0;
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
    m_state[pe] = desiredOutput[0];
    m_state[qe] = desiredOutput[1];
    m_state[2 + ip] = heldIp;
    m_state[2 + iq] = heldIq;
    m_state[2 + vf] = inputs[0];
    fieldSet = {heldIp, heldIq};
}

std::array<double, 3> WT4G1::rates(const IOdata& inputs, const double state[]) const
{
    const double voltage = inputs[0];
    const double ipcmd = inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : heldIp;
    const double iqcmd = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : heldIq;
    const double ipTarget = std::min(ipcmd, activeLimit(state[vf]));
    const double iqTarget = voltage > Vhvrcr ? Curhvrcr : iqcmd;
    return {std::min((ipTarget - state[ip]) / TIpCmd, RIpLvpl),
            (iqTarget - state[iq]) / TIqcmd,
            (voltage - state[vf]) / TLvpl};
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
        loc.destLoc[pe] =
            inputs[0] * activeVoltageLimit(inputs[0], loc.diffStateLoc[ip]) - loc.algStateLoc[pe];
        loc.destLoc[qe] = inputs[0] * loc.diffStateLoc[iq] - loc.algStateLoc[qe];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t i = 0; i < 3; ++i) {
            loc.destDiffLoc[i] -= loc.dstateLoc[i];
        }
    }
}

void WT4G1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double update[],
                            const SolverMode& sMode,
                            double)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[pe] = inputs[0] * activeVoltageLimit(inputs[0], loc.diffStateLoc[ip]);
    loc.destLoc[qe] = inputs[0] * loc.diffStateLoc[iq];
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
        const double current = loc.diffStateLoc[ip];
        matrixData.assign(loc.algOffset + pe, loc.algOffset + pe, -1.0);
        matrixData.assign(loc.algOffset + qe, loc.algOffset + qe, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset + pe,
                              loc.diffOffset + ip,
                              voltage < 0.4      ? 0.0 :
                                  voltage <= 0.8 ? 1.25 * voltage * voltage :
                                                   voltage);
            matrixData.assign(loc.algOffset + qe, loc.diffOffset + iq, voltage);
        }
        if (!inputLocs.empty()) {
            const auto f = [current](double v) { return v * activeVoltageLimit(v, current); };
            matrixData.assignCheckCol(loc.algOffset + pe,
                                      inputLocs[0],
                                      (f(voltage + step) - f(voltage - step)) / (2 * step));
            matrixData.assignCheckCol(loc.algOffset + qe, inputLocs[0], loc.diffStateLoc[iq]);
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
        const auto up = rates(inputs, plus.data());
        const auto down = rates(inputs, minus.data());
        for (index_t row = 0; row < 3; ++row) {
            matrixData.assign(loc.diffOffset + row,
                              loc.diffOffset + col,
                              (up[row] - down[row]) / (2 * step) -
                                  (row == col ? stateData.cj : 0.0));
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
        const auto up = rates(plus, loc.diffStateLoc);
        const auto down = rates(minus, loc.diffStateLoc);
        for (index_t row = 0; row < 3; ++row) {
            matrixData.assign(loc.diffOffset + row,
                              inputLocs[col],
                              (up[row] - down[row]) / (2 * step));
        }
    }
}

void WT4G1::timestep(CoreTime time, const IOdata& inputs, const SolverMode&)
{
    const double dt = time - prevTime;
    if (dt < 0.0) {
        throw InvalidParameterValue("WT4G1 timestep precedes current time");
    }
    const auto value = rates(inputs, m_state.data());
    for (index_t i = 0; i < 3; ++i) {
        m_state[2 + i] += dt * value[i];
    }
    m_state[pe] = inputs[0] * activeVoltageLimit(inputs[0], m_state[2 + ip]);
    m_state[qe] = inputs[0] * m_state[2 + iq];
    prevTime = time;
}

IOdata WT4G1::getOutputs(const IOdata&, const StateData& stateData, const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[pe], loc.algStateLoc[qe]};
}

void WT4G1::outputPartialDerivatives(const IOdata&,
                                     const StateData&,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(pe, alg + pe, 1.0);
    matrixData.assign(qe, alg + qe, 1.0);
}

stringVec WT4G1::localStateNames() const
{
    return {"Pe", "Qe", "Ip", "Iq", "Vf"};
}
}  // namespace griddyn
