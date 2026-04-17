/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel4.h"

#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::genmodels {
GenModel4::GenModel4(const std::string& objName): GenModel3(objName) {}
coreObject* GenModel4::clone(coreObject* obj) const
{
    auto* gd = cloneBase<GenModel4, GenModel3>(this, obj);
    if (gd == nullptr) {
        return obj;
    }
    gd->Xqp = Xqp;
    gd->Tqop = Tqop;
    gd->S10 = S10;
    gd->S12 = S12;
    return gd;
}

void GenModel4::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 4;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 25;
}

// initial conditions
void GenModel4::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);
    double* gm = m_state.data();

    // Edp and Eqp
    gm[4] = Vd + Rs * gm[0] + (Xqp)*gm[1];
    gm[5] = Vq + Rs * gm[1] - (Xdp)*gm[0];

    // record Pm = Pset
    // this should be close to P from above
    double Pmt = gm[4] * gm[0] + gm[5] * gm[1] + (Xdp - Xqp) * gm[0] * gm[1];

    // exciter - assign Ef
    double Eft = gm[5] - (Xd - Xdp) * gm[0];
    // preset the inputs that should be initialized
    fieldSet[2] = Eft;
    fieldSet[3] = Pmt;
}

void GenModel4::residual(const IOdata& inputs,
                         const stateData& sD,
                         double resid[],
                         const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, resid, sMode, this);
    const double* gm = Loc.algStateLoc;
    const double* gmd = Loc.diffStateLoc;

    updateLocalCache(inputs, sD, sMode);

    // Id and Iq
    if (hasAlgebraic(sMode)) {
        double* rva = Loc.destLoc;
        rva[0] = Vd + Rs * gm[0] + (Xqp)*gm[1] - gmd[2];
        rva[1] = Vq + Rs * gm[1] - (Xdp)*gm[0] - gmd[3];
    }

    if (hasDifferential(sMode)) {
        double* rvd = Loc.destDiffLoc;
        // Get the exciter field
        double Eft = inputs[genModelEftInLocation];
        double Pmt = inputs[genModelPmechInLocation];
        const double* gmp = Loc.dstateLoc;
        // delta
        rvd[0] = systemBaseFrequency * (gmd[1] - 1.0) - gmp[0];
        // Edp and Eqp
        rvd[2] = (-gmd[2] - (Xq - Xqp) * gm[1]) / Tqop - gmp[2];
        rvd[3] = (-gmd[3] + (Xd - Xdp) * gm[0] + Eft) / Tdop - gmp[3];

        // omega
        double Pe = gmd[2] * gm[0] + gmd[3] * gm[1] + (Xdp - Xqp) * gm[0] * gm[1];
        rvd[1] = 0.5 * (Pmt - Pe - D * (gmd[1] - 1.0)) / H - gmp[1];
    }
    // if (parent->parent->name == "BUS_31")
    //   {
    //   printf("[%d]t=%f gmp[1]=%f Vq=%f, Vd=%f,Pdiff=%f A=%f, B=%f, C=%f Id=%f,
    //   Iq=%f, Eft=%f\n", getID(), time,
    //   gmp[1], Vq,Vd, Pmt - Pe, gmd[2] * gm[0], gmd[3] * gm[1], (Xdp - Xqp) *
    //   gm[0] * gm[1],gm[0],gm[1],Eft);
    //   }
}

void GenModel4::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    stateData sD(time, m_state.data());
    derivative(inputs, sD, m_dstate_dt.data(), cLocalSolverMode);
    double dt = time - prevTime;
    m_state[2] += dt * m_dstate_dt[2];
    m_state[3] += dt * m_dstate_dt[3];
    m_state[4] += dt * m_dstate_dt[4];
    m_state[5] += dt * m_dstate_dt[5];
    prevTime = time;
    algebraicUpdate(inputs, sD, m_state.data(), cLocalSolverMode, 1.0);
}

void GenModel4::algebraicUpdate(const IOdata& inputs,
                                const stateData& sD,
                                double update[],
                                const solverMode& sMode,
                                double /*alpha*/)
{
    auto Loc = offsets.getLocations(sD, update, sMode, this);
    updateLocalCache(inputs, sD, sMode);
    gmlc::utilities::solve2x2(Rs,
                              (Xqp),
                              -(Xdp),
                              Rs,
                              Loc.diffStateLoc[2] - Vd,
                              Loc.diffStateLoc[3] - Vq,
                              Loc.destLoc[0],
                              Loc.destLoc[1]);
    m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
}

void GenModel4::derivative(const IOdata& inputs,
                           const stateData& sD,
                           double deriv[],
                           const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    const double* ast = Loc.algStateLoc;
    const double* dst = Loc.diffStateLoc;
    double* dv = Loc.destDiffLoc;
    // Get the exciter field
    double Eft = inputs[genModelEftInLocation];
    double Pmt = inputs[genModelPmechInLocation];

    // Id and Iq

    // delta
    dv[0] = systemBaseFrequency * (dst[1] - 1.0);
    // Edp and Eqp
    dv[2] = (-dst[2] - (Xq - Xqp) * ast[1]) / Tqop;
    dv[3] = (-dst[3] + (Xd - Xdp) * ast[0] + Eft) / Tdop;

    // omega
    double Pe = dst[2] * ast[0] + dst[3] * ast[1] + (Xdp - Xqp) * ast[0] * ast[1];
    dv[1] = 0.5 * (Pmt - Pe - D * (dst[1] - 1.0)) / H;
}

void GenModel4::jacobianElements(const IOdata& inputs,
                                 const stateData& sD,
                                 matrixData<double>& md,
                                 const IOlocs& inputLocs,
                                 const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);

    auto refAlg = Loc.algOffset;
    auto refDiff = Loc.diffOffset;
    const double* gm = Loc.algStateLoc;
    double V = inputs[voltageInLocation];
    auto VLoc = inputLocs[voltageInLocation];
    auto TLoc = inputLocs[angleInLocation];

    updateLocalCache(inputs, sD, sMode);

    // P
    if (hasAlgebraic(sMode)) {
        if (TLoc != kNullLocation) {
            md.assign(refAlg, TLoc, Vq);
            md.assign(refAlg + 1, TLoc, -Vd);
        }

        // Q
        if (VLoc != kNullLocation) {
            if (V == 0) {
                LOG_WARNING("voltage=0");
            }
            md.assign(refAlg, VLoc, Vd / V);
            md.assign(refAlg + 1, VLoc, Vq / V);
        }

        md.assign(refAlg, refAlg, Rs);
        md.assign(refAlg, refAlg + 1, (Xqp));

        md.assign(refAlg + 1, refAlg, -(Xdp));
        md.assign(refAlg + 1, refAlg + 1, Rs);
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        // Id Differential

        md.assign(refAlg, refDiff, -Vq);
        md.assign(refAlg, refDiff + 2, -1.0);

        // Iq Differential

        md.assign(refAlg + 1, refDiff, Vd);
        md.assign(refAlg + 1, refDiff + 3, -1.0);
    }

    if (hasDifferential(sMode)) {
        // delta
        md.assign(refDiff, refDiff, -sD.cj);
        md.assign(refDiff, refDiff + 1, systemBaseFrequency);

        // omega
        double kVal = -0.5 / H;
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 1, refAlg, -0.5 * (gm[4] + (Xdp - Xqp) * gm[1]) / H);
            md.assign(refDiff + 1, refAlg + 1, -0.5 * (gm[5] + (Xdp - Xqp) * gm[0]) / H);

            md.assign(refDiff + 2, refAlg + 1, -(Xq - Xqp) / Tqop);  // Edp

            md.assign(refDiff + 3, refAlg, (Xd - Xdp) / Tdop);  // Eqp
        }

        md.assign(refDiff + 1, refDiff + 1, -0.5 * D / H - sD.cj);
        md.assign(refDiff + 1, refDiff + 2, -0.5 * gm[0] / H);
        md.assign(refDiff + 1, refDiff + 3, -0.5 * gm[1] / H);

        md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation],
                          -kVal);  // governor: Pm

        md.assign(refDiff + 2, refDiff + 2, -1.0 / Tqop - sD.cj);

        // Eqp

        md.assign(refDiff + 3, refDiff + 3, -1.0 / Tdop - sD.cj);

        md.assignCheckCol(refDiff + 3,
                          inputLocs[genModelEftInLocation],
                          1.0 / Tdop);  // exciter: Ef
    }
}

static const stringVec genModel4Names{"id", "iq", "delta", "freq", "edp", "eqp"};

stringVec GenModel4::localStateNames() const
{
    return genModel4Names;
}
// set parameters
void GenModel4::set(const std::string& param, const std::string& val)
{
    if (param == "saturation_type") {
        sat.setType(val);
    } else {
        GenModel3::set(param, val);
    }
}

void GenModel4::set(const std::string& param, double val, units::unit unitType)
{
    if (param == "xd") {
        Xd = val;
    } else if (param == "xqp") {
        Xqp = val;
    } else if ((param == "tqop") || (param == "tq0p")) {
        Tqop = val;
    } else if ((param == "top") || (param == "t0p")) {
        Tqop = val;
        Tdop = val;
    } else if ((param == "s1") || (param == "s10")) {
        S10 = val;
        sat.setParam(S10, S12);
    } else if (param == "s12") {
        S12 = val;
        sat.setParam(S10, S12);
    } else {
        GenModel3::set(param, val, unitType);
    }
}

}  // namespace griddyn::genmodels
