/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel8.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::genmodels {
GenModel8::GenModel8(const std::string& objName): GenModel6(objName) {}
CoreObject* GenModel8::clone(CoreObject* obj) const
{
    auto* genModelClone = cloneBase<GenModel8, GenModel6>(this, obj);
    if (genModelClone == nullptr) {
        return obj;
    }
    return genModelClone;
}

void GenModel8::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 8;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 47;
}
// initial conditions
void GenModel8::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq - Xl);
    double* genState = m_state.data();

    // Edp and Eqp  and Edpp

    genState[7] = Vq + (Rs * genState[1]) - ((Xdpp - Xl) * genState[0]);
    genState[6] = Vd + (Rs * genState[0]) + ((Xqpp - Xl) * genState[1]);

    const double qrat = Tqopp * (Xqpp - Xl) / (Tqop * (Xqp - Xl));
    const double drat = Tdopp * (Xdpp - Xl) / (Tdop * (Xdp - Xl));
    genState[4] = genState[6] + ((Xqp - Xqpp + (qrat * (Xq - Xqp))) * genState[1]);

    // record Pm = Pset
    // this should be close to P from above
    const double mechanicalPower = (genState[6] * genState[0]) + (genState[7] * genState[1]) +
        ((Xdpp - Xqpp) * genState[0] * genState[1]);

    // exciter - assign Ef
    const double fieldVoltage = genState[7] - ((Xd - Xdpp) * genState[0]);
    // preset the inputs that should be initialized
    fieldSet[2] = fieldVoltage;
    fieldSet[3] = mechanicalPower;

    genState[5] = genState[7] - ((Xdp - Xdpp + (drat * (Xd - Xdp))) * genState[0]) +
        (Taa / Tdop * fieldVoltage);

    genState[8] = genState[7] + ((Xdpp - Xl) * genState[0]);
    genState[9] = -genState[6] + ((Xqpp - Xl) * genState[1]);
}

void GenModel8::algebraicUpdate(const IOdata& inputs,
                                const StateData& stateData,
                                double update[],
                                const SolverMode& sMode,
                                double /*alpha*/)
{
    auto locations = offsets.getLocations(stateData, update, sMode, this);
    updateLocalCache(inputs, stateData, sMode);

    gmlc::utilities::solve2x2(Rs,
                              Xqpp - Xl,
                              -(Xdpp - Xl),
                              Rs,
                              locations.diffStateLoc[4] - Vd,
                              locations.diffStateLoc[5] - Vq,
                              locations.destLoc[0],
                              locations.destLoc[1]);
    m_output = -((locations.destLoc[1] * Vq) + (locations.destLoc[0] * Vd));
}

void GenModel8::derivative(const IOdata& inputs,
                           const StateData& stateData,
                           double deriv[],
                           const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    auto locations = offsets.getLocations(stateData, deriv, sMode, this);

    const double* algState = locations.algStateLoc;
    const double* diffState = locations.diffStateLoc;
    // const double *gmp = Loc.dstateLoc;

    double* diffResidual = locations.destDiffLoc;
    updateLocalCache(inputs, stateData, sMode);

    // Get the exciter field
    const double fieldVoltage = inputs[genModelEftInLocation];
    const double mechanicalPower = inputs[genModelPmechInLocation];

    const double qrat = Tqopp * (Xqpp - Xl) / (Tqop * (Xqp - Xl));
    const double drat = Tdopp * (Xdpp - Xl) / (Tdop * (Xdp - Xl));

    diffResidual[0] = systemBaseFrequency * (diffState[1] - 1.0);
    // Edp and Eqp
    diffResidual[2] = (-diffState[2] -
                       ((Xq - Xqp - (qrat * (Xq - Xqp))) * algState[1])) /
        Tqop;
    diffResidual[3] = (-diffState[3] +
                       ((Xd - Xdp - (drat * (Xd - Xdp))) * algState[0]) +
                       ((1.0 - (Taa / Tdop)) * fieldVoltage)) /
        Tdop;
    // Edpp
    diffResidual[4] = (-diffState[4] + diffState[2] -
                       ((Xqp - Xqpp + (qrat * (Xq - Xqp))) * algState[1])) /
        Tqopp;
    diffResidual[5] = (-diffState[5] + diffState[3] +
                       ((Xdp - Xdpp + (drat * (Xd - Xdp))) * algState[0]) +
                       (Taa / Tdop * fieldVoltage)) /
        Tdopp;
    // omega
    // double Pe = (gm[6] * gm[0]) + (gm[7] * gm[1]) + ((Xdpp - Xqpp) * gm[0] * gm[1]);
    const double electricalPower = (diffState[6] * algState[1]) -
        (diffState[7] * algState[0]);
    diffResidual[1] = 0.5 * (mechanicalPower - electricalPower -
                             (D * (diffState[1] - 1.0))) /
        H;
    // psid and psiq
    diffResidual[6] = systemBaseFrequency *
        (Vd + (Rs * algState[0]) + (diffState[1] * diffState[7]));
    diffResidual[7] = systemBaseFrequency *
        (Vq + (Rs * algState[1]) - (diffState[1] * diffState[6]));
}

void GenModel8::residual(const IOdata& inputs,
                         const StateData& stateData,
                         double resid[],
                         const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, resid, sMode, this);

    const double* algState = locations.algStateLoc;
    const double* diffState = locations.diffStateLoc;
    const double* stateDerivative = locations.dstateLoc;

    double* algResidual = locations.destLoc;
    double* diffResidual = locations.destDiffLoc;
    updateLocalCache(inputs, stateData, sMode);

    if (hasAlgebraic(sMode)) {
        // Id and Iq
        algResidual[0] = Vd + (Rs * algState[0]) + ((Xqpp - Xl) * algState[1]) - diffState[4];
        algResidual[1] = Vq + (Rs * algState[1]) - ((Xdpp - Xl) * algState[0]) - diffState[5];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        /// delta
        diffResidual[0] -= stateDerivative[0];
        diffResidual[1] -= stateDerivative[1];
        diffResidual[2] -= stateDerivative[2];
        diffResidual[3] -= stateDerivative[3];
        diffResidual[4] -= stateDerivative[4];
        diffResidual[5] -= stateDerivative[5];
        diffResidual[6] -= stateDerivative[6];
        diffResidual[7] -= stateDerivative[7];
    }
}

void GenModel8::jacobianElements(const IOdata& inputs,
                                 const StateData& stateData,
                                 MatrixData<double>& matrixData,
                                 const IOlocs& inputLocs,
                                 const SolverMode& sMode)
{
    // matrixData.assign (arrayIndex, RowIndex, ColIndex, value) const
    auto locations = offsets.getLocations(stateData, nullptr, sMode, this);

    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double* algState = locations.algStateLoc;
    const double* diffState = locations.diffStateLoc;
    //  const double *gmp = Loc.dstateLoc;

    updateLocalCache(inputs, stateData, sMode);

    auto refAlg = locations.algOffset;
    auto refDiff = locations.diffOffset;

    auto voltageLocation = inputLocs[VOLTAGE_IN_LOCATION];
    auto angleLocation = inputLocs[ANGLE_IN_LOCATION];

    if (hasAlgebraic(sMode)) {
        // P
        if (angleLocation != kNullLocation) {
            matrixData.assign(refAlg, angleLocation, Vq);
            matrixData.assign(refAlg + 1, angleLocation, -Vd);
        }

        // Q
        if (voltageLocation != kNullLocation) {
            matrixData.assign(refAlg, voltageLocation, Vd / voltage);
            matrixData.assign(refAlg + 1, voltageLocation, Vq / voltage);
        }

        matrixData.assign(refAlg, refAlg, Rs);
        matrixData.assign(refAlg, refAlg + 1, (Xqpp - Xl));

        matrixData.assign(refAlg + 1, refAlg, -(Xdpp - Xl));
        matrixData.assign(refAlg + 1, refAlg + 1, Rs);

        if (isAlgebraicOnly(sMode)) {
            return;
        }

        // Id Differential

        matrixData.assign(refAlg, refDiff, -Vq);
        matrixData.assign(refAlg, refDiff + 4, -1.0);

        // Iq Differential

        matrixData.assign(refAlg + 1, refDiff, Vd);
        matrixData.assign(refAlg + 1, refDiff + 5, -1.0);
    }
    // delta
    matrixData.assign(refDiff, refDiff, -stateData.cj);
    matrixData.assign(refDiff, refDiff + 1, systemBaseFrequency);

    // omega
    // Pe2 = gm[8] * gm[1] - gm[9] * gm[0];
    const double kValue = -0.5 / H;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 1, refAlg, 0.5 * (diffState[7]) / H);
        matrixData.assign(refDiff + 1, refAlg + 1, -0.5 * (diffState[6]) / H);
    }
    matrixData.assign(refDiff + 1, refDiff + 1, (-0.5 * D / H) - stateData.cj);
    matrixData.assign(refDiff + 1, refDiff + 6, -0.5 * algState[1] / H);
    matrixData.assign(refDiff + 1, refDiff + 7, 0.5 * algState[0] / H);

    matrixData.assignCheckCol(refDiff + 1, inputLocs[genModelPmechInLocation], -kValue);  // governor: Pm

    const double qrat = Tqopp * (Xqpp - Xl) / (Tqop * (Xqp - Xl));
    const double drat = Tdopp * (Xdpp - Xl) / (Tdop * (Xdp - Xl));

    // Edp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 2,
                          refAlg + 1,
                          -(Xq - Xqp - (qrat * (Xq - Xqp))) / Tqop);
    }
    matrixData.assign(refDiff + 2, refDiff + 2, (-1.0 / Tqop) - stateData.cj);

    // Eqp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 3,
                          refAlg,
                          (Xd - Xdp - (drat * (Xd - Xdp))) / Tdop);
    }
    matrixData.assign(refDiff + 3, refDiff + 3, (-1.0 / Tdop) - stateData.cj);

    if (inputLocs[genModelEftInLocation] != kNullLocation)  // check if exciter exists
    {
        matrixData.assign(refDiff + 3,
                  inputLocs[genModelEftInLocation],
                  (1.0 - (Taa / Tdop)) / Tdop);  // exciter: Ef
        matrixData.assign(refDiff + 5, inputLocs[genModelEftInLocation], Taa / Tdop / Tdopp);
    }
    // Edpp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 4,
                          refAlg + 1,
                          -(Xqp - Xqpp + (qrat * (Xq - Xqp))) / Tqopp);
    }
    matrixData.assign(refDiff + 4, refDiff + 2, 1.0 / Tqopp);
    matrixData.assign(refDiff + 4, refDiff + 4, (-1.0 / Tqopp) - stateData.cj);

    // Eqpp
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 5,
                          refAlg,
                          (Xdp - Xdpp + (drat * (Xd - Xdp))) / Tdopp);
    }
    matrixData.assign(refDiff + 5, refDiff + 3, 1.0 / Tdopp);
    matrixData.assign(refDiff + 5, refDiff + 5, (-1.0 / Tdopp) - stateData.cj);

    /*
rv[8] = systemBaseFrequency*(Vd + Rs*gm[0] + gm[3] / systemBaseFrequency*gm[9]) - gmp[8];
rv[9] = systemBaseFrequency*(Vq + Rs*gm[1] - gm[3] / systemBaseFrequency*gm[8]) - gmp[9];
*/
    // psib and psiq
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 6, refAlg, Rs * systemBaseFrequency);
    }
    matrixData.assign(refDiff + 6, refDiff + 1, diffState[7] * systemBaseFrequency);
    matrixData.assign(refDiff + 6, refDiff + 6, -stateData.cj);
    matrixData.assign(refDiff + 6, refDiff + 7, diffState[1] * systemBaseFrequency);
    matrixData.assign(refDiff + 6, refDiff, -Vq * systemBaseFrequency);

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refDiff + 7, refAlg + 1, Rs * systemBaseFrequency);
    }
    matrixData.assign(refDiff + 7, refDiff + 1, -diffState[6] * systemBaseFrequency);
    matrixData.assign(refDiff + 7, refDiff + 6, -diffState[1] * systemBaseFrequency);
    matrixData.assign(refDiff + 7, refDiff + 7, -stateData.cj);
    matrixData.assign(refDiff + 7, refDiff, Vd * systemBaseFrequency);

    if (voltageLocation != kNullLocation) {
        matrixData.assign(refDiff + 6, voltageLocation, Vd / voltage * systemBaseFrequency);
        matrixData.assign(refDiff + 7, voltageLocation, Vq / voltage * systemBaseFrequency);
    }
    if (angleLocation != kNullLocation) {
        matrixData.assign(refDiff + 6, angleLocation, Vq * systemBaseFrequency);
        matrixData.assign(refDiff + 7, angleLocation, -Vd * systemBaseFrequency);
    }
}

static const stringVec GEN_MODEL_8_NAMES{
    "id", "iq", "delta", "freq", "edp", "eqp", "edpp", "eqpp", "psid", "psiq"};

stringVec GenModel8::localStateNames() const
{
    return GEN_MODEL_8_NAMES;
}
}  // namespace griddyn::genmodels
