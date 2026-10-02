/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModel5.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <cstddef>
#include <complex>
#include <string>

namespace griddyn::genmodels {
GenModel5::GenModel5(const std::string& objName): GenModel4(objName) {}
CoreObject* GenModel5::clone(CoreObject* obj) const
{
    auto* clonedModel = cloneBase<GenModel5, GenModel4>(this, obj);
    if (clonedModel == nullptr) {
        return obj;
    }
    clonedModel->Tqopp = Tqopp;
    clonedModel->Taa = Taa;
    clonedModel->Tdopp = Tdopp;
    clonedModel->Xdpp = Xdpp;
    clonedModel->Xqpp = Xqpp;
    return clonedModel;
}

void GenModel5::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
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
    double* generatorState = m_state.data();
    computeInitialAngleAndCurrent(inputs, desiredOutput, Rs, Xq);

    // Edp and Eqp  and Edpp

    generatorState[5] = Vq + (Rs * generatorState[1]) - (Xdp * generatorState[0]);
    generatorState[6] = Vd + (Rs * generatorState[0]) + (Xqp * generatorState[1]);

    const double transientReactanceRatio = Tqopp * (Xdp + Xl) / (Tqop * (Xqp + Xl));
    generatorState[4] = generatorState[6] +
        ((Xqp - Xdp + (transientReactanceRatio * (Xq - Xqp))) * generatorState[1]);

    // record Pm = Pset
    // this should be close to P from above
    const double mechanicalPower = (generatorState[6] * generatorState[0]) +
        (generatorState[5] * generatorState[1]) +
        ((Xdp - Xqp) * generatorState[0] * generatorState[1]);
    // exciter - assign Ef
    const double exciterField = generatorState[5] - ((Xd - Xdp) * generatorState[0]);
    // preset the inputs that should be initialized
    fieldSet[2] = exciterField;
    fieldSet[3] = mechanicalPower;
}

void GenModel5::algebraicUpdate(const IOdata& inputs,
                                const StateData& stateData,
                                double update[],
                                const SolverMode& sMode,
                                double /*alpha*/)
{
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    updateLocalCache(inputs, stateData, sMode);
    gmlc::utilities::solve2x2(Rs,
                              (Xqp),
                              -(Xdp),
                              Rs,
                               locations.diffStateLoc[4] - Vd,
                               locations.diffStateLoc[3] - Vq,
                               locations.destLoc[0],
                               locations.destLoc[1]);
    m_output = -((locations.destLoc[1] * Vq) + (locations.destLoc[0] * Vd));
}

void GenModel5::residual(const IOdata& inputs,
                         const StateData& stateData,
                         double resid[],
                         const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const double* generatorState = locations.algStateLoc;
    const double* differentialState = locations.diffStateLoc;
    const double* differentialDerivative = locations.dstateLoc;

    double* algebraicResidual = locations.destLoc;
    double* differentialResidual = locations.destDiffLoc;
    updateLocalCache(inputs, stateData, sMode);

    // Id and Iq
    if (hasAlgebraic(sMode)) {
        algebraicResidual[0] =
            Vd + (Rs * generatorState[0]) + (Xqp * generatorState[1]) - differentialState[4];
        algebraicResidual[1] =
            Vq + (Rs * generatorState[1]) - (Xdp * generatorState[0]) - differentialState[3];
    }

    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        // Get the exciter field

        // delta
        differentialResidual[0] -= differentialDerivative[0];
        differentialResidual[1] -= differentialDerivative[1];
        differentialResidual[2] -= differentialDerivative[2];
        differentialResidual[3] -= differentialDerivative[3];
        differentialResidual[4] -= differentialDerivative[4];
    }
}

void GenModel5::derivative(const IOdata& inputs,
                           const StateData& stateData,
                           double deriv[],
                           const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* algebraicState = locations.algStateLoc;
    const double* differentialState = locations.diffStateLoc;
    double* derivativeValues = locations.destDiffLoc;
    // Get the exciter field
    const double exciterField = inputs[genModelEftInLocation];
    const double mechanicalPower = inputs[genModelPmechInLocation];

    // Id and Iq

    // delta
    derivativeValues[0] = systemBaseFrequency * (differentialState[1] - 1.0);
    // Edp and Eqp

    const double transientReactanceRatio = Tqopp * (Xdp + Xl) / (Tqop * (Xqp + Xl));
    // Edp and Eqp
    derivativeValues[2] =
        (-differentialState[2] -
         ((Xq - Xqp - (transientReactanceRatio * (Xq - Xqp))) * algebraicState[1])) /
        Tqop;
    derivativeValues[3] =
        (-differentialState[3] + ((Xd - Xdp) * algebraicState[0]) + exciterField) / Tdop;
    // Edpp
    derivativeValues[4] =
        (-differentialState[4] + differentialState[2] -
         ((Xqp - Xdp + (transientReactanceRatio * (Xq - Xqp))) * algebraicState[1])) /
        Tqopp;
    // omega

    const double electricalPower = (differentialState[4] * algebraicState[0]) +
        (differentialState[3] * algebraicState[1]) +
        ((Xdp - Xqp) * algebraicState[0] * algebraicState[1]);
    derivativeValues[1] =
        0.5 * (mechanicalPower - electricalPower - (D * (differentialState[1] - 1.0))) / H;
}

void GenModel5::jacobianElements(const IOdata& inputs,
                                 const StateData& stateData,
                                 MatrixData<double>& matrixData,
                                 const IOlocs& inputLocs,
                                 const SolverMode& sMode)
{
    // md.assign (arrayIndex, RowIndex, ColIndex, value) const
    const auto locations = offsets.getLocations(stateData, nullptr, sMode, this);

    auto refAlg = locations.algOffset;
    auto refDiff = locations.diffOffset;
    const double* generatorState = locations.algStateLoc;

    const auto voltageLocation = inputLocs[VOLTAGE_IN_LOCATION];
    const auto angleLocation = inputLocs[ANGLE_IN_LOCATION];

    updateLocalCache(inputs, stateData, sMode);

    const bool hasAlgebraicEquations = hasAlgebraic(sMode);
    // P
    if (hasAlgebraicEquations) {
        if (angleLocation != kNullLocation) {
            matrixData.assign(refAlg, angleLocation, Vq);
            matrixData.assign(refAlg + 1, angleLocation, -Vd);
        }

        // Q
        if (voltageLocation != kNullLocation) {
            const double voltageMagnitude = inputs[VOLTAGE_IN_LOCATION];
            matrixData.assign(refAlg, voltageLocation, Vd / voltageMagnitude);
            matrixData.assign(refAlg + 1, voltageLocation, Vq / voltageMagnitude);
        }

        matrixData.assign(refAlg, refAlg, Rs);
        matrixData.assign(refAlg, refAlg + 1, Xqp);

        matrixData.assign(refAlg + 1, refAlg, -Xdp);
        matrixData.assign(refAlg + 1, refAlg + 1, Rs);

        if (isAlgebraicOnly(sMode)) {
            return;
        }

        // Id Additional

        matrixData.assign(refAlg, refDiff, -Vq);
        matrixData.assign(refAlg, refDiff + 4, -1);

        // Iq Additional
        matrixData.assign(refAlg + 1, refDiff, Vd);
        matrixData.assign(refAlg + 1, refDiff + 3, -1);
    }

    if (hasDifferential(sMode)) {
        // delta
        matrixData.assign(refDiff, refDiff, -stateData.cj);
        matrixData.assign(refDiff, refDiff + 1, systemBaseFrequency);

        // omega
        const double inertiaFactor = -0.5 / H;
        if (hasAlgebraicEquations) {
            matrixData.assign(refDiff + 1,
                              refAlg,
                              -0.5 * (generatorState[6] + ((Xdp - Xqp) * generatorState[1])) / H);
            matrixData.assign(refDiff + 1,
                              refAlg + 1,
                              -0.5 * (generatorState[5] + ((Xdp - Xqp) * generatorState[0])) / H);
        }

        matrixData.assign(refDiff + 1, refDiff + 1, (-0.5 * D / H) - stateData.cj);
        matrixData.assign(refDiff + 1, refDiff + 4, -0.5 * generatorState[0] / H);
        matrixData.assign(refDiff + 1, refDiff + 3, -0.5 * generatorState[1] / H);

        matrixData.assignCheckCol(refDiff + 1,
                                  inputLocs[genModelPmechInLocation],
                                  -inertiaFactor);  // governor: Pm

        const double transientReactanceRatio = Tqopp * (Xdp + Xl) / (Tqop * (Xqp + Xl));
        // Edp
        if (hasAlgebraicEquations) {
            matrixData.assign(refDiff + 2,
                              refAlg + 1,
                              -(Xq - Xqp - (transientReactanceRatio * (Xq - Xqp))) / Tqop);
        }
        matrixData.assign(refDiff + 2, refDiff + 2, -(1 / Tqop) - stateData.cj);

        // Eqp
        if (hasAlgebraicEquations) {
            matrixData.assign(refDiff + 3, refAlg, (Xd - Xdp) / Tdop);
        }
        matrixData.assign(refDiff + 3, refDiff + 3, -(1 / Tdop) - stateData.cj);

        matrixData.assignCheckCol(refDiff + 3,
                                  inputLocs[genModelEftInLocation],
                                  1 / Tdop);  // exciter: Ef

        // Edpp
        if (hasAlgebraicEquations) {
            matrixData.assign(refDiff + 4,
                              refAlg + 1,
                              -(Xqp - Xdp + (transientReactanceRatio * (Xq - Xqp))) / Tqopp);
        }
        matrixData.assign(refDiff + 4, refDiff + 2, 1 / Tqopp);
        matrixData.assign(refDiff + 4, refDiff + 4, -(1 / Tqopp) - stateData.cj);
    }
}

static const stringVec GEN_MODEL_5_NAMES{"id", "iq", "delta", "freq", "edp", "eqp", "edpp"};

stringVec GenModel5::localStateNames() const
{
    return GEN_MODEL_5_NAMES;
}

double GenModel5::getFreq(const StateData& stateDataValue,
                          const SolverMode& sMode,
                          index_t* freqOffset) const
{
    if (isLocal(sMode)) {
        const auto frequencyState = offsets.local().local.algSize + 1;
        if (freqOffset != nullptr) {
            *freqOffset = kNullLocation;
        }
        return (frequencyState >= 0 && static_cast<std::size_t>(frequencyState) < m_state.size()) ?
            m_state[static_cast<std::size_t>(frequencyState)] :
            1.0;
    }
    if (!stateDataValue.empty()) {
        const auto loc = offsets.getLocations(stateDataValue, sMode, this);
        if (freqOffset != nullptr) {
            *freqOffset = isAlgebraicOnly(sMode) ? kNullLocation : loc.diffOffset + 1;
        }
        return loc.diffStateLoc[1];
    }
    if (freqOffset != nullptr) {
        *freqOffset = isAlgebraicOnly(sMode) ? kNullLocation : offsets.getDiffOffset(sMode) + 1;
    }
    return 1.0;
}

double GenModel5::getAngle(const StateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* angleOffset) const
{
    if (isLocal(sMode)) {
        const auto angleState = offsets.local().local.algSize;
        if (angleOffset != nullptr) {
            *angleOffset = kNullLocation;
        }
        return (angleState >= 0 && static_cast<std::size_t>(angleState) < m_state.size()) ?
            m_state[static_cast<std::size_t>(angleState)] :
            0.0;
    }
    if (!stateDataValue.empty()) {
        const auto loc = offsets.getLocations(stateDataValue, sMode, this);
        if (angleOffset != nullptr) {
            *angleOffset = isAlgebraicOnly(sMode) ? kNullLocation : loc.diffOffset;
        }
        return loc.diffStateLoc[0];
    }
    if (angleOffset != nullptr) {
        *angleOffset = isAlgebraicOnly(sMode) ? kNullLocation : offsets.getDiffOffset(sMode);
    }
    return 0.0;
}

// set parameters
void GenModel5::set(std::string_view param, std::string_view val)
{
    GenModel4::set(param, val);
}
void GenModel5::set(std::string_view param, double val, units::unit unitType)
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

}  // namespace griddyn::genmodels
