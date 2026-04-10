/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel6type2.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>

namespace griddyn {
namespace genmodels {
    GenModel6type2::GenModel6type2(const std::string& objName): GenModel5type2(objName) {}
    coreObject* GenModel6type2::clone(coreObject* obj) const
    {
        auto* gd = cloneBase<GenModel6type2, GenModel5type2>(this, obj);
        if (gd == nullptr) {
            return nullptr;
        }
        return gd;
    }

    void GenModel6type2::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
    {
        offsets.local().local.diffSize = 6;
        offsets.local().local.algSize = 2;
        offsets.local().local.jacSize = 40;
    }

    // initial conditions
    void GenModel6type2::dynObjectInitializeB(const IOdata& inputs,
                                              const IOdata& desiredOutput,
                                              IOdata& fieldSet)
    {
        computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);
        double* gm = m_state.data();

        double qrat = Tqopp * (Xqpp + Xl) / (Tqop * (Xqp + Xl));
        double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xdp + Xl));
        gm[4] = -(Xq - Xqp - qrat * (Xq - Xqp)) * gm[1];

        // Edp and Eqp  and Edpp

        gm[7] = Vq + Rs * gm[1] - (Xdpp)*gm[0];
        gm[6] = Vd + Rs * gm[0] + (Xqpp)*gm[1];

        // record Pm = Pset
        // this should be close to P from above
        double Pmt = gm[6] * gm[0] + gm[7] * gm[1] + (Xdpp - Xqpp) * gm[0] * gm[1];

        // exciter - assign Ef
        double Eft = gm[7] - (Xd - Xdpp) * gm[0];

        gm[5] = gm[7] - (Xdp - Xdpp + drat * (Xd - Xdp)) * gm[0] + Taa / Tdop * Eft;

        // double g4 = gm[6] + (Xqp - Xqpp + qrat * (Xq - Xqp)) * gm[1];
        // preset the inputs that should be initialized
        fieldSet[2] = Eft;
        fieldSet[3] = Pmt;
    }

    void GenModel6type2::algebraicUpdate(const IOdata& inputs,
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
                                  Loc.diffStateLoc[4] - Vd,
                                  Loc.diffStateLoc[5] - Vq,
                                  Loc.destLoc[0],
                                  Loc.destLoc[1]);
        m_output = -(Loc.destLoc[1] * Vq + Loc.destLoc[0] * Vd);
    }

    void GenModel6type2::derivative(const IOdata& inputs,
                                    const stateData& sD,
                                    double deriv[],
                                    const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, deriv, sMode, this);
        const double* gm = Loc.algStateLoc;
        const double* gmd = Loc.diffStateLoc;
        // const double *gmp = Loc.dstateLoc;

        //  double *rva = Loc.destLoc;
        double* rvd = Loc.destDiffLoc;

        // Get the exciter field
        double Eft = inputs[genModelEftInLocation];
        double Pmt = inputs[genModelPmechInLocation];

        double qrat = Tqopp * (Xqpp + Xl) / (Tqop * (Xqp + Xl));
        double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xdp + Xl));

        // delta
        rvd[0] = systemBaseFrequency * (gmd[1] - 1.0);
        // Edp and Eqp
        rvd[2] = (-gmd[2] - (Xq - Xqp - qrat * (Xq - Xqp)) * gm[1]) / Tqop;
        rvd[3] =
            (-gmd[3] + (Xd - Xdp - drat * (Xd - Xdp)) * gm[0] + (1.0 - Taa / Tdop) * Eft) / Tdop;
        // Edpp
        rvd[4] = (-gmd[4] + gmd[2] - (Xqp - Xqpp + qrat * (Xq - Xqp)) * gm[1]) / Tqopp;
        rvd[5] = (-gmd[5] + gmd[3] + (Xdp - Xdpp + drat * (Xd - Xdp)) * gm[0] + Taa / Tdop * Eft) /
            Tdopp;
        // omega
        double Pe = gmd[4] * gm[0] + gmd[5] * gm[1] + (Xdpp - Xqpp) * gm[0] * gm[1];
        //  double Pe2 = (Vq + Rs * gm[1]) * gm[1] + (Vd + Rs * gm[0]) * gm[0];
        rvd[1] = 0.5 * (Pmt - Pe - D * (gmd[1] - 1.0)) / H;
    }

    void GenModel6type2::residual(const IOdata& inputs,
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
            // Id and Iq
            rva[0] = Vd + Rs * gm[0] + (Xqpp)*gm[1] - gmd[4];
            rva[1] = Vq + Rs * gm[1] - (Xdpp)*gm[0] - gmd[5];
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
            rvd[5] -= gmp[5];
        }
    }

    void GenModel6type2::jacobianElements(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, sMode, this);

        double V = inputs[voltageInLocation];
        const double* gm = Loc.algStateLoc;
        const double* gmd = Loc.diffStateLoc;
        // const double *gmp = Loc.dstateLoc;

        updateLocalCache(inputs, sD, sMode);

        // md.assign(arrayIndex, RowIndex, ColIndex, value)
        auto refAlg = Loc.algOffset;
        auto refDiff = Loc.diffOffset;

        auto VLoc = inputLocs[voltageInLocation];
        auto TLoc = inputLocs[angleInLocation];

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
            md.assign(refAlg, refDiff + 4, -1.0);

            // Iq Differential

            md.assign(refAlg + 1, refDiff, Vd);
            md.assign(refAlg + 1, refDiff + 5, -1.0);
        }

        // delta
        md.assign(refDiff, refDiff, -sD.cj);
        md.assign(refDiff, refDiff + 1, systemBaseFrequency);

        // omega
        double kVal = -0.5 / H;
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 1, refAlg, -0.5 * (gmd[4] + (Xdpp - Xqpp) * gm[1]) / H);
            md.assign(refDiff + 1, refAlg + 1, -0.5 * (gmd[5] + (Xdpp - Xqpp) * gm[0]) / H);
        }
        md.assign(refDiff + 1, refDiff + 1, -0.5 * D / H - sD.cj);
        md.assign(refDiff + 1, refDiff + 4, -0.5 * gm[0] / H);
        md.assign(refDiff + 1, refDiff + 5, -0.5 * gm[1] / H);

        md.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], -kVal);  // governor: Pm

        double qrat = Tqopp * (Xqpp + Xl) / (Tqop * (Xqp + Xl));
        double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xdp + Xl));

        // Edp
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 2, refAlg + 1, -(Xq - Xqp - qrat * (Xq - Xqp)) / Tqop);
        }
        md.assign(refDiff + 2, refDiff + 2, -1.0 / Tqop - sD.cj);

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
            md.assign(refDiff + 5, inputLocs[genModelEftInLocation], Taa / Tdop / Tdopp);
        }
        // Edpp
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 4, refAlg + 1, -(Xqp - Xqpp + qrat * (Xq - Xqp)) / Tqopp);
        }
        md.assign(refDiff + 4, refDiff + 2, 1.0 / Tqopp);
        md.assign(refDiff + 4, refDiff + 4, -1.0 / Tqopp - sD.cj);

        // Eqpp
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 5, refAlg, (Xdp - Xdpp + drat * (Xd - Xdp)) / Tdopp);
        }
        md.assign(refDiff + 5, refDiff + 3, 1.0 / Tdopp);
        md.assign(refDiff + 5, refDiff + 5, -1.0 / Tdopp - sD.cj);
    }

    static const stringVec
        genModel6type2Names{"id", "iq", "delta", "freq", "edp", "eqp", "edpp", "eqpp"};

    stringVec GenModel6type2::localStateNames() const
    {
        return genModel6type2Names;
    }
}  // namespace genmodels
}  // namespace griddyn
