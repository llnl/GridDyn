/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelClassical.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactory.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <complex>
#include <string>

namespace griddyn::genmodels {
GenModelClassical::GenModelClassical(const std::string& objName): GenModel(objName)
{
    // default values
    opFlags.set(internal_frequency_calculation);
    Xd = 0.85;
}

CoreObject* GenModelClassical::clone(CoreObject* obj) const
{
    auto* gd = cloneBase<GenModelClassical, GenModel>(this, obj);
    if (gd == nullptr) {
        return obj;
    }
    gd->H = H;
    gd->D = D;
    gd->mp_Kw = mp_Kw;  //!< speed gain for a simple pss
    return gd;
}

void GenModelClassical::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 2;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 18;
}

// initial conditions
void GenModelClassical::dynObjectInitializeB(const IOdata& inputs,
                                             const IOdata& desiredOutput,
                                             IOdata& fieldSet)
{
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xd);
    double* gm = m_state.data();
    double Eft = Vq + Rs * gm[1] - Xd * gm[0];
    // record Pm = Pset
    // this should be close to P from above
    // preset the inputs that should be initialized
    fieldSet[2] = Eft;
    fieldSet[3] = Eft * gm[1];
}

void GenModelClassical::computeInitialAngleAndCurrent(const IOdata& inputs,
                                                      const IOdata& desiredOutput,
                                                      double R1,
                                                      double X1)
{
    double* gm = m_state.data();
    double V = inputs[voltageInLocation];
    double theta = inputs[angleInLocation];
    std::complex<double> SS(desiredOutput[0], -desiredOutput[1]);
    std::complex<double> VV = std::polar(V, theta);
    std::complex<double> II = SS / conj(VV);
    gm[2] = std::arg(VV + std::complex<double>(R1, X1) * II);

    gm[3] = 1.0;
    double angle = gm[2] - theta;

    // Id and Iq
    Vq = V * cos(angle);
    Vd = -V * sin(angle);

    std::complex<double> Idq = II * std::polar(1.0, -(gm[2] - kPI / 2));

    gm[0] = -Idq.real();
    gm[1] = Idq.imag();
}

void GenModelClassical::updateLocalCache(const IOdata& inputs,
                                         const stateData& sD,
                                         const solverMode& sMode)
{
    if (sD.updateRequired(seqId)) {
        auto Loc = offsets.getLocations(sD, sMode, this);
        double V = inputs[voltageInLocation];
        double angle = Loc.diffStateLoc[0] - inputs[angleInLocation];
        Vq = V * cos(angle);
        Vd = -V * sin(angle);
        seqId = sD.seqID;
    }
}

void GenModelClassical::algebraicUpdate(const IOdata& inputs,
                                        const stateData& sD,
                                        double update[],
                                        const solverMode& sMode,
                                        double /*alpha*/)
{
    auto Loc = offsets.getLocations(sD, update, sMode, this);
    updateLocalCache(inputs, sD, sMode);
    gmlc::utilities::solve2x2(Rs,
                              (Xd),
                              -(Xd),
                              Rs,
                              -Vd,
                              inputs[genModelEftInLocation] - Vq,
                              Loc.destLoc[0],
                              Loc.destLoc[1]);
    m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
}

// residual

void GenModelClassical::residual(const IOdata& inputs,
                                 const stateData& sD,
                                 double resid[],
                                 const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, resid, sMode, this);

    updateLocalCache(inputs, sD, sMode);

    const double* gm = Loc.algStateLoc;
    const double* gmd = Loc.diffStateLoc;
    const double* gmp = Loc.dstateLoc;

    double* rva = Loc.destLoc;
    double* rvd = Loc.destDiffLoc;

    // Get the exciter field
    double Eft = inputs[genModelEftInLocation];
    double Pmt = inputs[genModelPmechInLocation];

    /*
rva[0] = Vd + Rs * gm[0] + (Xqp)* gm[1] - gmd[2];
rva[1] = Vq + Rs * gm[1] - (Xdp)* gm[0] - gmd[3];

if (isAlgebraicOnly(sMode))
{
        return;
}
// delta
rvd[0] = systemBaseFrequency * (gmd[1] - 1.0) - gmp[0];
// Edp and Eqp
rvd[2] = (-gmd[2] - (Xq - Xqp) * gm[1]) / Tqop - gmp[2];
rvd[3] = (-gmd[3] + (Xd - Xdp) * gm[0] + Eft) / Tdop - gmp[3];

// omega
double Pe = gmd[2] * gm[0] + gmd[3] * gm[1] + (Xdp - Xqp) * gm[0] * gm[1];
rvd[1] = 0.5  * (Pmt - Pe - D * (gmd[1] - 1.0)) / H - gmp[1];
*/
    // Id and Iq

    if (hasAlgebraic(sMode)) {
        rva[0] = Vd + Rs * gm[0] + Xd * gm[1];
        rva[1] = Vq + Rs * gm[1] - Xd * gm[0] - Eft - mp_Kw * (gmd[1] - 1.0);
    }

    if (hasDifferential(sMode)) {
        // delta
        rvd[0] = systemBaseFrequency * (gmd[1] - 1.0) - gmp[0];

        // omega
        double Pe = (Eft + mp_Kw * (gmd[1] - 1.0)) * gm[1];
        rvd[1] = 0.5 * (Pmt - Pe - D * (gmd[1] - 1.0)) / H - gmp[1];
    }
}

void GenModelClassical::derivative(const IOdata& inputs,
                                   const stateData& sD,
                                   double deriv[],
                                   const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    double* dv = Loc.destDiffLoc;
    // Get the exciter field
    double Eft = inputs[genModelEftInLocation];
    double Pmt = inputs[genModelPmechInLocation];

    double omega = Loc.diffStateLoc[1] - 1.0;
    // Id and Iq

    // delta
    dv[0] = systemBaseFrequency * omega;
    // Edp and Eqp

    // omega
    double Pe = (Eft + mp_Kw * omega) * Loc.algStateLoc[1];
    dv[1] = 0.5 * (Pmt - Pe - D * omega) / H;
}

double GenModelClassical::getFreq(const stateData& sD,
                                  const solverMode& sMode,
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
        auto Loc = offsets.getLocations(sD, sMode, this);

        omega = Loc.diffStateLoc[1];

        if (freqOffset != nullptr) {
            *freqOffset = Loc.diffOffset + 1;
            if (isAlgebraicOnly(sMode)) {
                *freqOffset = kNullLocation;
            }
        }
    } else if (freqOffset != nullptr) {
        *freqOffset = offsets.getDiffOffset(sMode) + 1;
    }
    return omega;
}

double GenModelClassical::getAngle(const stateData& sD,
                                   const solverMode& sMode,
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
        auto Loc = offsets.getLocations(sD, sMode, this);

        angle = Loc.diffStateLoc[0];

        if (angleOffset != nullptr) {
            *angleOffset = Loc.diffOffset;
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
                                     const stateData& sD,
                                     const solverMode& sMode) const
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    IOdata out(2);
    out[PoutLocation] = -(Loc.algStateLoc[1] * Vq + Loc.algStateLoc[0] * Vd);
    out[QoutLocation] = -(Loc.algStateLoc[1] * Vd - Loc.algStateLoc[0] * Vq);
    return out;
}

double GenModelClassical::getOutput(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode,
                                    index_t numOut) const
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    double Vqtemp = Vq;
    double Vdtemp = Vd;
    if ((sD.empty()) || (sD.seqID != seqId) || (sD.seqID == 0)) {
        double V = inputs[voltageInLocation];
        double angle = Loc.diffStateLoc[0] - inputs[angleInLocation];
        Vqtemp = V * cos(angle);
        Vdtemp = -V * sin(angle);
    }

    if (numOut == PoutLocation) {
        return -(Loc.algStateLoc[1] * Vqtemp + Loc.algStateLoc[0] * Vdtemp);
    }
    if (numOut == QoutLocation) {
        return -(Loc.algStateLoc[1] * Vdtemp - Loc.algStateLoc[0] * Vqtemp);
    }
    return kNullVal;
}

void GenModelClassical::ioPartialDerivatives(const IOdata& inputs,
                                             const stateData& sD,
                                             matrixData<double>& md,
                                             const IOlocs& inputLocs,
                                             const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);

    double V = inputs[voltageInLocation];
    updateLocalCache(inputs, sD, sMode);

    const double* gm = Loc.algStateLoc;

    if (inputLocs[angleInLocation] != kNullLocation) {
        md.assign(PoutLocation, inputLocs[angleInLocation], gm[1] * Vd - gm[0] * Vq);
        md.assign(QoutLocation, inputLocs[angleInLocation], -gm[1] * Vq - gm[0] * Vd);
    }
    if (inputLocs[voltageInLocation] != kNullLocation) {
        md.assign(PoutLocation, inputLocs[voltageInLocation], -gm[1] * Vq / V - gm[0] * Vd / V);
        md.assign(QoutLocation, inputLocs[voltageInLocation], -gm[1] * Vd / V + gm[0] * Vq / V);
    }
}

void GenModelClassical::jacobianElements(const IOdata& inputs,
                                         const stateData& sD,
                                         matrixData<double>& md,
                                         const IOlocs& inputLocs,
                                         const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);

    updateLocalCache(inputs, sD, sMode);

    const double* gm = Loc.algStateLoc;
    auto VLoc = inputLocs[voltageInLocation];
    auto TLoc = inputLocs[angleInLocation];
    auto refAlg = Loc.algOffset;
    auto refDiff = Loc.diffOffset;

    // rva[0] = Vd + Rs * gm[0] + Xd * gm[1];
    // rva[1] = Vq + Rs * gm[1] - Xd* gm[0] - Eft-mp_Kp*(gmd[1]-1.0);
    if (hasAlgebraic(sMode)) {
        if (TLoc != kNullLocation) {
            md.assign(refAlg, TLoc, Vq);
            md.assign(refAlg + 1, TLoc, -Vd);
        }

        // Q
        if (VLoc != kNullLocation) {
            md.assign(refAlg, VLoc, Vd / inputs[voltageInLocation]);
            md.assign(refAlg + 1, VLoc, Vq / inputs[voltageInLocation]);
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

        md.assign(refAlg + 1, refDiff + 1, -mp_Kw);

        // Iq Differential
        md.assign(refAlg + 1, refDiff, Vd);
    }
    // Id and Iq
    /*
rv[0] = Vd + Rs*gm[0] + (Xdp - Xl)*gm[1];
rv[1] = Vq + Rs*gm[1] - (Xdp - Xl)*gm[0];
*/
    // Id Differential

    // md.assignCheckCol (refAlg + 1, inputLocs[genModelEftInLocation], -1.0);
    // delta
    md.assign(refDiff, refDiff, -sD.cj);
    md.assign(refDiff, refDiff + 1, systemBaseFrequency);
    // omega

    double Eft = inputs[genModelEftInLocation];
    // double Pe = (Eft+mp_Kp*(gmd[1] - 1.0))*gm[1];

    double kVal = -0.5 / H;

    // md.assign (refDiff + 1, refAlg, -0.5  * (Xd - Xdp) * gm[1] / H);
    if (hasAlgebraic(sMode)) {
        md.assign(refDiff + 1, refAlg + 1, kVal * (Eft + mp_Kw * (Loc.diffStateLoc[1] - 1.0)));
    }
    md.assign(refDiff + 1, refDiff + 1, kVal * (D + mp_Kw * gm[1]) - sD.cj);

    md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], -kVal);  // governor: Pm
    md.assignCheckCol(refDiff + 1, inputLocs[genModelEftInLocation],
                      kVal * gm[1]);  // exciter: Ef
}

void GenModelClassical::outputPartialDerivatives(const IOdata& inputs,
                                                 const stateData& sD,
                                                 matrixData<double>& md,
                                                 const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    auto refAlg = Loc.algOffset;
    auto refDiff = Loc.diffOffset;

    const double* gm = Loc.algStateLoc;

    updateLocalCache(inputs, sD, sMode);
    if (hasAlgebraic(sMode)) {
        // output P
        md.assign(PoutLocation, refAlg, -Vd);
        md.assign(PoutLocation, refAlg + 1, -Vq);

        // output Q
        md.assign(QoutLocation, refAlg, Vq);
        md.assign(QoutLocation, refAlg + 1, -Vd);
    }

    if (hasDifferential(sMode)) {
        md.assign(PoutLocation, refDiff, -gm[1] * Vd + gm[0] * Vq);
        md.assign(QoutLocation, refDiff, gm[1] * Vq + gm[0] * Vd);
    }
}

count_t GenModelClassical::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 3;
}

static const stringVec genModelClassicStateNames{"id", "iq", "delta", "freq"};

stringVec GenModelClassical::localStateNames() const
{
    return genModelClassicStateNames;
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
                throw(unrecognizedParameter(param));
        }
        return;
    }

    if (param == "kw") {
        mp_Kw = val;
    } else {
        GenModel::set(param, val, unitType);
    }
}

}  // namespace griddyn::genmodels
