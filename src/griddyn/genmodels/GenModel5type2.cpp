/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel5type2.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <complex>
#include <string>

namespace griddyn::genmodels {
GenModel5type2::GenModel5type2(const std::string& objName): GenModel5(objName) {}
CoreObject* GenModel5type2::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<GenModel5type2, GenModel5>(this, obj);
    if (copy == nullptr) {
        return obj;
    }
    return copy;
}

void GenModel5type2::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
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
    double* state = m_state.data();
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);

    // Edp and Eqp  and Edpp

    state[6] = Vq + (Rs * state[1]) - (Xdpp * state[0]);
    state[4] = Vd + (Rs * state[0]) + (Xqpp * state[1]);

    // exciter - assign Ef
    const double eft = state[6] - ((Xd - Xdpp) * state[0]);

    const double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xdp + Xl));
    state[5] = state[6] - ((Xdp - Xdpp + (drat * (Xd - Xdp))) * state[0]) - (Taa / Tdop * eft);
    // record Pm = Pset
    // this should be close to P from above
    const double pmt =
        (state[4] * state[0]) + (state[6] * state[1]) + ((Xdpp - Xqpp) * state[0] * state[1]);
    // pmt = P;
    // preset the inputs that should be initialized
    fieldSet[2] = eft;
    fieldSet[3] = pmt;
}

void GenModel5type2::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.algStateLoc;
    const double* diffState = locations.diffStateLoc;
    double* rates = locations.destDiffLoc;
    // Get the exciter field
    const double eft = inputs[genModelEftInLocation];
    const double pmt = inputs[genModelPmechInLocation];

    const double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xdp + Xl));
    // Id and Iq

    // delta
    rates[0] = systemBaseFrequency * (diffState[1] - 1.0);
    // Edpp and Eqp
    rates[2] = (-diffState[2] - ((Xq - Xqpp) * state[1])) / Tqopp;
    rates[3] = (-diffState[3] + ((Xd - Xdp - (drat * (Xd - Xdp))) * state[0]) +
                ((1.0 - (Taa / Tdop)) * eft)) /
        Tdop;
    // Eqpp
    rates[4] = (-diffState[4] + diffState[3] + ((Xdp - Xdpp + (drat * (Xd - Xdp))) * state[0]) +
                (Taa / Tdop * eft)) /
        Tdopp;
    // omega
    const double electricalPower = (diffState[2] * state[0]) + (diffState[4] * state[1]) +
        ((Xdpp - Xqpp) * state[0] * state[1]);

    rates[1] = 0.5 * (pmt - electricalPower - (D * (diffState[1] - 1.0))) / H;
}

void GenModel5type2::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    auto locations = offsets.getLocations(stateData, update, sMode, this);
    updateLocalCache(inputs, stateData, sMode);
    gmlc::utilities::solve2x2(Rs,
                              (Xqpp),
                              -(Xdpp),
                              Rs,
                              locations.diffStateLoc[2] - Vd,
                              locations.diffStateLoc[4] - Vq,
                              locations.destLoc[0],
                              locations.destLoc[1]);
    m_output = -((locations.destLoc[1] * Vq) + (locations.destLoc[0] * Vd));
}

void GenModel5type2::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, resid, sMode, this);

    const double* state = locations.algStateLoc;
    const double* diffState = locations.diffStateLoc;
    const double* stateDerivative = locations.dstateLoc;

    double* algebraicResidual = locations.destLoc;
    double* differentialResidual = locations.destDiffLoc;
    updateLocalCache(inputs, stateData, sMode);

    // Id and Iq
    if (hasAlgebraic(sMode)) {
        algebraicResidual[0] = Vd + (Rs * state[0]) + (Xqpp * state[1]) - diffState[2];
        algebraicResidual[1] = Vq + (Rs * state[1]) - (Xdpp * state[0]) - diffState[4];
    }

    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        // Get the exciter field

        // delta
        differentialResidual[0] -= stateDerivative[0];
        differentialResidual[1] -= stateDerivative[1];
        differentialResidual[2] -= stateDerivative[2];
        differentialResidual[3] -= stateDerivative[3];
        differentialResidual[4] -= stateDerivative[4];
    }
}

void GenModel5type2::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    // matrixData.assign (arrayIndex, RowIndex, ColIndex, value) const
    auto locations = offsets.getLocations(stateData, sMode, this);

    auto refAlg = locations.algOffset;
    auto refDiff = locations.diffOffset;
    const double* state = locations.algStateLoc;
    const double* diffState = locations.diffStateLoc;
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    auto voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    auto angleLoc = inputLocs[ANGLE_IN_LOCATION];

    updateLocalCache(inputs, stateData, sMode);

    // P
    if (hasAlgebraic(sMode)) {
        if (angleLoc != kNullLocation) {
            matrixData.assign(refAlg, angleLoc, Vq);
            matrixData.assign(refAlg + 1, angleLoc, -Vd);
        }

        // Q
        if (voltageLoc != kNullLocation) {
            matrixData.assign(refAlg, voltageLoc, Vd / voltage);
            matrixData.assign(refAlg + 1, voltageLoc, Vq / voltage);
        }

        matrixData.assign(refAlg, refAlg, Rs);
        matrixData.assign(refAlg, refAlg + 1, (Xqpp));

        matrixData.assign(refAlg + 1, refAlg, -(Xdpp));
        matrixData.assign(refAlg + 1, refAlg + 1, Rs);

        if (isAlgebraicOnly(sMode)) {
            return;
        }

        // Id Differential

        matrixData.assign(refAlg, refDiff, -Vq);
        matrixData.assign(refAlg, refDiff + 2, -1.0);

        // Iq differential

        matrixData.assign(refAlg + 1, refDiff, Vd);
        matrixData.assign(refAlg + 1, refDiff + 4, -1.0);
    }
    // delta
    matrixData.assign(refDiff, refDiff, -stateData.cj);
    matrixData.assign(refDiff, refDiff + 1, systemBaseFrequency);

    // omega
    const double kVal = -0.5 / H;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 1,
                          refAlg,
                          -0.5 * (diffState[2] + ((Xdpp - Xqpp) * state[1])) / H);
        matrixData.assign(refDiff + 1,
                          refAlg + 1,
                          -0.5 * (diffState[4] + ((Xdpp - Xqpp) * state[0])) / H);
    }
    matrixData.assign(refDiff + 1, refDiff + 1, (-0.5 * D / H) - stateData.cj);
    matrixData.assign(refDiff + 1, refDiff + 2, -0.5 * state[0] / H);
    matrixData.assign(refDiff + 1, refDiff + 4, -0.5 * state[1] / H);

    matrixData.assignCheckCol(refDiff + 1,
                              inputLocs[genModelPmechInLocation],
                              -kVal);  // governor: Pm

    const double drat = Tdopp * (Xdpp + Xl) / (Tdop * (Xdp + Xl));

    // Edpp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 2, refAlg + 1, -(Xq - Xqpp) / Tqopp);
    }
    matrixData.assign(refDiff + 2, refDiff + 2, (-1 / Tqopp) - stateData.cj);

    // Eqp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 3, refAlg, (Xd - Xdp - (drat * (Xd - Xdp))) / Tdop);
    }
    matrixData.assign(refDiff + 3, refDiff + 3, (-1.0 / Tdop) - stateData.cj);

    if (inputLocs[genModelEftInLocation] != kNullLocation)  // check if exciter exists
    {
        matrixData.assign(refDiff + 3,
                          inputLocs[genModelEftInLocation],
                          (1.0 - (Taa / Tdop)) / Tdop);  // exciter: Ef
        matrixData.assign(refDiff + 4, inputLocs[genModelEftInLocation], Taa / Tdop / Tdopp);
    }

    // Eqpp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 4, refAlg, (Xdp - Xdpp + (drat * (Xd - Xdp))) / Tdopp);
    }
    matrixData.assign(refDiff + 4, refDiff + 3, 1.0 / Tdopp);
    matrixData.assign(refDiff + 4, refDiff + 4, (-1.0 / Tdopp) - stateData.cj);
}

static const stringVec GEN_MODEL5_TYPE2_NAMES{"id", "iq", "delta", "freq", "edpp", "eqp", "eqpp"};

stringVec GenModel5type2::localStateNames() const
{
    return GEN_MODEL5_TYPE2_NAMES;
}
}  // namespace griddyn::genmodels
