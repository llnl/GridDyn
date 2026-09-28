/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REECA1G.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 5> inputPortMap{{
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
        {.signal = RenewableSignal::synchronousSpeed, .ioIndex = 5},
    }};
    constexpr std::array<RenewablePort, 6> inputPortMapWithSpeed{{
        inputPortMap[0],
        inputPortMap[1],
        inputPortMap[2],
        inputPortMap[3],
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 4},
        inputPortMap[4],
    }};
}  // namespace

REECA1G::REECA1G(const std::string& name): REECA1(name)
{
    m_inputSize = 6;
}

CoreObject* REECA1G::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REECA1G, REECA1>(this, obj);
    if (out != nullptr) {
        out->Kf = Kf;
        out->generatorName = generatorName;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> REECA1G::inputPorts() const
{
    return get("pflag") == 1.0 ? std::span<const RenewablePort>{inputPortMapWithSpeed} :
                                 std::span<const RenewablePort>{inputPortMap};
}

std::string_view REECA1G::sourceName(RenewableSignal signal) const
{
    return signal == RenewableSignal::synchronousSpeed ? generatorName : std::string_view{};
}

void REECA1G::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kf") {
        Kf = val;
    } else {
        REECA1::set(param, val, unitType);
        if (key == "pflag") {
            m_inputSize = 6;
        }
    }
}

void REECA1G::set(std::string_view param, std::string_view val)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "sg") {
        generatorName = val;
        return;
    }
    GridComponent::set(param, val);
}

double REECA1G::get(std::string_view param, units::unit unitType) const
{
    return gmlc::utilities::convertToLowerCase(std::string{param}) == "kf" ?
        Kf :
        REECA1::get(param, unitType);
}

void REECA1G::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(Kf) || generatorName.empty()) {
        throw InvalidParameterValue("REECA1G requires finite Kf and a named synchronous generator");
    }
    REECA1::dynObjectInitializeA(time0, flags);
}

double REECA1G::activeReferenceAdjustment(const IOdata& inputs) const
{
    if (inputs.size() <= 5 || !std::isfinite(inputs[5]) || inputs[5] == kNullVal) {
        throw InvalidParameterValue("REECA1G requires synchronous generator speed");
    }
    return -Kf * (inputs[5] - 1.0);
}

void REECA1G::activeReferenceJacobian(const IOlocs& inputLocs,
                                      MatrixData<double>& matrixData,
                                      index_t row,
                                      double gain) const
{
    if (inputLocs.size() > 5) {
        matrixData.assignCheckCol(row, inputLocs[5], -Kf * gain);
    }
}

}  // namespace griddyn
