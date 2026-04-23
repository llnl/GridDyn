/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel3.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::genmodels {
GenModel3::GenModel3(const std::string& objName): GenModelClassical(objName)
{
    // default values

    Xd = 1.05;
}

coreObject* GenModel3::clone(coreObject* obj) const
{
    auto* gd = cloneBase<GenModel3, GenModelClassical>(this, obj);
    if (gd == nullptr) {
        return obj;
    }
    gd->Xl = Xl;
    gd->Xdp = Xdp;
    gd->Xq = Xq;
    gd->Tdop = Tdop;

    return gd;
}

void GenModel3::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 3;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 21;
}
// initial conditions
void GenModel3::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);
    double* gm = m_state.data();
    // Edp and Eqp
    E = Vd + Rs * gm[0] + (Xq)*gm[1];
    gm[4] = Vq + Rs * gm[1] - (Xdp)*gm[0];

    // record Pm = Pset
    // this should be close to P from above
    double Pmt = E * gm[0] + gm[4] * gm[1] + (Xdp - Xq) * gm[0] * gm[1];
    // Pmt = P;

    // exciter - assign Ef
    double Eft = gm[4] - (Xd - Xdp) * gm[0];
    // preset the inputs that should be initialized
    fieldSet[2] = Eft;
    fieldSet[3] = Pmt;
}

void GenModel3::derivative(const IOdata& inputs,
                           const stateData& sD,
                           double deriv[],
                           const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    const double* gm = Loc.algStateLoc;
    const double* gmd = Loc.diffStateLoc;
    double* dv = Loc.destDiffLoc;
    // Get the exciter field
    double Eft = inputs[genModelEftInLocation];
    double Pmt = inputs[genModelPmechInLocation];

    // Id and Iq

    // delta
    dv[0] = systemBaseFrequency * (gmd[1] - 1.0);
    // Eqp
    dv[2] = (-gmd[2] + Eft + (Xd - Xdp) * gm[0]) / Tdop;

    // omega
    double Pe = gmd[2] * gm[1] + E * gm[0] + (Xdp - Xq) * gm[0] * gm[1];

    dv[1] = 0.5 * (Pmt - Pe - D * (gmd[1] - 1.0)) / H;
}

void GenModel3::algebraicUpdate(const IOdata& inputs,
                                const stateData& sD,
                                double update[],
                                const solverMode& sMode,
                                double /*alpha*/)
{
    auto Loc = offsets.getLocations(sD, update, sMode, this);
    updateLocalCache(inputs, sD, sMode);
    gmlc::utilities::solve2x2(
        Rs, (Xq), -(Xdp), Rs, -Vd + E, Loc.diffStateLoc[2] - Vq, Loc.destLoc[0], Loc.destLoc[1]);
    m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
}

void GenModel3::residual(const IOdata& inputs,
                         const stateData& sD,
                         double resid[],
                         const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, resid, sMode, this);

    const double* gm = Loc.algStateLoc;
    const double* gmd = Loc.diffStateLoc;
    const double* gmp = Loc.dstateLoc;

    double* rva = Loc.destLoc;
    double* rvd = Loc.destDiffLoc;
    updateLocalCache(inputs, sD, sMode);

    if (hasAlgebraic(sMode)) {
        rva[0] = Vd + Rs * gm[0] + (Xq)*gm[1] - E;
        rva[1] = Vq + Rs * gm[1] - (Xdp)*gm[0] - gmd[2];
    }

    if (hasDifferential(sMode)) {
        // Get the exciter field
        double Eft = inputs[genModelEftInLocation];
        double Pmt = inputs[genModelPmechInLocation];

        // Id and Iq

        // delta
        rvd[0] = systemBaseFrequency * (gmd[1] - 1.0) - gmp[0];
        // Eqp
        rvd[2] = (-gmd[2] + Eft + (Xd - Xdp) * gm[0]) / Tdop - gmp[2];

        // omega
        double Pe = gmd[2] * gm[1] + E * gm[0] + (Xdp - Xq) * gm[0] * gm[1];

        rvd[1] = 0.5 * (Pmt - Pe - D * (gmd[1] - 1.0)) / H - gmp[1];
    }
    //
}

void GenModel3::jacobianElements(const IOdata& inputs,
                                 const stateData& sD,
                                 matrixData<double>& md,
                                 const IOlocs& inputLocs,
                                 const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);

    double V = inputs[voltageInLocation];
    const double* gm = Loc.algStateLoc;
    const double* gmd = Loc.diffStateLoc;
    //  const double *gmp = Loc.dstateLoc;

    updateLocalCache(inputs, sD, sMode);

    auto refAlg = Loc.algOffset;
    auto refDiff = Loc.diffOffset;

    auto VLoc = inputLocs[voltageInLocation];
    auto TLoc = inputLocs[angleInLocation];

    if (hasAlgebraic(sMode)) {
        // P
        if (TLoc != kNullLocation) {
            md.assign(refAlg, TLoc, Vq);
            md.assign(refAlg + 1, TLoc, -Vd);
        }

        // Q
        if (VLoc != kNullLocation) {
            md.assign(refAlg, VLoc, Vd / V);
            md.assign(refAlg + 1, VLoc, Vq / V);
        }

        md.assign(refAlg, refAlg, Rs);
        md.assign(refAlg, refAlg + 1, (Xq - Xl));

        md.assign(refAlg + 1, refAlg, -(Xdp - Xl));
        md.assign(refAlg + 1, refAlg + 1, Rs);
        if (hasDifferential(sMode)) {
            // Id Differential

            md.assign(refAlg, refDiff, -Vq);

            md.assign(refAlg + 1, refDiff, Vd);
            md.assign(refAlg + 1, refDiff + 2, -1.0);
        }
    }

    if (hasDifferential(sMode)) {
        // delta
        md.assign(refDiff, refDiff, -sD.cj);
        md.assign(refDiff, refDiff + 1, systemBaseFrequency);

        // omega
        double kVal = -0.5 / H;
        if (hasAlgebraic(sMode)) {
            // Pe = gm[4] * gm[1] + E*gm[0] + (Xdp - Xq)*gm[0] * gm[1];
            md.assign(refDiff + 1, refAlg, -0.5 * (E + (Xdp - Xq) * gm[1]) / H);
            md.assign(refDiff + 1, refAlg + 1, -0.5 * (gmd[2] + (Xdp - Xq) * gm[0]) / H);

            md.assign(refDiff + 2, refAlg, (Xd - Xdp) / Tdop);
        }

        md.assign(refDiff + 1, refDiff + 1, -0.5 * D / H - sD.cj);
        md.assign(refDiff + 1, refDiff + 2, -0.5 * gm[1] / H);

        md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation],
                          -kVal);  // governor: Pm

        md.assign(refDiff + 2, refDiff + 2, -1.0 / Tdop - sD.cj);

        md.assignCheckCol(refDiff + 2,
                          inputLocs[genModelEftInLocation],
                          1.0 / Tdop);  // exciter: Ef
    }
}

static const stringVec genModel3Names{"id", "iq", "delta", "freq", "eqp"};

stringVec GenModel3::localStateNames() const
{
    return genModel3Names;
}
// set parameters
void GenModel3::set(std::string_view param, std::string_view val)
{
    return GenModelClassical::set(param, val);
}

void GenModel3::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "x") {
        Xq = val;
        Xd = val;
    } else if (param == "xq") {
        Xq = val;
    } else if (param == "xl") {
        Xl = val;
    } else if ((param == "xp") || (param == "xdp")) {
        Xdp = val;
    } else if ((param == "tdop") || (param == "td0p")) {
        Tdop = val;
    } else if ((param == "top") || (param == "t0p")) {
        Tdop = val;
    } else {
        GenModelClassical::set(param, val, unitType);
    }
}
}  // namespace griddyn::genmodels
