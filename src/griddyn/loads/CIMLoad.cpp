/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CIMLoad.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <cmath>

namespace griddyn::loads {
namespace {
    void setSaturationParameter(std::string_view param,
                                double value,
                                double& e1,
                                double& se1,
                                double& e2,
                                double& se2,
                                utilities::Saturation& saturation)
    {
        if (param == "e1") {
            e1 = value;
        } else if (param == "se1") {
            se1 = value;
        } else if (param == "e2") {
            e2 = value;
        } else if (param == "se2") {
            se2 = value;
        }
        saturation.setParam(e1, se1, e2, se2);
    }

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
            (evaluation.derivative * magnitude - evaluation.value) / (magnitude * magnitude);
        derivativeErpp = derivativeMagnitude * erpp / magnitude;
        derivativeEmpp = derivativeMagnitude * empp / magnitude;
    }
}  // namespace

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
    if ((param == "e1") || (param == "se1") || (param == "e2") || (param == "se2")) {
        setSaturationParameter(
            param, val, saturationE1, saturationSE1, saturationE2, saturationSE2, saturation);
    } else if ((param == "ra") || (param == "rs")) {
        r = val;
    } else if (param == "xa") {
        x = val;
    } else if ((param == "tnom") || (param == "t_nom")) {
        torqueNominal = val;
    } else if ((param == "d") || (param == "torqueexponent")) {
        torqueExponent = val;
    } else {
        MotorLoad5::set(param, val, unitType);
    }
}
double CIM5::get(std::string_view param, units::unit unitType) const
{
    if ((param == "tnom") || (param == "t_nom")) return torqueNominal;
    if ((param == "d") || (param == "torqueexponent")) return torqueExponent;
    if (param == "e1") return saturationE1;
    if (param == "se1") return saturationSE1;
    if (param == "e2") return saturationE2;
    if (param == "se2") return saturationSE2;
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
    if ((param == "e1") || (param == "se1") || (param == "e2") || (param == "se2")) {
        setSaturationParameter(
            param, val, saturationE1, saturationSE1, saturationE2, saturationSE2, saturation);
    } else if ((param == "ra") || (param == "rs")) {
        r = val;
    } else if (param == "xa") {
        x = val;
    } else if ((param == "tnom") || (param == "t_nom")) {
        torqueNominal = val;
    } else if (param == "a") {
        coefficientA = val;
    } else if (param == "b") {
        coefficientB = val;
    } else if ((param == "c0") || (param == "cimc")) {
        coefficientC = val;
    } else if ((param == "d") || (param == "cimd")) {
        coefficientD = val;
    } else if (param == "e") {
        exponentE = val;
    } else {
        MotorLoad5::set(param, val, unitType);
    }
}
double CIM6::get(std::string_view param, units::unit unitType) const
{
    if ((param == "tnom") || (param == "t_nom")) return torqueNominal;
    if (param == "a") return coefficientA;
    if (param == "b") return coefficientB;
    if ((param == "c0") || (param == "cimc")) return coefficientC;
    if ((param == "d") || (param == "cimd")) return coefficientD;
    if (param == "e") return exponentE;
    if (param == "e1") return saturationE1;
    if (param == "se1") return saturationSE1;
    if (param == "e2") return saturationE2;
    if (param == "se2") return saturationSE2;
    return MotorLoad5::get(param, unitType);
}
double CIM6::mechPower(double slip) const
{
    const double omega = 1.0 - slip;
    return torqueNominal *
        (coefficientA * omega * omega + coefficientB * omega + coefficientC +
         coefficientD * std::pow(omega, exponentE));
}
double CIM6::dmechds(double slip) const
{
    const double omega = 1.0 - slip;
    return -torqueNominal *
        (2.0 * coefficientA * omega + coefficientB +
         coefficientD * exponentE * std::pow(omega, exponentE - 1.0));
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
