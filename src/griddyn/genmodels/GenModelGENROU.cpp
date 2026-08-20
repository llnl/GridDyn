/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelGENROU.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <complex>
#include <string>

namespace griddyn::genmodels {
namespace {
    /** Coefficients used to eliminate ANDES's auxiliary flux algebraic states.
     *
     * ANDES exposes psi2d and psi2q as algebraic variables. GridDyn evaluates
     * their closed-form expressions directly, reducing the nonlinear system
     * without changing the machine equations documented on GenModelGENROU.
     */
    struct GenrouCoefficients {
        double gd1;
        double gq1;
        double gd2;
        double gq2;
        double gqd;
    };

    GenrouCoefficients computeCoefficients(double xd,
                                           double xq,
                                           double xdp,
                                           double xqp,
                                           double xdpp,
                                           double xqpp,
                                           double xl)
    {
        const double xdDifference = xdp - xl;
        const double xqDifference = xqp - xl;
        return {(xdpp - xl) / xdDifference,
                (xqpp - xl) / xqDifference,
                (xdp - xdpp) / (xdDifference * xdDifference),
                (xqp - xqpp) / (xqDifference * xqDifference),
                (xq - xl) / (xd - xl)};
    }

}  // namespace

GenModelGENROU::GenModelGENROU(const std::string& objName): GenModel5(objName)
{
    Xdpp = 0.2;
    Xqpp = 0.2;
    Xqp = 0.30;
    D = 0.03;
    S10 = 0.0;
    S12 = 1.0;
    sat.setType(utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    sat.setParam(S10, S12);
}

CoreObject* GenModelGENROU::clone(CoreObject* obj) const
{
    auto* gd = cloneBase<GenModelGENROU, GenModel5>(this, obj);
    if (gd == nullptr) {
        return obj;
    }
    return gd;
}

void GenModelGENROU::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if ((H <= 0.0) || (Tdop <= 0.0) || (Tdopp <= 0.0) || (Tqop <= 0.0) || (Tqopp <= 0.0) ||
        (Xd <= Xdp) || (Xdp < Xdpp) || (Xdpp < Xl) || (Xq <= Xqp) || (Xqp < Xqpp) || (Xqpp < Xl)) {
        throw InvalidParameterValue("GENROU reactances, inertia, or time constants");
    }

    offsets.local().local.diffSize = 6;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 50;
}

void GenModelGENROU::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    // This complex initialization follows ANDES GENROU and its cited OpenIPSL
    // reference. The explicit sign changes below translate the rotated ANDES
    // quantities into the GridDyn dq convention documented in the header.
    const double voltageMagnitude = inputs[VOLTAGE_IN_LOCATION];
    const double voltageAngle = inputs[ANGLE_IN_LOCATION];
    const std::complex<double> terminalVoltage = std::polar(voltageMagnitude, voltageAngle);
    const std::complex<double> terminalPower(desiredOutput[POUT_LOCATION],
                                             -desiredOutput[QOUT_LOCATION]);
    const std::complex<double> terminalCurrent = terminalPower / std::conj(terminalVoltage);
    const std::complex<double> statorImpedance(Rs, Xdpp);
    const std::complex<double> subtransientFlux =
        terminalVoltage + terminalCurrent * statorImpedance;

    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    const double fluxMagnitude = std::abs(subtransientFlux);
    const double saturation = sat.compute(fluxMagnitude);
    const double currentMagnitude = std::abs(terminalCurrent);
    const double fluxCurrentAngle = std::arg(subtransientFlux) - std::arg(terminalCurrent);
    const double angleNumerator = currentMagnitude * (Xqpp - Xq) * std::cos(fluxCurrentAngle);
    const double angleDenominator = currentMagnitude * (Xqpp - Xq) * std::sin(fluxCurrentAngle) -
        fluxMagnitude * (1.0 + saturation * coefficients.gqd);
    const double rotorAngle =
        std::atan(angleNumerator / angleDenominator) + std::arg(subtransientFlux);

    const std::complex<double> dqRotation = std::polar(1.0, -rotorAngle);
    const std::complex<double> fluxDq = subtransientFlux * dqRotation;
    const std::complex<double> currentDq = std::conj(terminalCurrent * dqRotation);
    const double andesId = std::imag(currentDq);
    const double iq = std::real(currentDq);
    const double idCurrent = -andesId;
    const double psi2d = std::real(fluxDq);
    const double psi2q = std::imag(fluxDq);  // negative of the ANDES q-axis flux convention

    double* gm = m_state.data();
    gm[0] = idCurrent;
    gm[1] = iq;
    gm[2] = rotorAngle;
    gm[3] = 1.0;

    Vd = -voltageMagnitude * std::sin(rotorAngle - voltageAngle);
    Vq = voltageMagnitude * std::cos(rotorAngle - voltageAngle);

    const double fieldVoltage = (1.0 + saturation) * psi2d + (Xd - Xdpp) * andesId;
    gm[4] = -(Xq - Xqp) * iq - saturation * coefficients.gqd * psi2q;
    gm[5] = (Xd - Xdp) * idCurrent - saturation * psi2d + fieldVoltage;
    gm[6] = -(Xq - Xl) * iq - saturation * coefficients.gqd * psi2q;
    gm[7] = (Xd - Xl) * idCurrent - saturation * psi2d + fieldVoltage;

    const double mechanicalPower = (Vd + Rs * idCurrent) * idCurrent + (Vq + Rs * iq) * iq;
    fieldSet[genModelEftInLocation] = fieldVoltage;
    fieldSet[genModelPmechInLocation] = mechanicalPower;
}

void GenModelGENROU::algebraicUpdate(const IOdata& inputs,
                                     const StateData& sD,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    auto Loc = offsets.getLocations(sD, update, sMode, this);
    updateLocalCache(inputs, sD, sMode);
    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    const double psi2q =
        coefficients.gq1 * Loc.diffStateLoc[2] + (1.0 - coefficients.gq1) * Loc.diffStateLoc[4];
    const double psi2d =
        coefficients.gd1 * Loc.diffStateLoc[3] + (1.0 - coefficients.gd1) * Loc.diffStateLoc[5];

    gmlc::utilities::solve2x2(
        Rs, Xqpp, -Xdpp, Rs, psi2q - Vd, psi2d - Vq, Loc.destLoc[0], Loc.destLoc[1]);
    m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
}

void GenModelGENROU::derivative(const IOdata& inputs,
                                const StateData& sD,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    const double* alg = Loc.algStateLoc;
    const double* state = Loc.diffStateLoc;
    double* dstate = Loc.destDiffLoc;
    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);

    const double psi2q = coefficients.gq1 * state[2] + (1.0 - coefficients.gq1) * state[4];
    const double psi2d = coefficients.gd1 * state[3] + (1.0 - coefficients.gd1) * state[5];
    const double fluxMagnitude = std::hypot(psi2d, psi2q);
    const double saturation = sat.compute(fluxMagnitude);

    const double fieldVoltage = inputs[genModelEftInLocation];
    const double mechanicalPower = inputs[genModelPmechInLocation];
    const double electricalTorque = (Vd + Rs * alg[0]) * alg[0] + (Vq + Rs * alg[1]) * alg[1];

    dstate[0] = systemBaseFrequency * (state[1] - 1.0);
    dstate[1] = 0.5 * (mechanicalPower - electricalTorque - D * (state[1] - 1.0)) / H;
    dstate[2] =
        (-state[2] -
         (Xq - Xqp) * (coefficients.gq2 * (state[2] - state[4]) + coefficients.gq1 * alg[1]) -
         saturation * coefficients.gqd * psi2q) /
        Tqop;
    dstate[3] =
        (fieldVoltage - state[3] +
         (Xd - Xdp) * (coefficients.gd1 * alg[0] + coefficients.gd2 * (state[5] - state[3])) -
         saturation * psi2d) /
        Tdop;
    dstate[4] = (-state[4] + state[2] - (Xqp - Xl) * alg[1]) / Tqopp;
    dstate[5] = (-state[5] + state[3] + (Xdp - Xl) * alg[0]) / Tdopp;
}

void GenModelGENROU::residual(const IOdata& inputs,
                              const StateData& sD,
                              double resid[],
                              const SolverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, resid, sMode, this);
    const double* alg = Loc.algStateLoc;
    const double* state = Loc.diffStateLoc;
    const double* dstate = Loc.dstateLoc;
    double* algResidual = Loc.destLoc;
    double* stateResidual = Loc.destDiffLoc;
    updateLocalCache(inputs, sD, sMode);

    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    if (hasAlgebraic(sMode)) {
        const double psi2q = coefficients.gq1 * state[2] + (1.0 - coefficients.gq1) * state[4];
        const double psi2d = coefficients.gd1 * state[3] + (1.0 - coefficients.gd1) * state[5];
        algResidual[0] = Vd + Rs * alg[0] + Xqpp * alg[1] - psi2q;
        algResidual[1] = Vq + Rs * alg[1] - Xdpp * alg[0] - psi2d;
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, sD, resid, sMode);
        for (index_t index = 0; index < 6; ++index) {
            stateResidual[index] -= dstate[index];
        }
    }
}

void GenModelGENROU::jacobianElements(const IOdata& inputs,
                                      const StateData& sD,
                                      MatrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    const double* alg = Loc.algStateLoc;
    const double* state = Loc.diffStateLoc;
    const auto refAlg = Loc.algOffset;
    const auto refDiff = Loc.diffOffset;
    const auto voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    const auto angleLoc = inputLocs[ANGLE_IN_LOCATION];
    updateLocalCache(inputs, sD, sMode);

    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    if (hasAlgebraic(sMode)) {
        if (angleLoc != kNullLocation) {
            md.assign(refAlg, angleLoc, Vq);
            md.assign(refAlg + 1, angleLoc, -Vd);
        }
        if (voltageLoc != kNullLocation) {
            md.assign(refAlg, voltageLoc, Vd / inputs[VOLTAGE_IN_LOCATION]);
            md.assign(refAlg + 1, voltageLoc, Vq / inputs[VOLTAGE_IN_LOCATION]);
        }
        md.assign(refAlg, refAlg, Rs);
        md.assign(refAlg, refAlg + 1, Xqpp);
        md.assign(refAlg + 1, refAlg, -Xdpp);
        md.assign(refAlg + 1, refAlg + 1, Rs);

        if (isAlgebraicOnly(sMode)) {
            return;
        }
        md.assign(refAlg, refDiff, -Vq);
        md.assign(refAlg, refDiff + 2, -coefficients.gq1);
        md.assign(refAlg, refDiff + 4, -(1.0 - coefficients.gq1));
        md.assign(refAlg + 1, refDiff, Vd);
        md.assign(refAlg + 1, refDiff + 3, -coefficients.gd1);
        md.assign(refAlg + 1, refDiff + 5, -(1.0 - coefficients.gd1));
    }

    if (!hasDifferential(sMode)) {
        return;
    }

    md.assign(refDiff, refDiff, -sD.cj);
    md.assign(refDiff, refDiff + 1, systemBaseFrequency);

    const double inverseTwoH = 0.5 / H;
    if (hasAlgebraic(sMode)) {
        md.assign(refDiff + 1, refAlg, -inverseTwoH * (Vd + 2.0 * Rs * alg[0]));
        md.assign(refDiff + 1, refAlg + 1, -inverseTwoH * (Vq + 2.0 * Rs * alg[1]));
    }
    md.assign(refDiff + 1, refDiff, inverseTwoH * (Vq * alg[0] - Vd * alg[1]));
    md.assign(refDiff + 1, refDiff + 1, -inverseTwoH * D - sD.cj);
    md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], inverseTwoH);
    if (angleLoc != kNullLocation) {
        md.assign(refDiff + 1, angleLoc, -inverseTwoH * (Vq * alg[0] - Vd * alg[1]));
    }
    if (voltageLoc != kNullLocation) {
        md.assign(refDiff + 1,
                  voltageLoc,
                  -inverseTwoH * (Vd * alg[0] + Vq * alg[1]) / inputs[VOLTAGE_IN_LOCATION]);
    }

    const double psi2q = coefficients.gq1 * state[2] + (1.0 - coefficients.gq1) * state[4];
    const double psi2d = coefficients.gd1 * state[3] + (1.0 - coefficients.gd1) * state[5];
    const double fluxMagnitude = std::hypot(psi2d, psi2q);
    const auto saturation = sat.evaluate(fluxMagnitude);
    double dSaturationD = 0.0;
    double dSaturationQ = 0.0;
    if (fluxMagnitude > 0.0) {
        dSaturationD = saturation.derivative * psi2d / fluxMagnitude;
        dSaturationQ = saturation.derivative * psi2q / fluxMagnitude;
    }
    const double dQSatD = coefficients.gqd * psi2q * dSaturationD;
    const double dQSatQ = coefficients.gqd * (saturation.value + psi2q * dSaturationQ);
    const double dDSatD = saturation.value + psi2d * dSaturationD;
    const double dDSatQ = psi2d * dSaturationQ;

    if (hasAlgebraic(sMode)) {
        md.assign(refDiff + 2, refAlg + 1, -(Xq - Xqp) * coefficients.gq1 / Tqop);
    }
    md.assign(refDiff + 2,
              refDiff + 2,
              (-1.0 - (Xq - Xqp) * coefficients.gq2 - dQSatQ * coefficients.gq1) / Tqop - sD.cj);
    md.assign(refDiff + 2,
              refDiff + 4,
              ((Xq - Xqp) * coefficients.gq2 - dQSatQ * (1.0 - coefficients.gq1)) / Tqop);
    md.assign(refDiff + 2, refDiff + 3, -dQSatD * coefficients.gd1 / Tqop);
    md.assign(refDiff + 2, refDiff + 5, -dQSatD * (1.0 - coefficients.gd1) / Tqop);

    if (hasAlgebraic(sMode)) {
        md.assign(refDiff + 3, refAlg, (Xd - Xdp) * coefficients.gd1 / Tdop);
    }
    md.assign(refDiff + 3,
              refDiff + 3,
              (-1.0 - (Xd - Xdp) * coefficients.gd2 - dDSatD * coefficients.gd1) / Tdop - sD.cj);
    md.assign(refDiff + 3,
              refDiff + 5,
              ((Xd - Xdp) * coefficients.gd2 - dDSatD * (1.0 - coefficients.gd1)) / Tdop);
    md.assign(refDiff + 3, refDiff + 2, -dDSatQ * coefficients.gq1 / Tdop);
    md.assign(refDiff + 3, refDiff + 4, -dDSatQ * (1.0 - coefficients.gq1) / Tdop);
    md.assignCheckCol(refDiff + 3, inputLocs[genModelEftInLocation], 1.0 / Tdop);

    if (hasAlgebraic(sMode)) {
        md.assign(refDiff + 4, refAlg + 1, -(Xqp - Xl) / Tqopp);
    }
    md.assign(refDiff + 4, refDiff + 2, 1.0 / Tqopp);
    md.assign(refDiff + 4, refDiff + 4, -1.0 / Tqopp - sD.cj);
    if (hasAlgebraic(sMode)) {
        md.assign(refDiff + 5, refAlg, (Xdp - Xl) / Tdopp);
    }
    md.assign(refDiff + 5, refDiff + 3, 1.0 / Tdopp);
    md.assign(refDiff + 5, refDiff + 5, -1.0 / Tdopp - sD.cj);
}

void GenModelGENROU::set(std::string_view param, std::string_view val)
{
    GenModel5::set(param, val);
}

void GenModelGENROU::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "s1") || (param == "s10")) {
        S10 = val;
        sat.setParam(S10, S12);
    } else if (param == "s12") {
        S12 = val;
        sat.setParam(S10, S12);
    } else if (param == "xpp") {
        Xdpp = val;
        Xqpp = val;
    } else {
        GenModel5::set(param, val, unitType);
    }
}

static const stringVec genModelGenrouNames{"id", "iq", "delta", "freq", "e1d", "e1q", "e2q", "e2d"};

stringVec GenModelGENROU::localStateNames() const
{
    return genModelGenrouNames;
}
}  // namespace griddyn::genmodels
