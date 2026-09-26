/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "REGCA1.h"

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
    constexpr index_t activePower = 0;
    constexpr index_t reactivePower = 1;
    constexpr index_t activeCurrentState = 0;
    constexpr index_t reactiveCurrentState = 1;
    constexpr index_t filteredVoltage = 2;
}  // namespace

REGCA1::REGCA1(const std::string& name): TerminalElectricalModel(name)
{
    m_inputSize = 3;
}

CoreObject* REGCA1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REGCA1, TerminalElectricalModel>(this, obj);
    if (out != nullptr) {
        out->Tg = Tg;
        out->Rrpwr = Rrpwr;
        out->Brkpt = Brkpt;
        out->Zerox = Zerox;
        out->Lvpl1 = Lvpl1;
        out->Volim = Volim;
        out->Lvpnt1 = Lvpnt1;
        out->Lvpnt0 = Lvpnt0;
        out->Iolim = Iolim;
        out->Tfltr = Tfltr;
        out->Khv = Khv;
        out->Iqrmax = Iqrmax;
        out->Iqrmin = Iqrmin;
        out->Accel = Accel;
        out->initialReactivePower = initialReactivePower;
        out->Lvplsw = Lvplsw;
        out->heldIpCommand = heldIpCommand;
        out->heldIqCommand = heldIqCommand;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> REGCA1::inputPorts() const
{
    return inputPortMap;
}
std::span<const RenewablePort> REGCA1::outputPorts() const
{
    return outputPortMap;
}

void REGCA1::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tg") {
        Tg = val;
    } else if (key == "rrpwr") {
        Rrpwr = val;
    } else if (key == "brkpt") {
        Brkpt = val;
    } else if (key == "zerox") {
        Zerox = val;
    } else if (key == "lvpl1") {
        Lvpl1 = val;
    } else if (key == "volim") {
        Volim = val;
    } else if (key == "lvpnt1") {
        Lvpnt1 = val;
    } else if (key == "lvpnt0") {
        Lvpnt0 = val;
    } else if (key == "iolim") {
        Iolim = val;
    } else if (key == "tfltr") {
        Tfltr = val;
    } else if (key == "khv") {
        Khv = val;
    } else if (key == "iqrmax") {
        Iqrmax = val;
    } else if (key == "iqrmin") {
        Iqrmin = val;
    } else if (key == "lvplsw") {
        if (val != 0.0 && val != 1.0) {
            throw InvalidParameterValue("REGCA1 Lvplsw must be zero or one");
        }
        Lvplsw = val == 1.0;
    } else if (key == "accel") {
        Accel = val;
    } else {
        TerminalElectricalModel::set(param, val, unitType);
    }
}

double REGCA1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tg") {
        return Tg;
    }
    if (key == "rrpwr") {
        return Rrpwr;
    }
    if (key == "brkpt") {
        return Brkpt;
    }
    if (key == "zerox") {
        return Zerox;
    }
    if (key == "lvpl1") {
        return Lvpl1;
    }
    if (key == "volim") {
        return Volim;
    }
    if (key == "lvpnt1") {
        return Lvpnt1;
    }
    if (key == "lvpnt0") {
        return Lvpnt0;
    }
    if (key == "iolim") {
        return Iolim;
    }
    if (key == "tfltr") {
        return Tfltr;
    }
    if (key == "khv") {
        return Khv;
    }
    if (key == "iqrmax") {
        return Iqrmax;
    }
    if (key == "iqrmin") {
        return Iqrmin;
    }
    if (key == "lvplsw") {
        return Lvplsw ? 1.0 : 0.0;
    }
    if (key == "accel") {
        return Accel;
    }
    return TerminalElectricalModel::get(param, unitType);
}

void REGCA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 13> params{
        Tg, Rrpwr, Brkpt, Zerox, Lvpl1, Volim, Lvpnt1, Lvpnt0, Iolim, Tfltr, Khv, Iqrmax, Iqrmin};
    if (std::any_of(params.begin(),
                    params.end(),
                    [](double stateValue) { return !std::isfinite(stateValue); }) ||
        Tg <= 0.0 || Tfltr <= 0.0 || Rrpwr <= 0.0 || Brkpt <= Zerox || Lvpnt1 <= Lvpnt0 ||
        Lvpl1 < 0.0 || !std::isfinite(Accel) || Accel < 0.0 || Accel > 1.0) {
        throw InvalidParameterValue("REGCA1 gains, limits or time constants");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = 3;
    local.jacSize = 24;
    prevTime = time0;
}

double REGCA1::lowVoltageGain(double voltage) const
{
    return std::clamp((voltage - Lvpnt0) / (Lvpnt1 - Lvpnt0), 0.0, 1.0);
}

double REGCA1::lowVoltagePowerLimit(double voltage) const
{
    if (!Lvplsw || voltage >= Brkpt) {
        return kBigNum;
    }
    if (voltage <= Zerox) {
        return 0.0;
    }
    return Lvpl1 * (voltage - Zerox) / (Brkpt - Zerox);
}

double REGCA1::activeCommand(const IOdata& inputs) const
{
    return inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : heldIpCommand;
}

double REGCA1::reactiveCommand(const IOdata& inputs) const
{
    return inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : heldIqCommand;
}

double REGCA1::limitReactiveRate(double rate) const
{
    // REGCA1 applies the recovery slew limit only in the direction selected
    // by the initial reactive power. A nonpositive upper limit or nonnegative
    // lower limit disables that recovery direction in PowerWorld's REGC_A.
    if (initialReactivePower > 0.0 && Iqrmax > 0.0) {
        return std::min(rate, Iqrmax);
    }
    if (initialReactivePower < 0.0 && Iqrmin < 0.0) {
        return std::max(rate, Iqrmin);
    }
    return rate;
}

bool REGCA1::reactiveRateFree(double rate) const
{
    return (initialReactivePower <= 0.0 || Iqrmax <= 0.0 || rate < Iqrmax) &&
        (initialReactivePower >= 0.0 || Iqrmin >= 0.0 || rate > Iqrmin);
}

void REGCA1::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet)
{
    if (inputs.empty() || desiredOutput.size() < 2 || !std::isfinite(inputs[0]) ||
        inputs[0] <= 0.0 || !std::isfinite(desiredOutput[0]) || !std::isfinite(desiredOutput[1])) {
        throw InvalidParameterValue("REGCA1 terminal voltage or initial P/Q");
    }
    const double voltage = inputs[0];
    const double gain = lowVoltageGain(voltage);
    if (gain == 0.0 && desiredOutput[0] != 0.0) {
        throw InvalidParameterValue("REGCA1 initial active power at zero low-voltage gain");
    }
    const double activeCurrentInitial = gain == 0.0 ? 0.0 : desiredOutput[0] / (voltage * gain);
    const double reactiveCurrentInitial = desiredOutput[1] / voltage;
    heldIpCommand = activeCurrentInitial;
    heldIqCommand = -reactiveCurrentInitial;
    initialReactivePower = desiredOutput[1];
    m_state[activePower] = desiredOutput[0];
    m_state[reactivePower] = desiredOutput[1];
    m_state[2 + activeCurrentState] = activeCurrentInitial;
    m_state[2 + reactiveCurrentState] = reactiveCurrentInitial;
    m_state[2 + filteredVoltage] = voltage;
    fieldSet = {activeCurrentInitial, -reactiveCurrentInitial};
}

void REGCA1::derivative(const IOdata& inputs,
                        const StateData& stateData,
                        double deriv[],
                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    const double ipTarget =
        std::min(activeCommand(inputs), lowVoltagePowerLimit(state[filteredVoltage]));
    const double iqTarget =
        std::max(Iolim, -reactiveCommand(inputs) - (Khv * std::max(inputs[0] - Volim, 0.0)));
    loc.destDiffLoc[activeCurrentState] =
        std::min((ipTarget - state[activeCurrentState]) / Tg, Rrpwr);
    loc.destDiffLoc[reactiveCurrentState] =
        limitReactiveRate((iqTarget - state[reactiveCurrentState]) / Tg);
    loc.destDiffLoc[filteredVoltage] = (inputs[0] - state[filteredVoltage]) / Tfltr;
}

void REGCA1::residual(const IOdata& inputs,
                      const StateData& stateData,
                      double resid[],
                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[activePower] =
            (inputs[0] * loc.diffStateLoc[activeCurrentState] * lowVoltageGain(inputs[0])) -
            loc.algStateLoc[activePower];
        loc.destLoc[reactivePower] =
            (inputs[0] * loc.diffStateLoc[reactiveCurrentState]) -
            loc.algStateLoc[reactivePower];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void REGCA1::algebraicUpdate(const IOdata& inputs,
                             const StateData& stateData,
                             double update[],
                             const SolverMode& sMode,
                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[activePower] =
        inputs[0] * loc.diffStateLoc[activeCurrentState] * lowVoltageGain(inputs[0]);
    loc.destLoc[reactivePower] = inputs[0] * loc.diffStateLoc[reactiveCurrentState];
}

void REGCA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime = time - prevTime;
    if (deltaTime < 0.0) {
        throw InvalidParameterValue("REGCA1 timestep precedes current time");
    }
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    for (index_t index = 0; index < 3; ++index) {
        m_state[2 + index] += deltaTime * m_dstate_dt[2 + index];
    }
    m_state[activePower] = inputs[0] * m_state[2 + activeCurrentState] * lowVoltageGain(inputs[0]);
    m_state[reactivePower] = inputs[0] * m_state[2 + reactiveCurrentState];
    prevTime = time;
}

void REGCA1::jacobianElements(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double* state = loc.diffStateLoc;
    const index_t alg = loc.algOffset;
    const index_t diff = loc.diffOffset;
    const double voltage = inputs[0];
    const double gain = lowVoltageGain(voltage);
    const double gainSlope = voltage > Lvpnt0 && voltage < Lvpnt1 ? 1.0 / (Lvpnt1 - Lvpnt0) : 0.0;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg + activePower, alg + activePower, -1.0);
        matrixData.assign(alg + reactivePower, alg + reactivePower, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(alg + activePower, diff + activeCurrentState, voltage * gain);
            matrixData.assign(alg + reactivePower, diff + reactiveCurrentState, voltage);
        }
        matrixData.assignCheckCol(alg + activePower,
                                  inputLocs[0],
                                  state[activeCurrentState] * (gain + (voltage * gainSlope)));
        matrixData.assignCheckCol(alg + reactivePower, inputLocs[0], state[reactiveCurrentState]);
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double ipLimit = lowVoltagePowerLimit(state[filteredVoltage]);
    const double ipTarget = std::min(activeCommand(inputs), ipLimit);
    const double ipRate = (ipTarget - state[activeCurrentState]) / Tg;
    matrixData.assign(diff + activeCurrentState,
                      diff + activeCurrentState,
                      (ipRate < Rrpwr ? -1.0 / Tg : 0.0) - stateData.cj);
    if (ipRate < Rrpwr) {
        if (activeCommand(inputs) <= ipLimit) {
            if (inputLocs.size() > 1 && inputs.size() > 1 && inputs[1] != kNullVal) {
                matrixData.assignCheckCol(diff + activeCurrentState, inputLocs[1], 1.0 / Tg);
            }
        } else if (state[filteredVoltage] > Zerox && state[filteredVoltage] < Brkpt) {
            matrixData.assign(diff + activeCurrentState,
                              diff + filteredVoltage,
                              Lvpl1 / ((Brkpt - Zerox) * Tg));
        }
    }
    const double iqRaw = -reactiveCommand(inputs) - (Khv * std::max(voltage - Volim, 0.0));
    const double iqTarget = std::max(Iolim, iqRaw);
    const double iqRate = (iqTarget - state[reactiveCurrentState]) / Tg;
    const bool iqFree = reactiveRateFree(iqRate);
    matrixData.assign(diff + reactiveCurrentState,
                      diff + reactiveCurrentState,
                      (iqFree ? -1.0 / Tg : 0.0) - stateData.cj);
    if (iqFree && iqRaw > Iolim) {
        if (inputLocs.size() > 2 && inputs.size() > 2 && inputs[2] != kNullVal) {
            matrixData.assignCheckCol(diff + reactiveCurrentState, inputLocs[2], -1.0 / Tg);
        }
        if (voltage > Volim) {
            matrixData.assignCheckCol(diff + reactiveCurrentState, inputLocs[0], -Khv / Tg);
        }
    }
    matrixData.assign(diff + filteredVoltage,
                      diff + filteredVoltage,
                      (-1.0 / Tfltr) - stateData.cj);
    matrixData.assignCheckCol(diff + filteredVoltage, inputLocs[0], 1.0 / Tfltr);
}

IOdata REGCA1::getOutputs(const IOdata& /*inputs*/,
                          const StateData& stateData,
                          const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[activePower], loc.algStateLoc[reactivePower]};
}

void REGCA1::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const StateData& /*stateData*/,
                                      MatrixData<double>& matrixData,
                                      const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(activePower, alg + activePower, 1.0);
    matrixData.assign(reactivePower, alg + reactivePower, 1.0);
}

stringVec REGCA1::localStateNames() const
{
    return {"Pe", "Qe", "Ip", "Iq", "Vf"};
}

}  // namespace griddyn
