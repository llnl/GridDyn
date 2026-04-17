/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel5.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <complex>
#include <string>

namespace griddyn {
namespace genmodels {
    GenModel5::GenModel5(const std::string& objName): GenModel4(objName) {}
    coreObject* GenModel5::clone(coreObject* obj) const
    {
        auto* gd = cloneBase<GenModel5, GenModel4>(this, obj);
        if (gd == nullptr) {
            return obj;
        }
        gd->Tqopp = Tqopp;
        gd->Taa = Taa;
        gd->Tdopp = Tdopp;
        gd->Xdpp = Xdpp;
        gd->Xqpp = Xqpp;
        return gd;
    }

    void GenModel5::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
    {
        offsets.local().local.diffSize = 5;
        offsets.local().local.algSize = 2;
        offsets.local().local.jacSize = 40;
    }
    // initial conditions
    void GenModel5::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
    {
        double* gm = m_state.data();
        computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);

        // Edp and Eqp  and Edpp

        gm[5] = Vq + Rs * gm[1] - (Xdp)*gm[0];
        gm[6] = Vd + Rs * gm[0] + (Xqp)*gm[1];

        double xrat = Tqopp * (Xdp + Xl) / (Tqop * (Xqp + Xl));
        gm[4] = gm[6] + (Xqp - Xdp + xrat * (Xq - Xqp)) * gm[1];

        // record Pm = Pset
        // this should be close to P from above
        double Pmt = gm[6] * gm[0] + gm[5] * gm[1] + (Xdp - Xqp) * gm[0] * gm[1];
        // exciter - assign Ef
        double Eft = gm[5] - (Xd - Xdp) * gm[0];
        // preset the inputs that should be initialized
        fieldSet[2] = Eft;
        fieldSet[3] = Pmt;
    }

    void GenModel5::algebraicUpdate(const IOdata& inputs,
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
                                  Loc.diffStateLoc[4] - Vd,
                                  Loc.diffStateLoc[3] - Vq,
                                  Loc.destLoc[0],
                                  Loc.destLoc[1]);
        m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
    }

    void GenModel5::residual(const IOdata& inputs,
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

        // Id and Iq
        if (hasAlgebraic(sMode)) {
            rva[0] = Vd + Rs * gm[0] + (Xqp)*gm[1] - gmd[4];
            rva[1] = Vq + Rs * gm[1] - (Xdp)*gm[0] - gmd[3];
        }

        if (hasDifferential(sMode)) {
            derivative(inputs, sD, resid, sMode);
            // Get the exciter field

            // delta
            rvd[0] -= gmp[0];
            rvd[1] -= gmp[1];
            rvd[2] -= gmp[2];
            rvd[3] -= gmp[3];
            rvd[4] -= gmp[4];
        }
    }

    void GenModel5::derivative(const IOdata& inputs,
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

        double xrat = Tqopp * (Xdp + Xl) / (Tqop * (Xqp + Xl));
        // Edp and Eqp
        dv[2] = (-dst[2] - (Xq - Xqp - xrat * (Xq - Xqp)) * ast[1]) / Tqop;
        dv[3] = (-dst[3] + (Xd - Xdp) * ast[0] + Eft) / Tdop;
        // Edpp
        dv[4] = (-dst[4] + dst[2] - (Xqp - Xdp + xrat * (Xq - Xqp)) * ast[1]) / Tqopp;
        // omega

        double Pe = dst[4] * ast[0] + dst[3] * ast[1] + (Xdp - Xqp) * ast[0] * ast[1];
        dv[1] = 0.5 * (Pmt - Pe - D * (dst[1] - 1.0)) / H;
    }

    void GenModel5::jacobianElements(const IOdata& inputs,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
    {
        // md.assign (arrayIndex, RowIndex, ColIndex, value) const
        auto Loc = offsets.getLocations(sD, nullptr, sMode, this);

        auto refAlg = Loc.algOffset;
        auto refDiff = Loc.diffOffset;
        const double* gm = Loc.algStateLoc;

        auto VLoc = inputLocs[voltageInLocation];
        auto TLoc = inputLocs[angleInLocation];

        updateLocalCache(inputs, sD, sMode);

        bool hasAlg = hasAlgebraic(sMode);
        // P
        if (hasAlg) {
            if (TLoc != kNullLocation) {
                md.assign(refAlg, TLoc, Vq);
                md.assign(refAlg + 1, TLoc, -Vd);
            }

            // Q
            if (VLoc != kNullLocation) {
                double V = inputs[voltageInLocation];
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

            // Id Additional

            md.assign(refAlg, refDiff, -Vq);
            md.assign(refAlg, refDiff + 4, -1);

            // Iq Additional
            md.assign(refAlg + 1, refDiff, Vd);
            md.assign(refAlg + 1, refDiff + 3, -1);
        }

        if (hasDifferential(sMode)) {
            // delta
            md.assign(refDiff, refDiff, -sD.cj);
            md.assign(refDiff, refDiff + 1, systemBaseFrequency);

            // omega
            double kVal = -0.5 / H;
            if (hasAlg) {
                md.assign(refDiff + 1, refAlg, -0.5 * (gm[6] + (Xdp - Xqp) * gm[1]) / H);
                md.assign(refDiff + 1, refAlg + 1, -0.5 * (gm[5] + (Xdp - Xqp) * gm[0]) / H);
            }

            md.assign(refDiff + 1, refDiff + 1, -0.5 * D / H - sD.cj);
            md.assign(refDiff + 1, refDiff + 4, -0.5 * gm[0] / H);
            md.assign(refDiff + 1, refDiff + 3, -0.5 * gm[1] / H);

            md.assignCheckCol(refDiff + 1,
                              inputLocs[genModelPmechInLocation],
                              -kVal);  // governor: Pm

            double xrat = Tqopp * (Xdp + Xl) / (Tqop * (Xqp + Xl));
            // Edp
            if (hasAlg) {
                md.assign(refDiff + 2, refAlg + 1, -(Xq - Xqp - xrat * (Xq - Xqp)) / Tqop);
            }
            md.assign(refDiff + 2, refDiff + 2, -1 / Tqop - sD.cj);

            // Eqp
            if (hasAlg) {
                md.assign(refDiff + 3, refAlg, (Xd - Xdp) / Tdop);
            }
            md.assign(refDiff + 3, refDiff + 3, -1 / Tdop - sD.cj);

            md.assignCheckCol(refDiff + 3,
                              inputLocs[genModelEftInLocation],
                              1 / Tdop);  // exciter: Ef

            // Edpp
            if (hasAlg) {
                md.assign(refDiff + 4, refAlg + 1, -(Xqp - Xdp + xrat * (Xq - Xqp)) / Tqopp);
            }
            md.assign(refDiff + 4, refDiff + 2, 1 / Tqopp);
            md.assign(refDiff + 4, refDiff + 4, -1 / Tqopp - sD.cj);
        }
    }

    static const stringVec genModel5Names{"id", "iq", "delta", "freq", "edp", "eqp", "edpp"};

    stringVec GenModel5::localStateNames() const
    {
        return genModel5Names;
    }
    // set parameters
    void GenModel5::set(const std::string& param, const std::string& val)
    {
        return GenModel4::set(param, val);
    }
    void GenModel5::set(const std::string& param, double val, units::unit unitType)
    {
        if ((param == "tqopp") || (param == "tq0pp")) {
            Tqopp = val;
        } else if (param == "taa") {
            Taa = val;
        } else if ((param == "tdopp") || (param == "td0pp")) {
            Tdopp = val;
        } else if (param == "xdpp") {
            Xdpp = val;
        } else if (param == "xqpp") {
            Xqpp = val;
        } else if (param == "xpp") {
            Xdpp = val;
            Xdpp = val;
        } else {
            GenModel4::set(param, val, unitType);
        }
    }

}  // namespace genmodels
}  // namespace griddyn
