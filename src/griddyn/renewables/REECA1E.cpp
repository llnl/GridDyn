/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REECA1E.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 6> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::activeReferenceIncrement,
         .ioIndex = 1,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveReferenceIncrement,
         .ioIndex = 2,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::activeReference,
         .ioIndex = 3,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::frequencyDeviation, .ioIndex = 5},
        {.signal = RenewableSignal::rateOfChangeOfFrequency, .ioIndex = 6},
    }};
    constexpr std::array<RenewablePort, 7> inputPortMapWithSpeed{{
        inputPortMap[0], inputPortMap[1], inputPortMap[2], inputPortMap[3],
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 4},
        inputPortMap[4], inputPortMap[5],
    }};
}  // namespace

REECA1E::REECA1E(const std::string& name): REECA1(name)
{
    m_inputSize = 7;
}

CoreObject* REECA1E::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REECA1E, REECA1>(this, obj);
    if (out != nullptr) {
        out->Kf = Kf;
        out->Kdf = Kdf;
        out->measurementName = measurementName;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> REECA1E::inputPorts() const
{
    return get("pflag") == 1.0 ? std::span<const RenewablePort>{inputPortMapWithSpeed} :
                                 std::span<const RenewablePort>{inputPortMap};
}

std::string_view REECA1E::sourceName(RenewableSignal signal) const
{
    if (signal == RenewableSignal::frequencyDeviation ||
        signal == RenewableSignal::rateOfChangeOfFrequency) {
        return measurementName;
    }
    return {};
}

void REECA1E::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kf") {
        Kf = val;
    } else if (key == "kdf") {
        Kdf = val;
    } else {
        REECA1::set(param, val, unitType);
        if (key == "pflag") {
            m_inputSize = 7;
        }
    }
}

void REECA1E::set(std::string_view param, std::string_view val)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "busroc") {
        measurementName = val;
        return;
    }
    GridComponent::set(param, val);
}

double REECA1E::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kf") {
        return Kf;
    }
    if (key == "kdf") {
        return Kdf;
    }
    return REECA1::get(param, unitType);
}

void REECA1E::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(Kf) || !std::isfinite(Kdf)) {
        throw InvalidParameterValue("REECA1E frequency gains must be finite");
    }
    if (measurementName.empty()) {
        throw InvalidParameterValue("REECA1E requires a named BUSROCOF measurement");
    }
    REECA1::dynObjectInitializeA(time0, flags);
}

double REECA1E::activeReferenceAdjustment(const IOdata& inputs) const
{
    if (inputs.size() <= 6 || !std::isfinite(inputs[5]) || !std::isfinite(inputs[6]) ||
        inputs[5] == kNullVal || inputs[6] == kNullVal) {
        throw InvalidParameterValue("REECA1E requires frequency deviation and ROCOF");
    }
    return -Kf * inputs[5] - Kdf * inputs[6];
}

void REECA1E::activeReferenceJacobian(const IOlocs& inputLocs,
                                      MatrixData<double>& matrixData,
                                      index_t row,
                                      double gain) const
{
    if (inputLocs.size() > 6) {
        matrixData.assignCheckCol(row, inputLocs[5], -Kf * gain);
        matrixData.assignCheckCol(row, inputLocs[6], -Kdf * gain);
    }
}

}  // namespace griddyn
