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
// The implementation mirrors the published equations, whose conventional
// grouping is clearer than the extra precedence-only parentheses requested by
// this clang-tidy check.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    /** Coefficients used to eliminate ANDES's auxiliary flux algebraic states.
     *
     * ANDES exposes psi2d and psi2q as algebraic variables. GridDyn evaluates
     * their closed-form expressions directly, reducing the nonlinear system
     * without changing the machine equations documented on GenModelGENROU.
     */
    struct GenrouCoefficients {
        double mGd1;
        double mGq1;
        double mGd2;
        double mGq2;
        double mGqd;
    };

    GenrouCoefficients computeCoefficients(double directAxisReactance,
                                           double quadratureAxisReactance,
                                           double directAxisTransientReactance,
                                           double quadratureAxisTransientReactance,
                                           double directAxisSubtransientReactance,
                                           double quadratureAxisSubtransientReactance,
                                           double leakageReactance)
    {
        const double xdDifference = directAxisTransientReactance - leakageReactance;
        const double xqDifference = quadratureAxisTransientReactance - leakageReactance;
        return {.mGd1 = (directAxisSubtransientReactance - leakageReactance) / xdDifference,
                .mGq1 = (quadratureAxisSubtransientReactance - leakageReactance) / xqDifference,
                .mGd2 = (directAxisTransientReactance - directAxisSubtransientReactance) /
                    (xdDifference * xdDifference),
                .mGq2 = (quadratureAxisTransientReactance - quadratureAxisSubtransientReactance) /
                    (xqDifference * xqDifference),
                .mGqd = (quadratureAxisReactance - leakageReactance) /
                    (directAxisReactance - leakageReactance)};
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
    auto* genrouClone = cloneBase<GenModelGENROU, GenModel5>(this, obj);
    if (genrouClone == nullptr) {
        return obj;
    }
    genrouClone->sat = sat;
    return genrouClone;
}

void GenModelGENROU::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    constexpr double minimumReactanceDifference = 1e-9;
    // PSS/E GENROU data conventionally uses a zero q-axis transient time
    // constant when Xq equals Xqp.  Keep the associated state well-defined
    // using the same short time constant fallback as the legacy model.
    if ((Tqop == 0.0) && (std::abs(Xq - Xqp) <= minimumReactanceDifference)) {
        Tqop = 0.01;
    }

    if ((H <= 0.0) || (Tdop <= 0.0) || (Tdopp <= 0.0) || (Tqop <= 0.0) || (Tqopp <= 0.0) ||
        (Xd <= Xdp) || (Xdp < Xdpp) || (Xq < Xqp) || (Xqp < Xqpp) ||
        (std::abs(Xdp - Xl) <= minimumReactanceDifference) ||
        (std::abs(Xqp - Xl) <= minimumReactanceDifference) ||
        (std::abs(Xd - Xl) <= minimumReactanceDifference)) {
        throw InvalidParameterValue("GENROU reactances, inertia, or time constants");
    }

    // ANDES reports xl > xd2 as an initialization warning but continues to
    // solve the model. Preserve that behavior for compatible PSS/E data; the
    // gamma coefficients remain well-defined unless a denominator above is
    // singular.
    if ((Xl > Xdpp) || (Xl > Xqpp)) {
        logging::warning(this,
                         "GENROU leakage reactance exceeds a subtransient reactance; "
                         "continuing with the supplied parameters");
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
        fluxMagnitude * (1.0 + saturation * coefficients.mGqd);
    const double rotorAngle =
        std::atan(angleNumerator / angleDenominator) + std::arg(subtransientFlux);

    const std::complex<double> dqRotation = std::polar(1.0, -rotorAngle);
    const std::complex<double> fluxDq = subtransientFlux * dqRotation;
    const std::complex<double> currentDq = std::conj(terminalCurrent * dqRotation);
    const double andesId = std::imag(currentDq);
    const double quadratureCurrent = std::real(currentDq);
    const double idCurrent = -andesId;
    const double psi2d = std::real(fluxDq);
    const double psi2q = std::imag(fluxDq);  // negative of the ANDES q-axis flux convention

    double* state = m_state.data();
    state[0] = idCurrent;
    state[1] = quadratureCurrent;
    state[2] = rotorAngle;
    state[3] = 1.0;

    Vd = -voltageMagnitude * std::sin(rotorAngle - voltageAngle);
    Vq = voltageMagnitude * std::cos(rotorAngle - voltageAngle);

    const double fieldVoltage = (1.0 + saturation) * psi2d + (Xd - Xdpp) * andesId;
    state[4] = -(Xq - Xqp) * quadratureCurrent - saturation * coefficients.mGqd * psi2q;
    state[5] = (Xd - Xdp) * idCurrent - saturation * psi2d + fieldVoltage;
    state[6] = -(Xq - Xl) * quadratureCurrent - saturation * coefficients.mGqd * psi2q;
    state[7] = (Xd - Xl) * idCurrent - saturation * psi2d + fieldVoltage;

    const double mechanicalPower =
        (Vd + Rs * idCurrent) * idCurrent + (Vq + Rs * quadratureCurrent) * quadratureCurrent;
    fieldSet[genModelEftInLocation] = fieldVoltage;
    fieldSet[genModelPmechInLocation] = mechanicalPower;
}

void GenModelGENROU::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    auto locations = offsets.getLocations(stateData, update, sMode, this);
    updateLocalCache(inputs, stateData, sMode);
    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    const double psi2q = coefficients.mGq1 * locations.diffStateLoc[2] +
        (1.0 - coefficients.mGq1) * locations.diffStateLoc[4];
    const double psi2d = coefficients.mGd1 * locations.diffStateLoc[3] +
        (1.0 - coefficients.mGd1) * locations.diffStateLoc[5];

    gmlc::utilities::solve2x2(
        Rs, Xqpp, -Xdpp, Rs, psi2q - Vd, psi2d - Vq, locations.destLoc[0], locations.destLoc[1]);
    m_output = -(locations.destLoc[1] * Vq + locations.destLoc[0] * Vd);
}

void GenModelGENROU::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* alg = locations.algStateLoc;
    const double* state = locations.diffStateLoc;
    double* dstate = locations.destDiffLoc;
    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);

    const double psi2q = coefficients.mGq1 * state[2] + (1.0 - coefficients.mGq1) * state[4];
    const double psi2d = coefficients.mGd1 * state[3] + (1.0 - coefficients.mGd1) * state[5];
    const double fluxMagnitude = std::hypot(psi2d, psi2q);
    const double saturation = sat.compute(fluxMagnitude);

    const double fieldVoltage = inputs[genModelEftInLocation];
    const double mechanicalPower = inputs[genModelPmechInLocation];
    const double electricalTorque = (Vd + Rs * alg[0]) * alg[0] + (Vq + Rs * alg[1]) * alg[1];

    dstate[0] = systemBaseFrequency * (state[1] - 1.0);
    dstate[1] = 0.5 * (mechanicalPower - electricalTorque - D * (state[1] - 1.0)) / H;
    dstate[2] =
        (-state[2] -
         (Xq - Xqp) * (coefficients.mGq2 * (state[2] - state[4]) + coefficients.mGq1 * alg[1]) -
         saturation * coefficients.mGqd * psi2q) /
        Tqop;
    dstate[3] =
        (fieldVoltage - state[3] +
         (Xd - Xdp) * (coefficients.mGd1 * alg[0] + coefficients.mGd2 * (state[5] - state[3])) -
         saturation * psi2d) /
        Tdop;
    dstate[4] = (-state[4] + state[2] - (Xqp - Xl) * alg[1]) / Tqopp;
    dstate[5] = (-state[5] + state[3] + (Xdp - Xl) * alg[0]) / Tdopp;
}

void GenModelGENROU::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const double* alg = locations.algStateLoc;
    const double* state = locations.diffStateLoc;
    const double* dstate = locations.dstateLoc;
    double* algResidual = locations.destLoc;
    double* stateResidual = locations.destDiffLoc;
    updateLocalCache(inputs, stateData, sMode);

    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    if (hasAlgebraic(sMode)) {
        const double psi2q = coefficients.mGq1 * state[2] + (1.0 - coefficients.mGq1) * state[4];
        const double psi2d = coefficients.mGd1 * state[3] + (1.0 - coefficients.mGd1) * state[5];
        algResidual[0] = Vd + Rs * alg[0] + Xqpp * alg[1] - psi2q;
        algResidual[1] = Vq + Rs * alg[1] - Xdpp * alg[0] - psi2d;
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < 6; ++index) {
            stateResidual[index] -= dstate[index];
        }
    }
}

void GenModelGENROU::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, sMode, this);
    const double* alg = locations.algStateLoc;
    const double* state = locations.diffStateLoc;
    const auto refAlg = locations.algOffset;
    const auto refDiff = locations.diffOffset;
    const auto voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    const auto angleLoc = inputLocs[ANGLE_IN_LOCATION];
    updateLocalCache(inputs, stateData, sMode);

    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);
    if (hasAlgebraic(sMode)) {
        if (angleLoc != kNullLocation) {
            matrixData.assign(refAlg, angleLoc, Vq);
            matrixData.assign(refAlg + 1, angleLoc, -Vd);
        }
        if (voltageLoc != kNullLocation) {
            matrixData.assign(refAlg, voltageLoc, Vd / inputs[VOLTAGE_IN_LOCATION]);
            matrixData.assign(refAlg + 1, voltageLoc, Vq / inputs[VOLTAGE_IN_LOCATION]);
        }
        matrixData.assign(refAlg, refAlg, Rs);
        matrixData.assign(refAlg, refAlg + 1, Xqpp);
        matrixData.assign(refAlg + 1, refAlg, -Xdpp);
        matrixData.assign(refAlg + 1, refAlg + 1, Rs);

        if (isAlgebraicOnly(sMode)) {
            return;
        }
        matrixData.assign(refAlg, refDiff, -Vq);
        matrixData.assign(refAlg, refDiff + 2, -coefficients.mGq1);
        matrixData.assign(refAlg, refDiff + 4, -(1.0 - coefficients.mGq1));
        matrixData.assign(refAlg + 1, refDiff, Vd);
        matrixData.assign(refAlg + 1, refDiff + 3, -coefficients.mGd1);
        matrixData.assign(refAlg + 1, refDiff + 5, -(1.0 - coefficients.mGd1));
    }

    if (!hasDifferential(sMode)) {
        return;
    }

    matrixData.assign(refDiff, refDiff, -stateData.cj);
    matrixData.assign(refDiff, refDiff + 1, systemBaseFrequency);

    const double inverseTwoH = 0.5 / H;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 1, refAlg, -inverseTwoH * (Vd + 2.0 * Rs * alg[0]));
        matrixData.assign(refDiff + 1, refAlg + 1, -inverseTwoH * (Vq + 2.0 * Rs * alg[1]));
    }
    matrixData.assign(refDiff + 1, refDiff, inverseTwoH * (Vq * alg[0] - Vd * alg[1]));
    matrixData.assign(refDiff + 1, refDiff + 1, -inverseTwoH * D - stateData.cj);
    matrixData.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], inverseTwoH);
    if (angleLoc != kNullLocation) {
        matrixData.assign(refDiff + 1, angleLoc, -inverseTwoH * (Vq * alg[0] - Vd * alg[1]));
    }
    if (voltageLoc != kNullLocation) {
        matrixData.assign(refDiff + 1,
                          voltageLoc,
                          -inverseTwoH * (Vd * alg[0] + Vq * alg[1]) / inputs[VOLTAGE_IN_LOCATION]);
    }

    const double psi2q = coefficients.mGq1 * state[2] + (1.0 - coefficients.mGq1) * state[4];
    const double psi2d = coefficients.mGd1 * state[3] + (1.0 - coefficients.mGd1) * state[5];
    const double fluxMagnitude = std::hypot(psi2d, psi2q);
    const auto saturation = sat.evaluate(fluxMagnitude);
    double dSaturationD = 0.0;
    double dSaturationQ = 0.0;
    if (fluxMagnitude > 0.0) {
        dSaturationD = saturation.derivative * psi2d / fluxMagnitude;
        dSaturationQ = saturation.derivative * psi2q / fluxMagnitude;
    }
    const double dQSatD = coefficients.mGqd * psi2q * dSaturationD;
    const double dQSatQ = coefficients.mGqd * (saturation.value + psi2q * dSaturationQ);
    const double dDSatD = saturation.value + psi2d * dSaturationD;
    const double dDSatQ = psi2d * dSaturationQ;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 2, refAlg + 1, -(Xq - Xqp) * coefficients.mGq1 / Tqop);
    }
    matrixData.assign(refDiff + 2,
                      refDiff + 2,
                      (-1.0 - (Xq - Xqp) * coefficients.mGq2 - dQSatQ * coefficients.mGq1) / Tqop -
                          stateData.cj);
    matrixData.assign(refDiff + 2,
                      refDiff + 4,
                      ((Xq - Xqp) * coefficients.mGq2 - dQSatQ * (1.0 - coefficients.mGq1)) / Tqop);
    matrixData.assign(refDiff + 2, refDiff + 3, -dQSatD * coefficients.mGd1 / Tqop);
    matrixData.assign(refDiff + 2, refDiff + 5, -dQSatD * (1.0 - coefficients.mGd1) / Tqop);

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 3, refAlg, (Xd - Xdp) * coefficients.mGd1 / Tdop);
    }
    matrixData.assign(refDiff + 3,
                      refDiff + 3,
                      (-1.0 - (Xd - Xdp) * coefficients.mGd2 - dDSatD * coefficients.mGd1) / Tdop -
                          stateData.cj);
    matrixData.assign(refDiff + 3,
                      refDiff + 5,
                      ((Xd - Xdp) * coefficients.mGd2 - dDSatD * (1.0 - coefficients.mGd1)) / Tdop);
    matrixData.assign(refDiff + 3, refDiff + 2, -dDSatQ * coefficients.mGq1 / Tdop);
    matrixData.assign(refDiff + 3, refDiff + 4, -dDSatQ * (1.0 - coefficients.mGq1) / Tdop);
    matrixData.assignCheckCol(refDiff + 3, inputLocs[genModelEftInLocation], 1.0 / Tdop);

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 4, refAlg + 1, -(Xqp - Xl) / Tqopp);
    }
    matrixData.assign(refDiff + 4, refDiff + 2, 1.0 / Tqopp);
    matrixData.assign(refDiff + 4, refDiff + 4, -1.0 / Tqopp - stateData.cj);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 5, refAlg, (Xdp - Xl) / Tdopp);
    }
    matrixData.assign(refDiff + 5, refDiff + 3, 1.0 / Tdopp);
    matrixData.assign(refDiff + 5, refDiff + 5, -1.0 / Tdopp - stateData.cj);
}

IOdata GenModelGENROU::getMachineControllerSignals(const IOdata& inputs,
                                                   const StateData& stateDataValue,
                                                   const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double* alg = locations.algStateLoc;
    const double* state = locations.diffStateLoc;
    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);

    const double angleDifference = state[0] - inputs[ANGLE_IN_LOCATION];
    const double directVoltage = -inputs[VOLTAGE_IN_LOCATION] * std::sin(angleDifference);
    const double quadratureVoltage = inputs[VOLTAGE_IN_LOCATION] * std::cos(angleDifference);
    const double psi2q = coefficients.mGq1 * state[2] + (1.0 - coefficients.mGq1) * state[4];
    const double psi2d = coefficients.mGd1 * state[3] + (1.0 - coefficients.mGd1) * state[5];
    const double saturation = sat.compute(std::hypot(psi2d, psi2q));
    const double electricalPower = directVoltage * alg[0] + quadratureVoltage * alg[1];
    const double electricalTorque = electricalPower + Rs * (alg[0] * alg[0] + alg[1] * alg[1]);
    const double xadIfd = state[3] +
        (Xd - Xdp) *
            (-coefficients.mGd1 * alg[0] - coefficients.mGd2 * state[5] +
             coefficients.mGd2 * state[3]) +
        saturation * psi2d;

    IOdata signals(machineControllerSignalCount, kNullVal);
    signals[static_cast<index_t>(MachineControllerSignal::ID)] = alg[0];
    signals[static_cast<index_t>(MachineControllerSignal::IQ)] = alg[1];
    signals[static_cast<index_t>(MachineControllerSignal::VD)] = directVoltage;
    signals[static_cast<index_t>(MachineControllerSignal::VQ)] = quadratureVoltage;
    signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)] = electricalPower;
    signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE)] = electricalTorque;
    signals[static_cast<index_t>(MachineControllerSignal::XADIFD)] = xadIfd;
    return signals;
}

MachineSignalDerivativeData
    GenModelGENROU::getMachineControllerSignalDerivatives(const IOdata& inputs,
                                                          const StateData& stateDataValue,
                                                          const IOlocs& inputLocs,
                                                          const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double* alg = locations.algStateLoc;
    const double* state = locations.diffStateLoc;
    const auto refAlg = locations.algOffset;
    const auto refDiff = locations.diffOffset;
    const auto coefficients = computeCoefficients(Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Xl);

    MachineSignalDerivativeData derivatives;
    const auto addDerivative =
        [&derivatives](MachineControllerSignal signal, index_t location, double value) {
            if ((location != kNullLocation) && (location != kInvalidLocation) && (value != 0.0)) {
                derivatives[static_cast<index_t>(signal)].push_back(
                    {.location = location, .value = value});
            }
        };

    addDerivative(MachineControllerSignal::ID, refAlg, 1.0);
    addDerivative(MachineControllerSignal::IQ, refAlg + 1, 1.0);

    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angleDifference = state[0] - inputs[ANGLE_IN_LOCATION];
    const double directVoltage = -voltage * std::sin(angleDifference);
    const double quadratureVoltage = voltage * std::cos(angleDifference);
    const double inverseVoltage = (voltage != 0.0) ? 1.0 / voltage : 0.0;
    const index_t voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    const index_t angleLoc = inputLocs[ANGLE_IN_LOCATION];

    addDerivative(MachineControllerSignal::VD, voltageLoc, directVoltage * inverseVoltage);
    addDerivative(MachineControllerSignal::VD, angleLoc, quadratureVoltage);
    addDerivative(MachineControllerSignal::VD, refDiff, -quadratureVoltage);
    addDerivative(MachineControllerSignal::VQ, voltageLoc, quadratureVoltage * inverseVoltage);
    addDerivative(MachineControllerSignal::VQ, angleLoc, -directVoltage);
    addDerivative(MachineControllerSignal::VQ, refDiff, directVoltage);

    addDerivative(MachineControllerSignal::ELECTRICAL_POWER, refAlg, directVoltage);
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER, refAlg + 1, quadratureVoltage);
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  voltageLoc,
                  (directVoltage * alg[0] + quadratureVoltage * alg[1]) * inverseVoltage);
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  angleLoc,
                  quadratureVoltage * alg[0] - directVoltage * alg[1]);
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  refDiff,
                  -quadratureVoltage * alg[0] + directVoltage * alg[1]);

    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  refAlg,
                  directVoltage + 2.0 * Rs * alg[0]);
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  refAlg + 1,
                  quadratureVoltage + 2.0 * Rs * alg[1]);
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  voltageLoc,
                  (directVoltage * alg[0] + quadratureVoltage * alg[1]) * inverseVoltage);
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  angleLoc,
                  quadratureVoltage * alg[0] - directVoltage * alg[1]);
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  refDiff,
                  -quadratureVoltage * alg[0] + directVoltage * alg[1]);

    const double psi2q = coefficients.mGq1 * state[2] + (1.0 - coefficients.mGq1) * state[4];
    const double psi2d = coefficients.mGd1 * state[3] + (1.0 - coefficients.mGd1) * state[5];
    const double fluxMagnitude = std::hypot(psi2d, psi2q);
    const auto saturation = sat.evaluate(fluxMagnitude);
    double dSaturationD = 0.0;
    double dSaturationQ = 0.0;
    if (fluxMagnitude > 0.0) {
        dSaturationD = saturation.derivative * psi2d / fluxMagnitude;
        dSaturationQ = saturation.derivative * psi2q / fluxMagnitude;
    }
    const double dSaturationTermD = saturation.value + psi2d * dSaturationD;
    const double dSaturationTermQ = psi2d * dSaturationQ;
    const double reactanceDifference = Xd - Xdp;

    addDerivative(MachineControllerSignal::XADIFD,
                  refAlg,
                  -reactanceDifference * coefficients.mGd1);
    addDerivative(MachineControllerSignal::XADIFD,
                  refDiff + 2,
                  dSaturationTermQ * coefficients.mGq1);
    addDerivative(MachineControllerSignal::XADIFD,
                  refDiff + 3,
                  1.0 + reactanceDifference * coefficients.mGd2 +
                      dSaturationTermD * coefficients.mGd1);
    addDerivative(MachineControllerSignal::XADIFD,
                  refDiff + 4,
                  dSaturationTermQ * (1.0 - coefficients.mGq1));
    addDerivative(MachineControllerSignal::XADIFD,
                  refDiff + 5,
                  -reactanceDifference * coefficients.mGd2 +
                      dSaturationTermD * (1.0 - coefficients.mGd1));
    return derivatives;
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

double GenModelGENROU::get(std::string_view param, units::unit unitType) const
{
    if (param == "h") {
        return H;
    }
    if (param == "d") {
        return D;
    }
    if ((param == "r") || (param == "rs") || (param == "ra")) {
        return Rs;
    }
    if (param == "xl") {
        return Xl;
    }
    if (param == "xd") {
        return Xd;
    }
    if (param == "xq") {
        return Xq;
    }
    if (param == "xdp") {
        return Xdp;
    }
    if (param == "xqp") {
        return Xqp;
    }
    if ((param == "xdpp") || (param == "xpp")) {
        return Xdpp;
    }
    if (param == "xqpp") {
        return Xqpp;
    }
    if ((param == "tdop") || (param == "td0p")) {
        return Tdop;
    }
    if ((param == "tdopp") || (param == "td0pp")) {
        return Tdopp;
    }
    if ((param == "tqop") || (param == "tq0p")) {
        return Tqop;
    }
    if ((param == "tqopp") || (param == "tq0pp")) {
        return Tqopp;
    }
    if ((param == "s1") || (param == "s10")) {
        return S10;
    }
    if (param == "s12") {
        return S12;
    }
    return GenModel5::get(param, unitType);
}

static const stringVec
    GEN_MODEL_GENROU_NAMES{"id", "iq", "delta", "freq", "e1d", "e1q", "e2q", "e2d"};

stringVec GenModelGENROU::localStateNames() const
{
    return GEN_MODEL_GENROU_NAMES;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::genmodels
