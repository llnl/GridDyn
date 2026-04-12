/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel5type3.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn {
namespace genmodels {
    GenModel5type3::GenModel5type3(const std::string& objName): GenModel3(objName) {}
    coreObject* GenModel5type3::clone(coreObject* obj) const
    {
        auto* gd = cloneBase<GenModel5type3, GenModel3>(this, obj);
        if (gd == nullptr) {
            return obj;
        }
        return gd;
    }

    void GenModel5type3::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
    {
        offsets.local().local.diffSize = 5;
        offsets.local().local.algSize = 2;
        offsets.local().local.jacSize = 40;
    }
    // initial conditions
    void GenModel5type3::dynObjectInitializeB(const IOdata& inputs,
                                              const IOdata& desiredOutput,
                                              IOdata& fieldSet)
    {
        computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);
        double* gm = m_state.data();

        gm[5] = Vq + Rs * gm[1];
        gm[6] = -(Vd + Rs * gm[0]);

        // Edp and Eqp

        gm[4] = gm[5] - (Xd)*gm[0];

        // record Pm = Pset
        // this should be close to P from above
        double Pmt = gm[5] * gm[1] - gm[6] * gm[0];

        // exciter - assign Ef
        double Eft = gm[4];
        // preset the inputs that should be initialized
        fieldSet[2] = Eft;
        fieldSet[3] = Pmt;
    }

    void GenModel5type3::derivative(const IOdata& inputs,
                                    const stateData& sD,
                                    double deriv[],
                                    const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, deriv, sMode, this);
        const double* gm = Loc.algStateLoc;
        const double* gmd = Loc.diffStateLoc;
        const double* gmp = Loc.dstateLoc;
        double* dv = Loc.destDiffLoc;
        // Get the exciter field
        double Eft = inputs[genModelEftInLocation];
        double Pmt = inputs[genModelPmechInLocation];
        updateLocalCache(inputs, sD, sMode);
        // Id and Iq

        dv[0] = systemBaseFrequency * (gmd[1] - 1.0);
        // Eqp
        // rv[4] = (-gm[4] + Eft + (Xd - Xdp)*gm[0]) / Tdop - gmp[4];
        dv[2] = (Xd) / (Xdp) * ((Eft - gmd[2]) / Tdop - (Xd - Xdp) / (Xd)*gmp[3]);
        // omega
        double Pe2 = gmd[3] * gm[1] - gmd[4] * gm[0];
        dv[1] = 0.5 / H * (Pmt - Pe2 - D * (gmd[1] - 1.0));

        // psid and psiq
        dv[3] = systemBaseFrequency * (Vd + Rs * gm[0] + gmd[1] * gmd[4]);
        dv[4] = systemBaseFrequency * (Vq + Rs * gm[1] - gmd[1] * gmd[3]);
    }

    void GenModel5type3::residual(const IOdata& inputs,
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

        // Id and Iq
        if (hasAlgebraic(sMode)) {
            rva[0] = gmd[3] - gmd[2] - (Xd)*gm[0];
            rva[1] = gmd[4] - (Xq)*gm[1];
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

    void GenModel5type3::jacobianElements(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode)
    {
        // use the md.assign Macro defined in basicDefs
        // md.assign (arrayIndex, RowIndex, ColIndex, value) const
        auto Loc = offsets.getLocations(sD, sMode, this);

        double V = inputs[voltageInLocation];
        const double* gm = Loc.algStateLoc;
        const double* gmd = Loc.diffStateLoc;
        // const double *gmp = Loc.dstateLoc;

        updateLocalCache(inputs, sD, sMode);

        auto refAlg = Loc.algOffset;
        auto refDiff = Loc.diffOffset;

        auto VLoc = inputLocs[voltageInLocation];
        auto TLoc = inputLocs[angleInLocation];

        // P
        if (hasAlgebraic(sMode)) {
            md.assign(refAlg, refAlg, -(Xd));
            md.assign(refAlg + 1, refAlg + 1, -(Xq));

            if (!hasDifferential(sMode)) {
                return;
            }
            // Id Differential
            md.assign(refAlg, refDiff + 2, -1.0);
            md.assign(refAlg, refDiff + 3, 1.0);
            // Iq Differential

            md.assign(refAlg + 1, refDiff + 4, 1.0);
        }

        // delta
        md.assign(refDiff, refDiff, -sD.cj);
        md.assign(refDiff, refDiff + 1, systemBaseFrequency);

        // omega
        double kVal = -0.5 / H;
        // rv[3] = 0.5*systemBaseFrequency / H*(Pmt - Pe2 - D*(gm[3] / systemBaseFrequency - 1.0)) -
        // gmp[3];
        // Pe = gm[5] * gm[1] - gm[6] * gm[0];
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 1, refAlg, 0.5 * (gm[6]) / H);
            md.assign(refDiff + 1, refAlg + 1, -0.5 * (gm[5]) / H);
        }
        md.assign(refDiff + 1, refDiff + 1, -0.5 * D / H - sD.cj);
        md.assign(refDiff + 1, refDiff + 3, -0.5 * gm[1] / H);
        md.assign(refDiff + 1, refDiff + 4, 0.5 * gm[0] / H);

        md.assignCheckCol(refDiff + 1 + 3,
                          inputLocs[genModelPmechInLocation],
                          -kVal);  // governor: Pm

        //  rvd[2] = drat * ((Eft - gmd[2]) / Tdop - drat2 * gmp[3]) - gmp[2];

        double drat = (Xd) / (Xdp);
        double drat2 = (Xd - Xdp) / (Xd);
        md.assign(refDiff + 2, refDiff + 2, -drat / Tdop - sD.cj);
        md.assign(refDiff + 2, refDiff + 3, -drat * drat2 * sD.cj);

        md.assignCheckCol(refDiff + 2,
                          inputLocs[genModelEftInLocation],
                          drat / Tdop);  // exciter: Ef

        // rvd[3] = systemBaseFrequency * (Vd + Rs * gm[0] + gmd[1] / systemBaseFrequency * gmd[4])
        // - gmp[3]; rvd[4] = systemBaseFrequency * (Vq + Rs * gm[1] - gmd[2] / systemBaseFrequency
        // * gmd[3]) - gmp[4];

        // psib and psiq
        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 3, refAlg, Rs * systemBaseFrequency);
        }
        md.assign(refDiff + 3, refDiff + 1, gmd[4] * systemBaseFrequency);
        md.assign(refDiff + 3, refDiff + 3, -sD.cj);
        md.assign(refDiff + 3, refDiff + 4, gmd[1] * systemBaseFrequency);
        md.assign(refDiff + 3, refDiff, -Vq * systemBaseFrequency);

        if (hasAlgebraic(sMode)) {
            md.assign(refDiff + 4, refAlg + 1, Rs * systemBaseFrequency);
        }
        md.assign(refDiff + 4, refDiff + 1, -gmd[3] * systemBaseFrequency);
        md.assign(refDiff + 4, refDiff + 3, -gmd[1] * systemBaseFrequency);
        md.assign(refDiff + 4, refDiff + 4, -sD.cj);
        md.assign(refDiff + 4, refDiff, Vd * systemBaseFrequency);

        if (VLoc != kNullLocation) {
            md.assign(refDiff + 3, VLoc, Vd / V * systemBaseFrequency);
            md.assign(refDiff + 4, VLoc, Vq / V * systemBaseFrequency);
        }
        if (TLoc != kNullLocation) {
            md.assign(refDiff + 3, TLoc, Vq * systemBaseFrequency);
            md.assign(refDiff + 4, TLoc, -Vd * systemBaseFrequency);
        }
    }

    static const stringVec genModel5type3Names{"id", "iq", "delta", "freq", "eqp", "psid", "psiq"};

    stringVec GenModel5type3::localStateNames() const
    {
        return genModel5type3Names;
    }
}  // namespace genmodels
}  // namespace griddyn
