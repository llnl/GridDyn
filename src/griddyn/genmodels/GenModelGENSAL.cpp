/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelGENSAL.h"

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
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    struct GensalCoefficients {
        double mK1d;
        double mK3d;
        double mK4d;
    };

    GensalCoefficients coefficients(double directReactance,
                                    double directTransientReactance,
                                    double directSubtransientReactance,
                                    double leakageReactance)
    {
        const double denominator = directTransientReactance - leakageReactance;
        return {.mK1d = (directTransientReactance - directSubtransientReactance) *
                    (directReactance - directTransientReactance) / (denominator * denominator),
                .mK3d = (directSubtransientReactance - leakageReactance) / denominator,
                .mK4d = (directTransientReactance - directSubtransientReactance) / denominator};
    }

    void addSignalDerivative(MachineSignalDerivativeData& data,
                             MachineControllerSignal signal,
                             index_t location,
                             double value)
    {
        if ((location != kNullLocation) && (location != kInvalidLocation) && (value != 0.0)) {
            data[static_cast<index_t>(signal)].push_back({.location = location, .value = value});
        }
    }
}  // namespace

GenModelGENSAL::GenModelGENSAL(const std::string& objName): GenModel5(objName)
{
    Xdpp = 0.2;
    Xqpp = 0.2;
    S10 = 0.0;
    S12 = 1.0;
    sat.setType(utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    sat.setParam(S10, S12);
}

CoreObject* GenModelGENSAL::clone(CoreObject* obj) const
{
    auto* result = cloneBase<GenModelGENSAL, GenModel5>(this, obj);
    if (result != nullptr) {
        result->sat = sat;
    }
    return (result != nullptr) ? result : obj;
}

void GenModelGENSAL::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    constexpr double tolerance = 1e-9;
    if ((H <= 0.0) || (Tdop <= 0.0) || (Tdopp <= 0.0) || (Tqopp <= 0.0) || (Xd <= Xdp) ||
        (Xdp < Xdpp) || (Xq < Xqpp) || (std::abs(Xdp - Xl) <= tolerance) ||
        (std::abs(Xdp - Xdpp) <= tolerance)) {
        throw InvalidParameterValue("GENSAL reactances, inertia, or time constants");
    }
    offsets.local().local.algSize = 2;
    offsets.local().local.diffSize = 5;
    offsets.local().local.jacSize = 42;
}

void GenModelGENSAL::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angle = inputs[ANGLE_IN_LOCATION];
    const std::complex<double> terminalVoltage = std::polar(voltage, angle);
    const std::complex<double> terminalPower(desiredOutput[POUT_LOCATION],
                                             -desiredOutput[QOUT_LOCATION]);
    const std::complex<double> terminalCurrent = terminalPower / std::conj(terminalVoltage);
    const std::complex<double> subtransientFlux =
        terminalVoltage + std::complex<double>(Rs, Xdpp) * terminalCurrent;
    const std::complex<double> epqp =
        subtransientFlux + std::complex<double>(0.0, Xq - Xdpp) * terminalCurrent;
    const double rotorAngle = std::arg(epqp);
    const std::complex<double> rotation = std::polar(1.0, -rotorAngle);
    const std::complex<double> currentDq = std::conj(terminalCurrent * rotation);
    const std::complex<double> fluxDq = subtransientFlux * rotation;
    const double directCurrent = -std::imag(currentDq);
    const double quadratureCurrent = std::real(currentDq);
    const double psi2d = std::real(fluxDq);
    const double psi2q = std::imag(fluxDq);
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    const double epq = psi2d - (Xdp - Xdpp) * directCurrent;
    const double psikd = (psi2d - coeff.mK3d * epq) / coeff.mK4d;
    const double saturation = sat.compute(epq);
    const double efd = (1.0 + saturation) * epq - (Xd - Xdp) * directCurrent;

    m_state[0] = directCurrent;
    m_state[1] = quadratureCurrent;
    m_state[2] = rotorAngle;
    m_state[3] = 1.0;
    m_state[4] = epq;
    m_state[5] = psikd;
    m_state[6] = psi2q;
    Vd = -voltage * std::sin(rotorAngle - angle);
    Vq = voltage * std::cos(rotorAngle - angle);
    fieldSet[genModelEftInLocation] = efd;
    fieldSet[genModelPmechInLocation] =
        (Vd + Rs * directCurrent) * directCurrent + (Vq + Rs * quadratureCurrent) *
            quadratureCurrent;
}

void GenModelGENSAL::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    auto loc = offsets.getLocations(stateData, update, sMode, this);
    updateLocalCache(inputs, stateData, sMode);
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    const double psi2d = coeff.mK3d * loc.diffStateLoc[2] + coeff.mK4d * loc.diffStateLoc[3];
    gmlc::utilities::solve2x2(
        Rs, Xqpp, -Xdpp, Rs, loc.diffStateLoc[4] - Vd, psi2d - Vq, loc.destLoc[0], loc.destLoc[1]);
    m_output = -(loc.destLoc[0] * Vd + loc.destLoc[1] * Vq);
}

void GenModelGENSAL::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* alg = loc.algStateLoc;
    const double* state = loc.diffStateLoc;
    double* dst = loc.destDiffLoc;
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    const double epq = state[2];
    const double psikd = state[3];
    const double xadIfd = coeff.mK1d * (epq - psikd + (Xdp - Xl) * alg[0]) -
        (Xd - Xdp) * alg[0] + (1.0 + sat.compute(epq)) * epq;
    const double torque = (Vd + Rs * alg[0]) * alg[0] + (Vq + Rs * alg[1]) * alg[1];
    dst[0] = systemBaseFrequency * (state[1] - 1.0);
    dst[1] = (inputs[genModelPmechInLocation] - torque - D * (state[1] - 1.0)) / (2.0 * H);
    dst[2] = (inputs[genModelEftInLocation] - xadIfd) / Tdop;
    dst[3] = (epq - psikd + (Xdp - Xl) * alg[0]) / Tdopp;
    dst[4] = (-state[4] - (Xq - Xqpp) * alg[1]) / Tqopp;
}

void GenModelGENSAL::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, resid, sMode, this);
    updateLocalCache(inputs, stateData, sMode);
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    if (hasAlgebraic(sMode)) {
        const double psi2d =
            coeff.mK3d * loc.diffStateLoc[2] + coeff.mK4d * loc.diffStateLoc[3];
        loc.destLoc[0] =
            Vd + Rs * loc.algStateLoc[0] + Xqpp * loc.algStateLoc[1] - loc.diffStateLoc[4];
        loc.destLoc[1] = Vq + Rs * loc.algStateLoc[1] - Xdpp * loc.algStateLoc[0] - psi2d;
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t ii = 0; ii < 5; ++ii) {
            loc.destDiffLoc[ii] -= loc.dstateLoc[ii];
        }
    }
}

void GenModelGENSAL::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto algebraicRow = loc.algOffset;
    const auto differentialRow = loc.diffOffset;
    const double* alg = loc.algStateLoc;
    const double* state = loc.diffStateLoc;
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    updateLocalCache(inputs, stateData, sMode);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(algebraicRow, algebraicRow, Rs);
        matrixData.assign(algebraicRow, algebraicRow + 1, Xqpp);
        matrixData.assign(algebraicRow + 1, algebraicRow, -Xdpp);
        matrixData.assign(algebraicRow + 1, algebraicRow + 1, Rs);
        matrixData.assignCheckCol(algebraicRow,
                                  inputLocs[VOLTAGE_IN_LOCATION],
                                  Vd / inputs[VOLTAGE_IN_LOCATION]);
        matrixData.assignCheckCol(algebraicRow + 1,
                                  inputLocs[VOLTAGE_IN_LOCATION],
                                  Vq / inputs[VOLTAGE_IN_LOCATION]);
        matrixData.assignCheckCol(algebraicRow, inputLocs[ANGLE_IN_LOCATION], Vq);
        matrixData.assignCheckCol(algebraicRow + 1, inputLocs[ANGLE_IN_LOCATION], -Vd);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(algebraicRow, differentialRow, -Vq);
            matrixData.assign(algebraicRow, differentialRow + 4, -1.0);
            matrixData.assign(algebraicRow + 1, differentialRow, Vd);
            matrixData.assign(algebraicRow + 1, differentialRow + 2, -coeff.mK3d);
            matrixData.assign(algebraicRow + 1, differentialRow + 3, -coeff.mK4d);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(differentialRow, differentialRow, -stateData.cj);
    matrixData.assign(differentialRow, differentialRow + 1, systemBaseFrequency);
    const double inverseInertiaFactor = 1.0 / (2.0 * H);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(differentialRow + 1,
                          algebraicRow,
                          -inverseInertiaFactor * (Vd + 2.0 * Rs * alg[0]));
        matrixData.assign(differentialRow + 1,
                          algebraicRow + 1,
                          -inverseInertiaFactor * (Vq + 2.0 * Rs * alg[1]));
    }
    matrixData.assign(differentialRow + 1,
                      differentialRow,
                      inverseInertiaFactor * (Vq * alg[0] - Vd * alg[1]));
    matrixData.assign(differentialRow + 1,
                      differentialRow + 1,
                      -D * inverseInertiaFactor - stateData.cj);
    matrixData.assignCheckCol(differentialRow + 1,
                              inputLocs[genModelPmechInLocation],
                              inverseInertiaFactor);
    matrixData.assignCheckCol(differentialRow + 1,
                              inputLocs[ANGLE_IN_LOCATION],
                              -inverseInertiaFactor * (Vq * alg[0] - Vd * alg[1]));
    matrixData.assignCheckCol(differentialRow + 1,
                              inputLocs[VOLTAGE_IN_LOCATION],
                              -inverseInertiaFactor * (Vd * alg[0] + Vq * alg[1]) /
                                  inputs[VOLTAGE_IN_LOCATION]);

    const auto saturation = sat.evaluate(state[2]);
    const double dSatEpq = 1.0 + saturation.value + state[2] * saturation.derivative;
    const double dXadId = coeff.mK1d * (Xdp - Xl) - (Xd - Xdp);
    const double dXadEpq = coeff.mK1d + dSatEpq;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(differentialRow + 2, algebraicRow, -dXadId / Tdop);
    }
    matrixData.assign(differentialRow + 2,
                      differentialRow + 2,
                      -dXadEpq / Tdop - stateData.cj);
    matrixData.assign(differentialRow + 2, differentialRow + 3, coeff.mK1d / Tdop);
    matrixData.assignCheckCol(differentialRow + 2,
                              inputLocs[genModelEftInLocation],
                              1.0 / Tdop);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(differentialRow + 3, algebraicRow, (Xdp - Xl) / Tdopp);
    }
    matrixData.assign(differentialRow + 3, differentialRow + 2, 1.0 / Tdopp);
    matrixData.assign(differentialRow + 3,
                      differentialRow + 3,
                      -1.0 / Tdopp - stateData.cj);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(differentialRow + 4, algebraicRow + 1, -(Xq - Xqpp) / Tqopp);
    }
    matrixData.assign(differentialRow + 4,
                      differentialRow + 4,
                      -1.0 / Tqopp - stateData.cj);
}

IOdata GenModelGENSAL::getMachineControllerSignals(const IOdata& inputs,
                                                   const StateData& stateData,
                                                   const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double angle = loc.diffStateLoc[0] - inputs[ANGLE_IN_LOCATION];
    const double directVoltage = -inputs[VOLTAGE_IN_LOCATION] * std::sin(angle);
    const double quadratureVoltage = inputs[VOLTAGE_IN_LOCATION] * std::cos(angle);
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    const double epq = loc.diffStateLoc[2];
    const double xadIfd =
        coeff.mK1d * (epq - loc.diffStateLoc[3] + (Xdp - Xl) * loc.algStateLoc[0]) -
        (Xd - Xdp) * loc.algStateLoc[0] + (1.0 + sat.compute(epq)) * epq;
    IOdata signals(machineControllerSignalCount, kNullVal);
    signals[static_cast<index_t>(MachineControllerSignal::ID)] = loc.algStateLoc[0];
    signals[static_cast<index_t>(MachineControllerSignal::IQ)] = loc.algStateLoc[1];
    signals[static_cast<index_t>(MachineControllerSignal::VD)] = directVoltage;
    signals[static_cast<index_t>(MachineControllerSignal::VQ)] = quadratureVoltage;
    signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)] =
        directVoltage * loc.algStateLoc[0] + quadratureVoltage * loc.algStateLoc[1];
    signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE)] =
        (directVoltage + Rs * loc.algStateLoc[0]) * loc.algStateLoc[0] +
        (quadratureVoltage + Rs * loc.algStateLoc[1]) * loc.algStateLoc[1];
    signals[static_cast<index_t>(MachineControllerSignal::XADIFD)] = xadIfd;
    return signals;
}

MachineSignalDerivativeData
    GenModelGENSAL::getMachineControllerSignalDerivatives(const IOdata& inputs,
                                                          const StateData& stateData,
                                                          const IOlocs& inputLocs,
                                                          const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto algebraicRow = loc.algOffset;
    const auto differentialRow = loc.diffOffset;
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angle = loc.diffStateLoc[0] - inputs[ANGLE_IN_LOCATION];
    const double directVoltage = -voltage * std::sin(angle);
    const double quadratureVoltage = voltage * std::cos(angle);
    const double inverseVoltage = (voltage != 0.0) ? 1.0 / voltage : 0.0;
    MachineSignalDerivativeData data;
    addSignalDerivative(data, MachineControllerSignal::ID, algebraicRow, 1.0);
    addSignalDerivative(data, MachineControllerSignal::IQ, algebraicRow + 1, 1.0);
    addSignalDerivative(data,
                        MachineControllerSignal::VD,
                        inputLocs[VOLTAGE_IN_LOCATION],
                        directVoltage * inverseVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::VD,
                        inputLocs[ANGLE_IN_LOCATION],
                        quadratureVoltage);
    addSignalDerivative(data, MachineControllerSignal::VD, differentialRow, -quadratureVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::VQ,
                        inputLocs[VOLTAGE_IN_LOCATION],
                        quadratureVoltage * inverseVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::VQ,
                        inputLocs[ANGLE_IN_LOCATION],
                        -directVoltage);
    addSignalDerivative(data, MachineControllerSignal::VQ, differentialRow, directVoltage);
    addSignalDerivative(data, MachineControllerSignal::ELECTRICAL_POWER, algebraicRow, directVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_POWER,
                        algebraicRow + 1,
                        quadratureVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_POWER,
                        inputLocs[VOLTAGE_IN_LOCATION],
                        (directVoltage * loc.algStateLoc[0] +
                         quadratureVoltage * loc.algStateLoc[1]) *
                            inverseVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_POWER,
                        inputLocs[ANGLE_IN_LOCATION],
                        quadratureVoltage * loc.algStateLoc[0] -
                            directVoltage * loc.algStateLoc[1]);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_POWER,
                        differentialRow,
                        -quadratureVoltage * loc.algStateLoc[0] +
                            directVoltage * loc.algStateLoc[1]);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_TORQUE,
                        algebraicRow,
                        directVoltage + 2.0 * Rs * loc.algStateLoc[0]);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_TORQUE,
                        algebraicRow + 1,
                        quadratureVoltage + 2.0 * Rs * loc.algStateLoc[1]);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_TORQUE,
                        inputLocs[VOLTAGE_IN_LOCATION],
                        (directVoltage * loc.algStateLoc[0] +
                         quadratureVoltage * loc.algStateLoc[1]) *
                            inverseVoltage);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_TORQUE,
                        inputLocs[ANGLE_IN_LOCATION],
                        quadratureVoltage * loc.algStateLoc[0] -
                            directVoltage * loc.algStateLoc[1]);
    addSignalDerivative(data,
                        MachineControllerSignal::ELECTRICAL_TORQUE,
                        differentialRow,
                        -quadratureVoltage * loc.algStateLoc[0] +
                            directVoltage * loc.algStateLoc[1]);
    const auto coeff = coefficients(Xd, Xdp, Xdpp, Xl);
    const auto saturation = sat.evaluate(loc.diffStateLoc[2]);
    addSignalDerivative(data,
                        MachineControllerSignal::XADIFD,
                        algebraicRow,
                        coeff.mK1d * (Xdp - Xl) - (Xd - Xdp));
    addSignalDerivative(data,
                        MachineControllerSignal::XADIFD,
                        differentialRow + 2,
                        coeff.mK1d + 1.0 + saturation.value +
                            loc.diffStateLoc[2] * saturation.derivative);
    addSignalDerivative(data, MachineControllerSignal::XADIFD, differentialRow + 3, -coeff.mK1d);
    return data;
}

void GenModelGENSAL::set(std::string_view param, std::string_view val)
{
    GenModel5::set(param, val);
}

void GenModelGENSAL::set(std::string_view param, double val, units::unit unitType)
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

double GenModelGENSAL::get(std::string_view param, units::unit unitType) const
{
    if (param == "h") {
        return H;
    }
    if (param == "d") {
        return D;
    }
    if ((param == "r") || (param == "rs")) {
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

stringVec GenModelGENSAL::localStateNames() const
{
    return {"id", "iq", "delta", "freq", "epq", "psikd", "psi2q"};
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::genmodels
