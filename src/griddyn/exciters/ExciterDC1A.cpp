/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterDC1A.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
namespace griddyn::exciters {
ExciterDC1A::ExciterDC1A(const std::string& objName): ExciterIEEEtype1(objName)
{
    // default values
    Ka = 12;
    Ta = 0.06;
    Ke = 1.0;
    Te = .46;
    Kf = .1;
    Tf = 1.0;
    Aex = 0.0032;  // (3.1,.33) and (2.3,.1)
    Bex = 1.4924;
    Vrmin = -.9;
    Vrmax = 1;
}

// cloning function
CoreObject* ExciterDC1A::clone(CoreObject* obj) const
{
    ExciterDC1A* gdE;
    if (obj == nullptr) {
        gdE = new ExciterDC1A();
    } else {
        gdE = dynamic_cast<ExciterDC1A*>(obj);
        if (gdE == nullptr) {
            Exciter::clone(obj);
            return obj;
        }
    }
    ExciterIEEEtype1::clone(gdE);
    gdE->Tc = Tc;
    gdE->Tb = Tb;
    return gdE;
}

void ExciterDC1A::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 4;
    offsets.local().local.jacSize = 19;
    checkForLimits();
}

// initial conditions
void ExciterDC1A::dynObjectInitializeB(const IOdata& inputs,
                                       const IOdata& desiredOutput,
                                       IOdata& fieldSet)
{
    Exciter::dynObjectInitializeB(inputs,
                                  desiredOutput,
                                  fieldSet);  // this will dynInitializeB the field state if need be
    double* stateValues = m_state.data();
    stateValues[1] = (Ke + (Aex * exp(Bex * stateValues[0]))) * stateValues[0];  // Vr
    stateValues[2] = stateValues[1] / Ka;  // X
    stateValues[3] = (stateValues[0] * Kf) / Tf;  // Rf

    vBias = inputs[voltageInLocation] + (stateValues[1] / Ka) - Vref;
    fieldSet[1] = Vref;
}

// residual
void ExciterDC1A::residual(const IOdata& inputs,
                           const stateData& stateDataValue,
                           double resid[],
                           const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    derivative(inputs, stateDataValue, resid, sMode);

    auto offset = offsets.getDiffOffset(sMode);
    const double* esp = stateDataValue.dstate_dt + offset;
    resid[offset] -= esp[0];
    resid[offset + 1] -= esp[1];
    resid[offset + 2] -= esp[2];
    resid[offset + 3] -= esp[3];
}

void ExciterDC1A::derivative(const IOdata& inputs,
                             const stateData& stateDataValue,
                             double deriv[],
                             const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateDataValue, deriv, sMode, this);
    const double* exciterState = loc.diffStateLoc;
    double* derivatives = loc.destDiffLoc;
    const double voltage = inputs[voltageInLocation];
    derivatives[0] =
        ((-(Ke + (Aex * exp(Bex * exciterState[0]))) * exciterState[0]) + exciterState[1]) / Te;
    if (opFlags[outsideVoltageLimits]) {
        derivatives[1] = 0;
    } else {
        derivatives[1] =
            (-exciterState[1] +
             ((((Vref + vBias - voltage) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka *
              Tc / Tb) +
             ((exciterState[2] * (Tb - Tc) * Ka) / Tb)) /
            Ta;
    }
    derivatives[2] =
        ((-exciterState[2] + (Vref + vBias - voltage) - ((exciterState[0] * Kf) / Tf)) +
         exciterState[3]) /
        Tb;
    derivatives[3] = (-exciterState[3] + ((exciterState[0] * Kf) / Tf)) / Tf;
}

// Jacobian
void ExciterDC1A::jacobianElements(const IOdata& inputs,
                                   const stateData& stateDataValue,
                                   matrixData<double>& matrixDataValue,
                                   const IOlocs& inputLocs,
                                   const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);
    auto refI = offset;

    auto voltageLoc = inputLocs[voltageInLocation];
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)

    // Ef
    const double temp1 = (-(Ke +
                            (Aex * exp(Bex * stateDataValue.state[offset]) *
                             (1.0 + (Bex * stateDataValue.state[offset])))) /
                          Te) -
        stateDataValue.cj;
    matrixDataValue.assign(refI, refI, temp1);
    matrixDataValue.assign(refI, refI + 1, 1.0 / Te);

    if (opFlags[outsideVoltageLimits]) {
        limitJacobian(
            inputs[voltageInLocation], voltageLoc, refI + 1, stateDataValue.cj, matrixDataValue);
    } else {
        // Vr
        if (voltageLoc != kNullLocation) {
            matrixDataValue.assign(refI + 1, voltageLoc, -Ka * Tc / (Ta * Tb));
        }
        matrixDataValue.assign(refI + 1, refI, -Ka * Kf * Tc / (Tf * Ta * Tb));
        matrixDataValue.assign(refI + 1, refI + 1, (-1.0 / Ta) - stateDataValue.cj);
        matrixDataValue.assign(refI + 1, refI + 2, Ka * (Tb - Tc) / (Ta * Tb));
        matrixDataValue.assign(refI + 1, refI + 3, Ka * Tc / (Ta * Tb));
    }

    // X
    if (voltageLoc != kNullLocation) {
        matrixDataValue.assign(refI + 2, voltageLoc, -1.0 / Tb);
    }
    matrixDataValue.assign(refI + 2, refI, -Kf / (Tf * Tb));
    matrixDataValue.assign(refI + 2, refI + 2, (-1.0 / Tb) - stateDataValue.cj);
    matrixDataValue.assign(refI + 2, refI + 3, 1.0 / Tb);
    // Rf
    matrixDataValue.assign(refI + 3, refI, Kf / (Tf * Tf));
    matrixDataValue.assign(refI + 3, refI + 3, (-1.0 / Tf) - stateDataValue.cj);

    // printf("%f--%f--\n",sD.time,sD.cj);
}

void ExciterDC1A::limitJacobian(double /*V*/,
                                int /*Vloc*/,
                                int refLoc,
                                double cjValue,
                                matrixData<double>& matrixDataValue)
{
    matrixDataValue.assign(refLoc, refLoc, cjValue);
}

void ExciterDC1A::rootTest(const IOdata& inputs,
                           const stateData& stateDataValue,
                           double root[],
                           const SolverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode);
    const double* exciterState = stateDataValue.state + offset;

    const int rootOffset = offsets.getRootOffset(sMode);
    if (opFlags[outsideVoltageLimits]) {
        root[rootOffset] =
            ((((Vref + vBias - inputs[voltageInLocation]) - ((exciterState[0] * Kf) / Tf)) +
              exciterState[3]) *
             Ka * Tc / Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) - exciterState[1];
    } else {
        root[rootOffset] = std::min(Vrmax - exciterState[1], exciterState[1] - Vrmin) + 0.00001;
        if (exciterState[1] > Vrmax) {
            opFlags.set(triggerHigh);
        }
    }
}

ChangeCode ExciterDC1A::rootCheck(const IOdata& inputs,
                                  const stateData& /*sD*/,
                                  const SolverMode& /*sMode*/,
                                  CheckLevel /*level*/)
{
    double* exciterState = m_state.data();
    double test;
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[outsideVoltageLimits]) {
        test = ((((Vref + vBias - inputs[voltageInLocation]) - ((exciterState[0] * Kf) / Tf)) +
                 exciterState[3]) *
                Ka * Tc / Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) - exciterState[1];
        if (opFlags[triggerHigh]) {
            if (test < 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(outsideVoltageLimits);
                opFlags.reset(triggerHigh);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(outsideVoltageLimits);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (exciterState[1] > Vrmax + 0.00001) {
            opFlags.set(triggerHigh);
            opFlags.set(outsideVoltageLimits);
            exciterState[1] = Vrmax;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (exciterState[1] < Vrmin - 0.00001) {
            opFlags.reset(triggerHigh);
            opFlags.set(outsideVoltageLimits);
            exciterState[1] = Vrmin;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

static const stringVec DC1A_FIELDS{"ef", "vr", "x", "rf"};

stringVec ExciterDC1A::localStateNames() const
{
    return DC1A_FIELDS;
}
void ExciterDC1A::set(std::string_view param, std::string_view val)
{
    ExciterIEEEtype1::set(param, val);
}

// set parameters
void ExciterDC1A::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tb") {
        Tb = val;
    } else if (param == "tc") {
        Tc = val;
    } else {
        ExciterIEEEtype1::set(param, val, unitType);
    }
}

}  // namespace griddyn::exciters
