/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REGCP1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace griddyn {

namespace {
    constexpr std::array<RenewablePort, 5> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::activeCurrentCommand,
         .ioIndex = 1,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveCurrentCommand,
         .ioIndex = 2,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 3},
        {.signal = RenewableSignal::measuredAngle, .ioIndex = 4, .required = false},
    }};
}  // namespace

REGCP1::REGCP1(const std::string& objName): REGCA1(objName)
{
    m_inputSize = 5;
}

CoreObject* REGCP1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REGCP1, REGCA1>(this, obj);
    if (out != nullptr) {
        out->pllName = pllName;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> REGCP1::inputPorts() const
{
    return inputPortMap;
}

std::string_view REGCP1::sourceName(RenewableSignal signal) const
{
    return signal == RenewableSignal::measuredAngle ? std::string_view{pllName} :
                                                      std::string_view{};
}

void REGCP1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    REGCA1::dynObjectInitializeA(time0, flags);
    offsets.local().local.jacSize = 36;
}

void REGCP1::set(std::string_view param, double val, units::unit unitType)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "pll") {
        if (val != 0.0) {
            throw InvalidParameterValue("REGCP1 PLL reference must be a sensor name");
        }
        pllName.clear();
        return;
    }
    REGCA1::set(param, val, unitType);
}

void REGCP1::set(std::string_view param, std::string_view val)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "pll") {
        const auto value = gmlc::utilities::convertToLowerCase(std::string{val});
        pllName = value.empty() || value == "none" || value == "null" || value == "0" ?
            std::string{} :
            std::string{val};
        return;
    }
    GridComponent::set(param, val);
}

double REGCP1::angleDifference(const IOdata& inputs) const
{
    if (pllName.empty()) {
        return 0.0;
    }
    if (inputs.size() <= 4 || inputs[3] == kNullVal || inputs[4] == kNullVal ||
        !std::isfinite(inputs[3]) || !std::isfinite(inputs[4])) {
        throw InvalidParameterValue("REGCP1 requires terminal and named PLL angles");
    }
    return inputs[3] - inputs[4];
}

std::array<double, 2>
    REGCP1::powerInjection(const IOdata& inputs, double activeCurrent, double reactiveCurrent) const
{
    const double delta = angleDifference(inputs);
    const double cosine = std::cos(delta);
    const double sine = std::sin(delta);
    const double activeCurrentLimited = activeCurrent * lowVoltageGain(inputs[0]);
    return {inputs[0] * ((cosine * activeCurrentLimited) - (sine * reactiveCurrent)),
            inputs[0] * ((sine * activeCurrentLimited) + (cosine * reactiveCurrent))};
}

std::array<double, 2> REGCP1::initialCurrentFramePower(const IOdata& inputs,
                                                       const IOdata& desiredOutput) const
{
    const double delta = angleDifference(inputs);
    return {(std::cos(delta) * desiredOutput[0]) + (std::sin(delta) * desiredOutput[1]),
            (-std::sin(delta) * desiredOutput[0]) + (std::cos(delta) * desiredOutput[1])};
}

void REGCP1::powerJacobian(const IOdata& inputs,
                           const double state[],
                           MatrixData<double>& matrixData,
                           const IOlocs& inputLocs,
                           const SolverMode& sMode,
                           index_t alg,
                           index_t diff) const
{
    const double voltage = inputs[0];
    const double delta = angleDifference(inputs);
    const double cosine = std::cos(delta);
    const double sine = std::sin(delta);
    const double gain = lowVoltageGain(voltage);
    const double activeCurrentLimited = state[0] * gain;
    const double reactiveCurrent = state[1];
    matrixData.assign(alg, alg, -1.0);
    matrixData.assign(alg + 1, alg + 1, -1.0);
    if (!isAlgebraicOnly(sMode)) {
        matrixData.assign(alg, diff, voltage * cosine * gain);
        matrixData.assign(alg, diff + 1, -voltage * sine);
        matrixData.assign(alg + 1, diff, voltage * sine * gain);
        matrixData.assign(alg + 1, diff + 1, voltage * cosine);
    }
    const double voltageGain = gain + (voltage * lowVoltageGainSlope(voltage));
    matrixData.assignCheckCol(alg,
                              inputLocs[0],
                              (cosine * state[0] * voltageGain) - (sine * reactiveCurrent));
    matrixData.assignCheckCol(alg + 1,
                              inputLocs[0],
                              (sine * state[0] * voltageGain) + (cosine * reactiveCurrent));
    if (!pllName.empty()) {
        const double active =
            voltage * ((cosine * activeCurrentLimited) - (sine * reactiveCurrent));
        const double reactive =
            voltage * ((sine * activeCurrentLimited) + (cosine * reactiveCurrent));
        if (inputLocs.size() > 3) {
            matrixData.assignCheckCol(alg, inputLocs[3], -reactive);
            matrixData.assignCheckCol(alg + 1, inputLocs[3], active);
        }
        if (inputLocs.size() > 4) {
            matrixData.assignCheckCol(alg, inputLocs[4], reactive);
            matrixData.assignCheckCol(alg + 1, inputLocs[4], -active);
        }
    }
}

}  // namespace griddyn
