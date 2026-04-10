/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel5type2.h"

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
    GenModel5type2::GenModel5type2(const std::string& objName): GenModel5(objName) {}
    coreObject* GenModel5type2::clone(coreObject* obj) const
    {
        auto* gd = cloneBase<GenModel5type2, GenModel5>(this, obj);
        if (gd == nullptr) {
            return obj;
        }
        return gd;
    }

    void GenModel5type2::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
    {
        offsets.local().local.diffSize = 5;
        offsets.local().local.algSize = 2;
        offsets.local().local.jacSize = 40;
    }

    // initial conditions
    void GenModel5type2::dynObjectInitializeB(const IOdata& inputs,
                                              const IOdata& desiredOutput,
                                              IOdata& fieldSet)
    {
        double* gm = m_state.data();
        computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);

        // Edp and Eqp  and Edpp

        gm[6] = Vq + Rs * gm[1] - (Xdpp)*gm[0];
        gm[4] = Vd + Rs * gm[0] + (Xqpp)*gm[1];

        // exciter - assign Ef
        double Eft = gm[6] - (Xd - Xdpp) * gm[0];

        double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xqp + Xl));
        gm[5] = gm[6] - (Xdp - Xdpp + drat * (Xd - Xdp)) * gm[0] + Taa / Tdop * Eft;
        // record Pm = Pset
        // this should be close to P from above
        double Pmt = gm[4] * gm[0] + gm[6] * gm[1] + (Xdpp - Xqpp) * gm[0] * gm[1];
        // Pmt = P;
        // preset the inputs that should be initialized
        fieldSet[2] = Eft;
        fieldSet[3] = Pmt;
    }

    void GenModel5type2::derivative(const IOdata& inputs,
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

        double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xqp + Xl));
        // Id and Iq

        // delta
        dv[0] = systemBaseFrequency * (gmd[1] - 1.0);
        // Edpp and Eqp
        dv[2] = (-gmd[2] - (Xq - Xqpp) * gm[1]) / Tqopp;
        dv[3] =
            (-gmd[3] + (Xd - Xdp - drat * (Xd - Xdp)) * gm[0] + (1.0 - Taa / Tdop) * Eft) / Tdop;
        // Eqpp
        dv[4] = (-gmd[4] + gmd[3] + (Xdp - Xdpp + drat * (Xd - Xdp)) * gm[0] + Taa / Tdop * Eft) /
            Tdopp;
        // omega
        double Pe = gmd[2] * gm[0] + gmd[4] * gm[1] + (Xdpp - Xqpp) * gm[0] * gm[1];

        dv[1] = 0.5 * (Pmt - Pe - D * (gmd[1] - 1.0)) / H;
    }

    void GenModel5type2::algebraicUpdate(const IOdata& inputs,
                                         const stateData& sD,
                                         double update[],
                                         const solverMode& sMode,
                                         double /*alpha*/)
    {
        auto Loc = offsets.getLocations(sD, update, sMode, this);
        updateLocalCache(inputs, sD, sMode);
        gmlc::utilities::solve2x2(Rs,
                                  (Xqpp),
                                  -(Xdpp),
                                  Rs,
                                  Loc.diffStateLoc[2] - Vd,
                                  Loc.diffStateLoc[4] - Vq,
                                  Loc.destLoc[0],
                                  Loc.destLoc[1]);
        m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
    }

    void GenModel5type2::residual(const IOdata& inputs,
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
            rva[0] = Vd + Rs * gm[0] + (Xqpp)*gm[1] - gmd[2];
            rva[1] = Vq + Rs * gm[1] - (Xdpp)*gm[0] - gmd[4];
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

    void GenModel5type2::jacobianElements(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode)
    {
        // md.assign (arrayIndex, RowIndex, ColIndex, value) const
        auto Loc = offsets.getLocations(sD, sMode, this);

        auto refAlg = Loc.algOffset;
        auto refDiff = Loc.diffOffset;
        const double* gm = Loc.algStateLoc;
        const double* gmd = Loc.diffStateLoc;
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
                md.assign(refAlg, VLoc, Vd / V);
                md.assign(refAlg + 1, VLoc, Vq / V);
            }

            md.assign(refAlg, refAlg, Rs);
            md.assign(refAlg, refAlg + 1, (Xqpp));

            md.assign(refAlg + 1, refAlg, -(Xdpp));
            md.assign(refAlg + 1, refAlg + 1, Rs);

            if (isAlgebraicOnly(sMode)) {
                return;
            }

            // Id Differential

            md.assign(refAlg, refDiff, -Vq);
            md.assign(refAlg, refDiff + 2, -1.0);

            // Iq differential

            md.assign(refAlg + 1, refDiff, Vd);
            md.assign(refAlg + 1, refDiff + 4, -1.0);
        }
        // delta
        md.assign(refDiff, refDiff, -sD.cj);
        md.assign(refDiff, refDiff + 1, systemBaseFrequency);

        // omega
        double kVal = -0.5 / H;
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 1, refAlg, -0.5 * (gmd[2] + (Xdpp - Xqpp) * gm[1]) / H);
            md.assign(refDiff + 1, refAlg + 1, -0.5 * (gmd[4] + (Xdpp - Xqpp) * gm[0]) / H);
        }
        md.assign(refDiff + 1, refDiff + 1, -0.5 * D / H - sD.cj);
        md.assign(refDiff + 1, refDiff + 2, -0.5 * gm[0] / H);
        md.assign(refDiff + 1, refDiff + 4, -0.5 * gm[1] / H);

        md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], -kVal);  // governor: Pm

        double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xqp + Xl));

        // Edpp
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 2, refAlg + 1, -(Xq - Xqpp) / Tqopp);
        }
        md.assign(refDiff + 2, refDiff + 2, -1 / Tqopp - sD.cj);

        // Eqp
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 3, refAlg, (Xd - Xdp - drat * (Xd - Xdp)) / Tdop);
        }
        md.assign(refDiff + 3, refDiff + 3, -1.0 / Tdop - sD.cj);

        if (inputLocs[genModelEftInLocation] != kNullLocation)  // check if exciter exists
        {
            md.assign(refDiff + 3,
                      inputLocs[genModelEftInLocation],
                      (1.0 - Taa / Tdop) / Tdop);  // exciter: Ef
            md.assign(refDiff + 4, inputLocs[genModelEftInLocation], Taa / Tdop / Tdopp);
        }

        // Eqpp
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 4, refAlg, (Xdp - Xdpp + drat * (Xd - Xdp)) / Tdopp);
        }
        md.assign(refDiff + 4, refDiff + 3, 1.0 / Tdopp);
        md.assign(refDiff + 4, refDiff + 4, -1.0 / Tdopp - sD.cj);
    }

    static const stringVec genModel5type2Names{"id", "iq", "delta", "freq", "edpp", "eqp", "eqpp"};

    stringVec GenModel5type2::localStateNames() const
    {
        return genModel5type2Names;
    }
}  // namespace genmodels
}  // namespace griddyn
