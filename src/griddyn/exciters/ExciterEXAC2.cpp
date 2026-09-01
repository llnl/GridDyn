/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterEXAC2.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::exciters {
namespace {
    // These decimal values are the specified PSS/E FEX curve coefficients,
    // not approximations of unrelated mathematical constants.
    constexpr double lowCurrentSlope = 0.577;  // NOLINT(modernize-use-std-numbers)
    constexpr double highCurrentSlope = 1.732;  // NOLINT(modernize-use-std-numbers)
}  // namespace

ExciterEXAC2::ExciterEXAC2(const std::string& objName): ExciterEXAC1(objName) {}

CoreObject* ExciterEXAC2::clone(CoreObject* obj) const
{
    auto* clone = cloneBase<ExciterEXAC2, ExciterEXAC1>(this, obj);
    if (clone == nullptr) {
        return obj;
    }
    ExciterEXAC1::clone(clone);
    clone->Vamax = Vamax;
    clone->Vamin = Vamin;
    clone->Vlr = Vlr;
    clone->Kl = Kl;
    clone->Kh = Kh;
    clone->Kb = Kb;
    clone->vlr0 = vlr0;
    return clone;
}

void ExciterEXAC2::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(Vamax) || !std::isfinite(Vamin) || !std::isfinite(Vlr) ||
        !std::isfinite(Kl) || !std::isfinite(Kh) || !std::isfinite(Kb) || (Kb <= 0.0) ||
        (Kl <= 0.0) || (Vamax < Vamin) || (Tr <= 0.0)) {
        throw InvalidParameterValue("EXAC2 gains or limits");
    }
    ExciterEXAC1::dynObjectInitializeA(time0, flags);
}

void ExciterEXAC2::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    // VLR0 is a PostInitService in ANDES: it is calculated once from the
    // initialized field-feedback voltage and remains constant thereafter.
    const double fieldVoltage = desiredOutput.empty() ? 0.0 : desiredOutput[0];
    double exciterVoltage = std::max(1e-8, std::abs(fieldVoltage));
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    for (int count = 0; count < 20; ++count) {
        const double normalized = (Kc != 0.0) ? Kc * fieldCurrent / exciterVoltage : 0.0;
        double factor = 1.0;
        double slope = 0.0;
        if ((normalized > 0.0) && (normalized <= 0.433)) {
            factor = 1.0 - (lowCurrentSlope * normalized);
            slope = -lowCurrentSlope;
        } else if ((normalized > 0.433) && (normalized <= 0.75)) {
            factor = std::sqrt(std::max(0.0, 0.75 - (normalized * normalized)));
            slope = (factor > 0.0) ? -normalized / factor : 0.0;
        } else if ((normalized > 0.75) && (normalized <= 1.0)) {
            factor = highCurrentSlope * (1.0 - normalized);
            slope = -highCurrentSlope;
        } else if (normalized > 1.0) {
            factor = 0.0;
        }
        const double error = (exciterVoltage * factor) - fieldVoltage;
        const double derivative = factor - (slope * normalized);
        if (std::abs(derivative) < 1e-12) {
            break;
        }
        exciterVoltage = std::max(1e-8, exciterVoltage - (error / derivative));
        if (std::abs(error) < 1e-12) {
            break;
        }
    }
    const double feedback = (Ke * exciterVoltage) + saturation(exciterVoltage) +
        ((Kd == 0.0) ? 0.0 : Kd * fieldCurrent);
    vlr0 = std::max(Vlr, feedback + (feedback / (Kl * Kb)));
    ExciterEXAC1::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
}

void ExciterEXAC2::set(std::string_view param, std::string_view val)
{
    ExciterEXAC1::set(param, val);
}

void ExciterEXAC2::set(std::string_view param, double val, units::unit unitType)
{
    const auto finite = [val](const char* label) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue(std::string("EXAC2 ") + label + " must be finite");
        }
    };
    if (param == "vamax") {
        finite("VAMAX");
        Vamax = val;
    } else if (param == "vamin") {
        finite("VAMIN");
        Vamin = val;
    } else if (param == "vlr") {
        finite("VLR");
        Vlr = val;
    } else if (param == "kl") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXAC2 KL must be positive and finite");
        }
        Kl = val;
    } else if (param == "kh") {
        finite("KH");
        Kh = val;
    } else if (param == "kb") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXAC2 KB must be positive and finite");
        }
        Kb = val;
    } else {
        ExciterEXAC1::set(param, val, unitType);
    }
}

double ExciterEXAC2::get(std::string_view param, units::unit unitType) const
{
    if (param == "vamax") {
        return Vamax;
    }
    if (param == "vamin") {
        return Vamin;
    }
    if (param == "vlr") {
        return Vlr;
    }
    if (param == "kl") {
        return Kl;
    }
    if (param == "kh") {
        return Kh;
    }
    if (param == "kb") {
        return Kb;
    }
    return ExciterEXAC1::get(param, unitType);
}

double ExciterEXAC2::regulatorTarget(const IOdata& inputs, const double state[]) const
{
    const double feedback = vfe(inputs, state);
    const double highGate = state[2] - (Kh * feedback);
    const double lowGate = Kl * (vlr0 - feedback);
    return std::clamp(Kb * std::min(highGate, lowGate),
                      static_cast<double>(Vrmin),
                      static_cast<double>(Vrmax));
}

double ExciterEXAC2::regulatorUpperLimit() const
{
    return Vamax;
}
double ExciterEXAC2::regulatorLowerLimit() const
{
    return Vamin;
}
double ExciterEXAC2::initialRegulatorState(double vfeValue) const
{
    return (vfeValue * Kl) + (vfeValue / Kb);
}
double ExciterEXAC2::referenceOffset(double vfeValue) const
{
    return ((vfeValue * Kl) + (vfeValue / Kb)) / Ka;
}

void ExciterEXAC2::regulatorTargetDerivatives(const IOdata& inputs,
                                              const double state[],
                                              double& regulatorDerivative,
                                              double& exciterDerivative,
                                              double& fieldCurrentDerivative) const
{
    const double feedbackSlope = Ke + saturation.deriv(state[3]);
    const double feedback = vfe(inputs, state);
    const double highGate = state[2] - (Kh * feedback);
    const double lowGate = Kl * (vlr0 - feedback);
    const double unbounded = Kb * std::min(highGate, lowGate);
    regulatorDerivative = 0.0;
    exciterDerivative = 0.0;
    fieldCurrentDerivative = 0.0;
    if ((unbounded <= Vrmin) || (unbounded >= Vrmax)) {
        return;
    }
    if (highGate <= lowGate) {
        regulatorDerivative = Kb;
        exciterDerivative = -Kb * Kh * feedbackSlope;
        fieldCurrentDerivative = -Kb * Kh * Kd;
    } else {
        exciterDerivative = -Kb * Kl * feedbackSlope;
        fieldCurrentDerivative = -Kb * Kl * Kd;
    }
}
}  // namespace griddyn::exciters
