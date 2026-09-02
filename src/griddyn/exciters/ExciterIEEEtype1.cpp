/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterIEEEtype1.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::exciters {
ExciterIEEEtype1::ExciterIEEEtype1(const std::string& objName): Exciter(objName)
{
    // default values that are different from inherited default values
    Ka = 20;
    Ta = 0.04;

    limitState = 1;
}

// cloning function
CoreObject* ExciterIEEEtype1::clone(CoreObject* obj) const
{
    auto* gdE = cloneBase<ExciterIEEEtype1, Exciter>(this, obj);
    if (gdE == nullptr) {
        return obj;
    }
    gdE->Ke = Ke;
    gdE->Te = Te;
    gdE->Kf = Kf;
    gdE->Tf = Tf;
    gdE->Tr = Tr;
    gdE->E1 = E1;
    gdE->Se1 = Se1;
    gdE->E2 = E2;
    gdE->Se2 = Se2;
    gdE->saturation = saturation;
    gdE->Aex = Aex;
    gdE->Bex = Bex;
    return gdE;
}

void ExciterIEEEtype1::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    configureSaturation();
    offsets.local().local.diffSize = (Tr > 0.0) ? 4 : 3;
    offsets.local().local.jacSize = 18;
    checkForLimits();
}

// initial conditions
void ExciterIEEEtype1::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    Exciter::dynObjectInitializeB(inputs,
                                  desiredOutput,
                                  fieldSet);  // this will dynInitializeB the field state if need be
    double* stateValues = m_state.data();
    stateValues[1] = saturationFeedback(stateValues[0]);  // Vr
    stateValues[2] = (stateValues[0] * Kf) / Tf;  // Rf
    if (Tr > 0.0) {
        stateValues[3] = inputs[VOLTAGE_IN_LOCATION];
    }
    vBias = inputs[VOLTAGE_IN_LOCATION] + (stateValues[1] / Ka) - Vref;
    fieldSet[1] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

// residual
void ExciterIEEEtype1::residual(const IOdata& inputs,
                                const StateData& stateData,
                                double resid[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void ExciterIEEEtype1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;  // convert from a CoreTime
    m_state[0] += timeStep * m_dstate_dt[0];
    m_state[1] += timeStep * m_dstate_dt[1];
    m_state[2] += timeStep * m_dstate_dt[2];
    if (Tr > 0.0) {
        m_state[3] += timeStep * m_dstate_dt[3];
    }
    prevTime = time;
}

void ExciterIEEEtype1::derivative(const IOdata& inputs,
                                  const StateData& stateData,
                                  double deriv[],
                                  const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* derivatives = locations.destDiffLoc;
    const double voltage = measuredVoltage(inputs, state);
    derivatives[0] = (-saturationFeedback(state[0]) + state[1]) / Te;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        derivatives[1] = 0;
    } else {
        derivatives[1] =
            (-state[1] + (Ka * state[2]) - ((state[0] * Ka * Kf) / Tf) +
             (Ka * (Vref + vBias - voltage))) /
            Ta;
    }
    derivatives[2] = (-state[2] + ((state[0] * Kf) / Tf)) / Tf;
    if (Tr > 0.0) {
        derivatives[3] = (inputs[VOLTAGE_IN_LOCATION] - state[3]) / Tr;
    }
}

// Jacobian
void ExciterIEEEtype1::jacobianElements(const IOdata& /*inputs*/,
                                        const StateData& stateData,
                                        MatrixData<double>& matrixData,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto offset = offsets.getDiffOffset(sMode);

    matrixData.assign(offset,
                      offset,
                      (-(saturationDerivative(stateData.state[offset]) / Te)) - stateData.cj);
    matrixData.assign(offset, offset + 1, 1.0 / Te);
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        matrixData.assign(offset + 1, offset + 1, stateData.cj);
    } else {
        matrixData.assign(offset + 1, offset, (-Ka * Kf) / (Tf * Ta));
        matrixData.assign(offset + 1, offset + 1, (-1.0 / Ta) - stateData.cj);
        matrixData.assign(offset + 1, offset + 2, Ka / Ta);
        if (Tr > 0.0) {
            matrixData.assign(offset + 1, offset + 3, -Ka / Ta);
        } else {
            matrixData.assignCheckCol(offset + 1, inputLocs[VOLTAGE_IN_LOCATION], -Ka / Ta);
        }
    }

    // Rf
    matrixData.assign(offset + 2, offset, Kf / (Tf * Tf));
    matrixData.assign(offset + 2, offset + 2, (-1.0 / Tf) - stateData.cj);
    if (Tr > 0.0) {
        matrixData.assign(offset + 3, offset + 3, (-1.0 / Tr) - stateData.cj);
        matrixData.assignCheckCol(offset + 3, inputLocs[VOLTAGE_IN_LOCATION], 1.0 / Tr);
    }
}

void ExciterIEEEtype1::rootTest(const IOdata& inputs,
                                const StateData& stateData,
                                double roots[],
                                const SolverMode& sMode)
{
    const auto offset = offsets.getDiffOffset(sMode);
    const auto rootOffset = offsets.getRootOffset(sMode);
    const double* exciterState = stateData.state + offset;

    // printf("t=%f V=%f\n", time, inputs[VOLTAGE_IN_LOCATION]);

    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        roots[rootOffset] = exciterState[2] - ((exciterState[0] * Kf) / Tf) +
            (Vref + vBias - measuredVoltage(inputs, exciterState)) - (exciterState[1] / Ka) +
            ((0.001 * exciterState[1]) / (Ka * Ta));
    } else {
        roots[rootOffset] =
            std::min(regulatorUpperLimit() - exciterState[1], exciterState[1] - Vrmin) + 0.00001;
        if (exciterState[1] >= regulatorUpperLimit()) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

ChangeCode ExciterIEEEtype1::rootCheck(const IOdata& inputs,
                                       const StateData& /*sD*/,
                                       const SolverMode& /*sMode*/,
                                       CheckLevel /*level*/)
{
    const double* exciterState = m_state.data();
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double test = exciterState[2] - ((exciterState[0] * Kf) / Tf) +
            (Vref + vBias - measuredVoltage(inputs, exciterState)) - (exciterState[1] / Ka);

        if (opFlags[TRIGGER_HIGH]) {
            if (test < ((-0.001 * exciterState[1]) / (Ka * Ta))) {
                ret = ChangeCode::JACOBIAN_CHANGE;

                logging::debug(this, "root change V={}", inputs[VOLTAGE_IN_LOCATION]);
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                opFlags.reset(TRIGGER_HIGH);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > ((-0.001 * exciterState[1]) / (Ka * Ta))) {
                logging::debug(this, "root change V={}", inputs[VOLTAGE_IN_LOCATION]);
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (exciterState[1] > regulatorUpperLimit() + 0.00001) {
            logging::debug(this, "root toggle V={}", inputs[VOLTAGE_IN_LOCATION]);
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            m_state[1] = regulatorUpperLimit();
            m_dstate_dt[1] = 0.0;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (exciterState[1] < Vrmin - 0.00001) {
            logging::debug(this, "root toggle V={}", inputs[VOLTAGE_IN_LOCATION]);

            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            m_state[1] = Vrmin;
            m_dstate_dt[1] = 0.0;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

stringVec ExciterIEEEtype1::localStateNames() const
{
    return (Tr > 0.0) ? stringVec{"ef", "vr", "rf", "vmeas"} : stringVec{"ef", "vr", "rf"};
}
void ExciterIEEEtype1::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

// set parameters
void ExciterIEEEtype1::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "ke") {
        Ke = val;
    } else if (param == "te") {
        Te = val;
    } else if (param == "kf") {
        Kf = val;
    } else if (param == "tf") {
        Tf = val;
    } else if (param == "tr") {
        Tr = val;
    } else if (param == "e1") {
        E1 = val;
    } else if (param == "se1") {
        Se1 = val;
    } else if (param == "e2") {
        E2 = val;
    } else if (param == "se2") {
        Se2 = val;
    } else if (param == "aex") {
        Aex = val;
    } else if (param == "bex") {
        Bex = val;
    } else if (param == "limiter") {
        if (val > 0.1) {
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        } else if (val < -0.1) {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        } else {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
        }
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterIEEEtype1::get(std::string_view param, units::unit unitType) const
{
    if (param == "tr") {
        return Tr;
    }
    if (param == "e1") {
        return E1;
    }
    if (param == "se1") {
        return Se1;
    }
    if (param == "e2") {
        return E2;
    }
    if (param == "se2") {
        return Se2;
    }
    return Exciter::get(param, unitType);
}

void ExciterIEEEtype1::configureSaturation()
{
    if (Se2 == 0.0) {
        saturation.setType(utilities::Saturation::SaturationType::NONE);
        return;
    }
    if ((E1 < 0.0) || (E2 <= 0.0) || (Se1 < 0.0) || (Se2 < 0.0)) {
        throw InvalidParameterValue("IEEE Type 1 saturation points");
    }
    saturation.setType(utilities::Saturation::SaturationType::CUTOFF_QUADRATIC);
    saturation.setParam(E1, E1 * Se1, E2, E2 * Se2);
}

double ExciterIEEEtype1::saturationFeedback(double fieldVoltage) const
{
    return (Ke * fieldVoltage) + saturation(fieldVoltage);
}

double ExciterIEEEtype1::saturationDerivative(double fieldVoltage) const
{
    return Ke + saturation.deriv(fieldVoltage);
}

double ExciterIEEEtype1::regulatorUpperLimit() const
{
    return (Vrmax == 0.0) ? 999.0 : Vrmax;
}

double ExciterIEEEtype1::measuredVoltage(const IOdata& inputs, const double state[]) const
{
    return (Tr > 0.0) ? state[3] : inputs[VOLTAGE_IN_LOCATION];
}

}  // namespace griddyn::exciters
