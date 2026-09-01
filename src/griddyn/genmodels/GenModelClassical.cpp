/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelClassical.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactory.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <complex>
#include <string>

namespace griddyn::genmodels {
// GENCLS follows standard dq-machine notation and the compact state aliases
// used throughout this established model implementation.
// NOLINTBEGIN(readability-identifier-length)
GenModelClassical::GenModelClassical(const std::string& objName): GenModel(objName)
{
    opFlags.set(INTERNAL_FREQUENCY_CALCULATION);
    Xd = 0.2;
}

CoreObject* GenModelClassical::clone(CoreObject* obj) const
{
    auto* gd = cloneBase<GenModelClassical, GenModel>(this, obj);
    if (gd == nullptr) {
        return obj;
    }
    gd->H = H;
    gd->D = D;
    return gd;
}

void GenModelClassical::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(H) || !std::isfinite(D) || !std::isfinite(Rs) || !std::isfinite(Xd) ||
        (H < 0.0) || (Xd <= 0.0)) {
        throw InvalidParameterValue("GENCLS inertia, damping, or impedance");
    }
    offsets.local().local.diffSize = 2;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 17;
}

// initial conditions
void GenModelClassical::dynObjectInitializeB(const IOdata& inputs,
                                             const IOdata& desiredOutput,
                                             IOdata& fieldSet)
{
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xd);
    const double* gm = m_state.data();
    const double internalVoltage = Vq + (Rs * gm[1]) - (Xd * gm[0]);
    // Pm includes stator copper loss, matching the ANDES air-gap torque
    // initialization. It reduces to terminal active power when Rs is zero.
    fieldSet[genModelEftInLocation] = internalVoltage;
    fieldSet[genModelPmechInLocation] = internalVoltage * gm[1];
}

void GenModelClassical::computeInitialAngleAndCurrent(const IOdata& inputs,
                                                      const IOdata& desiredOutput,
                                                      double r1,
                                                      double x1)
{
    double* gm = m_state.data();
    double const v = inputs[VOLTAGE_IN_LOCATION];
    double const theta = inputs[ANGLE_IN_LOCATION];
    std::complex<double> const ss(desiredOutput[0], -desiredOutput[1]);
    std::complex<double> const vv = std::polar(v, theta);
    std::complex<double> const ii = ss / conj(vv);
    gm[2] = std::arg(vv + std::complex<double>(r1, x1) * ii);

    gm[3] = 1.0;
    double const angle = gm[2] - theta;

    // Id and Iq
    Vq = v * cos(angle);
    Vd = -v * sin(angle);

    std::complex<double> const idq = ii * std::polar(1.0, -(gm[2] - (kPI / 2)));

    gm[0] = -idq.real();
    gm[1] = idq.imag();
}

void GenModelClassical::updateLocalCache(const IOdata& inputs,
                                         const StateData& sD,
                                         const SolverMode& sMode)
{
    if (sD.updateRequired(seqId)) {
        auto loc = offsets.getLocations(sD, sMode, this);
        double const v = inputs[VOLTAGE_IN_LOCATION];
        double const angle = loc.diffStateLoc[0] - inputs[ANGLE_IN_LOCATION];
        Vq = v * cos(angle);
        Vd = -v * sin(angle);
        seqId = sD.seqID;
    }
}

void GenModelClassical::algebraicUpdate(const IOdata& inputs,
                                        const StateData& sD,
                                        double update[],
                                        const SolverMode& sMode,
                                        double /*alpha*/)
{
    auto loc = offsets.getLocations(sD, update, sMode, this);
    updateLocalCache(inputs, sD, sMode);
    gmlc::utilities::solve2x2(Rs,
                              (Xd),
                              -(Xd),
                              Rs,
                              -Vd,
                              inputs[genModelEftInLocation] - Vq,
                              loc.destLoc[0],
                              loc.destLoc[1]);
    m_output = -((loc.destLoc[1] * Vq) + (loc.destLoc[0] * Vd));
}

// residual

void GenModelClassical::residual(const IOdata& inputs,
                                 const StateData& sD,
                                 double resid[],
                                 const SolverMode& sMode)
{
    auto loc = offsets.getLocations(sD, resid, sMode, this);

    updateLocalCache(inputs, sD, sMode);

    const double* gm = loc.algStateLoc;
    const double* gmd = loc.diffStateLoc;
    const double* gmp = loc.dstateLoc;

    double* rva = loc.destLoc;
    double* rvd = loc.destDiffLoc;

    const double internalVoltage = inputs[genModelEftInLocation];
    const double mechanicalPower = inputs[genModelPmechInLocation];

    if (hasAlgebraic(sMode)) {
        rva[0] = Vd + (Rs * gm[0]) + (Xd * gm[1]);
        rva[1] = Vq + (Rs * gm[1]) - (Xd * gm[0]) - internalVoltage;
    }

    if (hasDifferential(sMode)) {
        if (H > 0.0) {
            rvd[0] = (systemBaseFrequency * (gmd[1] - 1.0)) - gmp[0];
            const double electricalPower = internalVoltage * gm[1];
            rvd[1] =
                ((mechanicalPower - electricalPower - (D * (gmd[1] - 1.0))) / (2.0 * H)) - gmp[1];
        } else {
            rvd[0] = -gmp[0];
            rvd[1] = -gmp[1];
        }
    }
}

void GenModelClassical::derivative(const IOdata& inputs,
                                   const StateData& sD,
                                   double deriv[],
                                   const SolverMode& sMode)
{
    auto loc = offsets.getLocations(sD, deriv, sMode, this);
    double* dv = loc.destDiffLoc;
    if (H > 0.0) {
        const double speedDeviation = loc.diffStateLoc[1] - 1.0;
        const double electricalPower = inputs[genModelEftInLocation] * loc.algStateLoc[1];
        dv[0] = systemBaseFrequency * speedDeviation;
        dv[1] =
            (inputs[genModelPmechInLocation] - electricalPower - (D * speedDeviation)) / (2.0 * H);
    } else {
        dv[0] = 0.0;
        dv[1] = 0.0;
    }
}

double GenModelClassical::getFreq(const StateData& sD,
                                  const SolverMode& sMode,
                                  index_t* freqOffset) const
{
    double omega{1.0};

    if (isLocal(sMode)) {
        if (m_state.size() > 3) {
            omega = m_state[3];
        }
        if (freqOffset != nullptr) {
            *freqOffset = kNullLocation;
        }
        return omega;
    }

    if (!sD.empty()) {
        auto loc = offsets.getLocations(sD, sMode, this);

        omega = loc.diffStateLoc[1];

        if (freqOffset != nullptr) {
            *freqOffset = loc.diffOffset + 1;
            if (isAlgebraicOnly(sMode)) {
                *freqOffset = kNullLocation;
            }
        }
    } else if (freqOffset != nullptr) {
        *freqOffset = offsets.getDiffOffset(sMode) + 1;
    }
    return omega;
}

double GenModelClassical::getAngle(const StateData& sD,
                                   const SolverMode& sMode,
                                   index_t* angleOffset) const
{
    double angle = kNullVal;

    if (isLocal(sMode)) {
        if (m_state.size() > 2) {
            angle = m_state[2];
        }
        if (angleOffset != nullptr) {
            *angleOffset = kNullLocation;
        }
        return angle;
    }

    if (!sD.empty()) {
        auto loc = offsets.getLocations(sD, sMode, this);

        angle = loc.diffStateLoc[0];

        if (angleOffset != nullptr) {
            *angleOffset = loc.diffOffset;
            if (isAlgebraicOnly(sMode)) {
                *angleOffset = kNullLocation;
            }
        }
    } else if (angleOffset != nullptr) {
        *angleOffset = offsets.getDiffOffset(sMode);
    }
    return angle;
}

IOdata GenModelClassical::getOutputs(const IOdata& /*inputs*/,
                                     const StateData& sD,
                                     const SolverMode& sMode) const
{
    auto loc = offsets.getLocations(sD, sMode, this);
    IOdata out(2);
    out[POUT_LOCATION] = -((loc.algStateLoc[1] * Vq) + (loc.algStateLoc[0] * Vd));
    out[QOUT_LOCATION] = -((loc.algStateLoc[1] * Vd) - (loc.algStateLoc[0] * Vq));
    return out;
}

double GenModelClassical::getOutput(const IOdata& inputs,
                                    const StateData& sD,
                                    const SolverMode& sMode,
                                    index_t numOut) const
{
    auto loc = offsets.getLocations(sD, sMode, this);
    double vqtemp = Vq;
    double vdtemp = Vd;
    if ((sD.empty()) || (sD.seqID != seqId) || (sD.seqID == 0)) {
        double const v = inputs[VOLTAGE_IN_LOCATION];
        double const angle = loc.diffStateLoc[0] - inputs[ANGLE_IN_LOCATION];
        vqtemp = v * cos(angle);
        vdtemp = -v * sin(angle);
    }

    if (numOut == POUT_LOCATION) {
        return -((loc.algStateLoc[1] * vqtemp) + (loc.algStateLoc[0] * vdtemp));
    }
    if (numOut == QOUT_LOCATION) {
        return -((loc.algStateLoc[1] * vdtemp) - (loc.algStateLoc[0] * vqtemp));
    }
    return kNullVal;
}

void GenModelClassical::ioPartialDerivatives(const IOdata& inputs,
                                             const StateData& sD,
                                             MatrixData<double>& md,
                                             const IOlocs& inputLocs,
                                             const SolverMode& sMode)
{
    auto loc = offsets.getLocations(sD, sMode, this);

    double const v = inputs[VOLTAGE_IN_LOCATION];
    updateLocalCache(inputs, sD, sMode);

    const double* gm = loc.algStateLoc;

    if (inputLocs[ANGLE_IN_LOCATION] != kNullLocation) {
        md.assign(POUT_LOCATION, inputLocs[ANGLE_IN_LOCATION], (gm[1] * Vd) - (gm[0] * Vq));
        md.assign(QOUT_LOCATION, inputLocs[ANGLE_IN_LOCATION], (-gm[1] * Vq) - (gm[0] * Vd));
    }
    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        md.assign(POUT_LOCATION,
                  inputLocs[VOLTAGE_IN_LOCATION],
                  (-gm[1] * Vq / v) - (gm[0] * Vd / v));
        md.assign(QOUT_LOCATION,
                  inputLocs[VOLTAGE_IN_LOCATION],
                  (-gm[1] * Vd / v) + (gm[0] * Vq / v));
    }
}

void GenModelClassical::jacobianElements(const IOdata& inputs,
                                         const StateData& sD,
                                         MatrixData<double>& md,
                                         const IOlocs& inputLocs,
                                         const SolverMode& sMode)
{
    auto loc = offsets.getLocations(sD, sMode, this);

    updateLocalCache(inputs, sD, sMode);

    const double* gm = loc.algStateLoc;
    auto vLoc = inputLocs[VOLTAGE_IN_LOCATION];
    auto tLoc = inputLocs[ANGLE_IN_LOCATION];
    auto refAlg = loc.algOffset;
    auto refDiff = loc.diffOffset;

    // rva[0] = Vd + Rs * gm[0] + Xd * gm[1];
    // rva[1] = Vq + Rs * gm[1] - Xd * gm[0] - Eft;
    if (hasAlgebraic(sMode)) {
        if (tLoc != kNullLocation) {
            md.assign(refAlg, tLoc, Vq);
            md.assign(refAlg + 1, tLoc, -Vd);
        }

        // Q
        if (vLoc != kNullLocation) {
            md.assign(refAlg, vLoc, Vd / inputs[VOLTAGE_IN_LOCATION]);
            md.assign(refAlg + 1, vLoc, Vq / inputs[VOLTAGE_IN_LOCATION]);
        }

        md.assign(refAlg, refAlg, Rs);
        md.assign(refAlg, refAlg + 1, (Xd));

        md.assign(refAlg + 1, refAlg, -(Xd));
        md.assign(refAlg + 1, refAlg + 1, Rs);
        md.assignCheckCol(refAlg + 1, inputLocs[genModelEftInLocation], -1.0);

        if (isAlgebraicOnly(sMode)) {
            return;
        }
        md.assign(refAlg, refDiff, -Vq);
        md.assign(refAlg + 1, refDiff, Vd);
    }

    if (hasDifferential(sMode)) {
        md.assign(refDiff, refDiff, -sD.cj);
        if (H > 0.0) {
            md.assign(refDiff, refDiff + 1, systemBaseFrequency);
            const double inverseInertia = 0.5 / H;
            if (hasAlgebraic(sMode)) {
                md.assign(refDiff + 1, refAlg + 1, -inverseInertia * inputs[genModelEftInLocation]);
            }
            md.assign(refDiff + 1, refDiff + 1, (-inverseInertia * D) - sD.cj);
            md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], inverseInertia);
            md.assignCheckCol(refDiff + 1,
                              inputLocs[genModelEftInLocation],
                              -inverseInertia * gm[1]);
        } else {
            md.assign(refDiff + 1, refDiff + 1, -sD.cj);
        }
    }
}

void GenModelClassical::outputPartialDerivatives(const IOdata& inputs,
                                                 const StateData& sD,
                                                 MatrixData<double>& md,
                                                 const SolverMode& sMode)
{
    auto loc = offsets.getLocations(sD, sMode, this);
    auto refAlg = loc.algOffset;
    auto refDiff = loc.diffOffset;

    const double* gm = loc.algStateLoc;

    updateLocalCache(inputs, sD, sMode);
    if (hasAlgebraic(sMode)) {
        // output P
        md.assign(POUT_LOCATION, refAlg, -Vd);
        md.assign(POUT_LOCATION, refAlg + 1, -Vq);

        // output Q
        md.assign(QOUT_LOCATION, refAlg, Vq);
        md.assign(QOUT_LOCATION, refAlg + 1, -Vd);
    }

    if (hasDifferential(sMode)) {
        md.assign(POUT_LOCATION, refDiff, (-gm[1] * Vd) + (gm[0] * Vq));
        md.assign(QOUT_LOCATION, refDiff, (gm[1] * Vq) + (gm[0] * Vd));
    }
}

count_t GenModelClassical::outputDependencyCount(index_t /*num*/, const SolverMode& /*sMode*/) const
{
    return 3;
}

static const stringVec GEN_MODEL_CLASSIC_STATE_NAMES{"id", "iq", "delta", "freq"};

stringVec GenModelClassical::localStateNames() const
{
    return GEN_MODEL_CLASSIC_STATE_NAMES;
}
// set parameters
void GenModelClassical::set(std::string_view param, std::string_view val)
{
    CoreObject::set(param, val);
}
void GenModelClassical::set(std::string_view param, double val, units::unit unitType)
{
    if (param.length() == 1) {
        switch (param[0]) {
            case 'x':
                Xd = val;
                break;
            case 'h':
                H = val;
                break;
            case 'r':
                Rs = val;
                break;
            case 'm':
                H = val / 2.0;
                break;
            case 'd':
                D = units::convert(val, unitType, units::puHz, systemBaseFrequency);
                break;

            default:
                throw(UnrecognizedParameter(param));
        }
        return;
    }

    if (param == "kw") {
        // Retained as an accepted legacy/PSAT parameter. GENCLS has no
        // voltage-speed feedback term; damping belongs only in the swing
        // equation.
        return;
    }
    if ((param == "xd1") || (param == "xdp")) {
        Xd = val;
    } else if (param == "ra") {
        Rs = val;
    } else {
        GenModel::set(param, val, unitType);
    }
}

double GenModelClassical::get(std::string_view param, units::unit unitType) const
{
    if (param == "h") {
        return H;
    }
    if (param == "m") {
        return 2.0 * H;
    }
    if (param == "d") {
        return units::convert(D, units::puHz, unitType, systemBaseFrequency);
    }
    if ((param == "x") || (param == "xd") || (param == "xs") || (param == "xd1") ||
        (param == "xdp")) {
        return Xd;
    }
    if ((param == "r") || (param == "rs") || (param == "ra")) {
        return Rs;
    }
    return GenModel::get(param, unitType);
}

}  // namespace griddyn::genmodels
// NOLINTEND(readability-identifier-length)
