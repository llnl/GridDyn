/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CIMLoad.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include <cmath>
#include <string>

namespace griddyn::loads {
namespace {

    double cimSaturationFactor(double erpp, double empp, const utilities::Saturation& saturation)
    {
        const double magnitude = std::hypot(erpp, empp);
        return (magnitude > 0.0) ? saturation.compute(magnitude) / magnitude : 0.0;
    }

    void cimSaturationFactorDerivatives(double erpp,
                                        double empp,
                                        const utilities::Saturation& saturation,
                                        double& derivativeErpp,
                                        double& derivativeEmpp)
    {
        const double magnitude = std::hypot(erpp, empp);
        if (magnitude == 0.0) {
            derivativeErpp = 0.0;
            derivativeEmpp = 0.0;
            return;
        }
        const auto evaluation = saturation.evaluate(magnitude);
        const double derivativeMagnitude =
            ((evaluation.derivative * magnitude) - evaluation.value) / (magnitude * magnitude);
        derivativeErpp = derivativeMagnitude * erpp / magnitude;
        derivativeEmpp = derivativeMagnitude * empp / magnitude;
    }
}  // namespace

const ModelDescription& CIM5::getModelDescription() const
{
    static const auto description = ModelDescription::define<CIM5>(
        "CIM5",
        &MotorLoad5::getModelDescription(),
        {parameter({"ra", "rs"}, &CIM5::r, units::one, "R"),
         parameter({"xa"}, &CIM5::x, units::one, "X"),
         parameter({"tnom", "t_nom"}, &CIM5::torqueNominal, units::one, "Torque nominal"),
         parameter({"d", "torqueexponent"}, &CIM5::torqueExponent, units::one, "Torque exponent"),
         parameter({"e1"}, &CIM5::saturationE1, units::one, "Saturation e1"),
         parameter({"se1"}, &CIM5::saturationSE1, units::one, "Saturation se1"),
         parameter({"e2"}, &CIM5::saturationE2, units::one, "Saturation e2"),
         parameter({"se2"}, &CIM5::saturationSE2, units::one, "Saturation se2")});
    return description;
}

void CIM5::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = CIM5::getModelDescription();
    getParamString<CIM5, MotorLoad5>(this,
                                     pstr,
                                     description.numericNames(),
                                     description.stringNames(),
                                     description.flagNames(),
                                     pstype);
}

CIM5::CIM5(const std::string& objName): MotorLoad5(objName)
{
    saturation.setParam(saturationE1, saturationSE1, saturationE2, saturationSE2);
}
CoreObject* CIM5::clone(CoreObject* obj) const
{
    auto* copied = cloneBase<CIM5, MotorLoad5>(this, obj);
    if (copied != nullptr) {
        copied->torqueNominal = torqueNominal;
        copied->torqueExponent = torqueExponent;
        copied->saturation = saturation;
        copied->saturationE1 = saturationE1;
        copied->saturationSE1 = saturationSE1;
        copied->saturationE2 = saturationE2;
        copied->saturationSE2 = saturationSE2;
    }
    return (copied == nullptr) ? obj : copied;
}
void CIM5::set(std::string_view param, double val, units::unit unitType)
{
    if (CIM5::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        if (param == "e1" || param == "se1" || param == "e2" || param == "se2") {
            saturation.setParam(saturationE1, saturationSE1, saturationE2, saturationSE2);
        }
        return;
    }
    MotorLoad5::set(param, val, unitType);
}
double CIM5::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = CIM5::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }

    return MotorLoad5::get(param, unitType);
}
double CIM5::mechPower(double slip) const
{
    return torqueNominal * std::pow(1.0 - slip, torqueExponent);
}
double CIM5::dmechds(double slip) const
{
    return -torqueNominal * torqueExponent * std::pow(1.0 - slip, torqueExponent - 1.0);
}
double CIM5::saturationFactor(double erpp, double empp) const
{
    return cimSaturationFactor(erpp, empp, saturation);
}
void CIM5::saturationFactorDerivatives(double erpp,
                                       double empp,
                                       double& derivativeErpp,
                                       double& derivativeEmpp) const
{
    cimSaturationFactorDerivatives(erpp, empp, saturation, derivativeErpp, derivativeEmpp);
}

const ModelDescription& CIM6::getModelDescription() const
{
    static const auto description = ModelDescription::define<CIM6>(
        "CIM6",
        &MotorLoad5::getModelDescription(),
        {parameter({"ra", "rs"}, &CIM6::r, units::one, "R"),
         parameter({"xa"}, &CIM6::x, units::one, "X"),
         parameter({"tnom", "t_nom"}, &CIM6::torqueNominal, units::one, "Torque nominal"),
         parameter({"a"}, &CIM6::coefficientA, units::one, "Coefficient a"),
         parameter({"b"}, &CIM6::coefficientB, units::one, "Coefficient b"),
         parameter({"c0", "cimc"}, &CIM6::coefficientC, units::one, "Coefficient c"),
         parameter({"d", "cimd"}, &CIM6::coefficientD, units::one, "Coefficient d"),
         parameter({"e"}, &CIM6::exponentE, units::one, "Exponent e"),
         parameter({"e1"}, &CIM6::saturationE1, units::one, "Saturation e1"),
         parameter({"se1"}, &CIM6::saturationSE1, units::one, "Saturation se1"),
         parameter({"e2"}, &CIM6::saturationE2, units::one, "Saturation e2"),
         parameter({"se2"}, &CIM6::saturationSE2, units::one, "Saturation se2")});
    return description;
}

void CIM6::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = CIM6::getModelDescription();
    getParamString<CIM6, MotorLoad5>(this,
                                     pstr,
                                     description.numericNames(),
                                     description.stringNames(),
                                     description.flagNames(),
                                     pstype);
}

CIM6::CIM6(const std::string& objName): MotorLoad5(objName)
{
    saturation.setParam(saturationE1, saturationSE1, saturationE2, saturationSE2);
}
CoreObject* CIM6::clone(CoreObject* obj) const
{
    auto* copied = cloneBase<CIM6, MotorLoad5>(this, obj);
    if (copied != nullptr) {
        copied->torqueNominal = torqueNominal;
        copied->coefficientA = coefficientA;
        copied->coefficientB = coefficientB;
        copied->coefficientC = coefficientC;
        copied->coefficientD = coefficientD;
        copied->exponentE = exponentE;
        copied->saturation = saturation;
        copied->saturationE1 = saturationE1;
        copied->saturationSE1 = saturationSE1;
        copied->saturationE2 = saturationE2;
        copied->saturationSE2 = saturationSE2;
    }
    return (copied == nullptr) ? obj : copied;
}
void CIM6::set(std::string_view param, double val, units::unit unitType)
{
    if (CIM6::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        if (param == "e1" || param == "se1" || param == "e2" || param == "se2") {
            saturation.setParam(saturationE1, saturationSE1, saturationE2, saturationSE2);
        }
        return;
    }
    MotorLoad5::set(param, val, unitType);
}
double CIM6::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = CIM6::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }

    return MotorLoad5::get(param, unitType);
}
double CIM6::mechPower(double slip) const
{
    const double omega = 1.0 - slip;
    return torqueNominal *
        ((coefficientA * omega * omega) + (coefficientB * omega) + coefficientC +
         (coefficientD * std::pow(omega, exponentE)));
}
double CIM6::dmechds(double slip) const
{
    const double omega = 1.0 - slip;
    return -torqueNominal *
        ((2.0 * coefficientA * omega) + coefficientB +
         (coefficientD * exponentE * std::pow(omega, exponentE - 1.0)));
}
double CIM6::saturationFactor(double erpp, double empp) const
{
    return cimSaturationFactor(erpp, empp, saturation);
}
void CIM6::saturationFactorDerivatives(double erpp,
                                       double empp,
                                       double& derivativeErpp,
                                       double& derivativeEmpp) const
{
    cimSaturationFactorDerivatives(erpp, empp, saturation, derivativeErpp, derivativeEmpp);
}
}  // namespace griddyn::loads
