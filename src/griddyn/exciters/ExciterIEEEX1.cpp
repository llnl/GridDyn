/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterIEEEX1.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>

namespace griddyn::exciters {
ExciterIEEEX1::ExciterIEEEX1(const std::string& objName): ExciterDC2A(objName) {}

CoreObject* ExciterIEEEX1::clone(CoreObject* obj) const
{
    auto* cloneObject = cloneBase<ExciterIEEEX1, ExciterDC2A>(this, obj);
    if (cloneObject == nullptr) {
        return obj;
    }
    return cloneObject;
}

bool ExciterIEEEX1::hasLeadLag() const
{
    return Tb > 0.0;
}

double ExciterIEEEX1::directMeasuredVoltage(const IOdata& inputs, const double state[]) const
{
    return (Tr > 0.0) ? state[3] : inputs[VOLTAGE_IN_LOCATION];
}

double ExciterIEEEX1::regulatorDrive(const IOdata& inputs, const double state[]) const
{
    const double measuredVoltage = hasLeadLag() ? ExciterDC1A::measuredVoltage(inputs, state) :
                                                  directMeasuredVoltage(inputs, state);
    const index_t feedbackState = hasLeadLag() ? 3 : 2;
    const double voltageError =
        Vref + vBias - measuredVoltage - (state[0] * Kf / Tf) + state[feedbackState];
    const double leadLagOutput = hasLeadLag() ?
        ((Tc * voltageError) + ((Tb - Tc) * state[2])) / Tb :
        voltageError;
    return (Ka * leadLagOutput) - state[1];
}

void ExciterIEEEX1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if ((Ka == 0.0) || (Ta <= 0.0) || (Te <= 0.0) || (Tf <= 0.0) || (Tr < 0.0) ||
        (Tb < 0.0) || (Tc < 0.0) || (Vrmin > Vrmax)) {
        throw InvalidParameterValue("IEEEX1 gains, limits, or time constants");
    }
    if (hasLeadLag()) {
        ExciterDC2A::dynObjectInitializeA(time0, flags);
        return;
    }
    configureSaturation();
    offsets.local().local.diffSize = (Tr > 0.0) ? 4 : 3;
    offsets.local().local.jacSize = 18;
    checkForLimits();
}

void ExciterIEEEX1::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (hasLeadLag()) {
        ExciterDC2A::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        return;
    }
    ExciterIEEEtype1::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
}

void ExciterIEEEX1::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    if (hasLeadLag()) {
        ExciterDC2A::residual(inputs, stateData, resid, sMode);
        return;
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void ExciterIEEEX1::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (hasLeadLag()) {
        ExciterDC2A::derivative(inputs, stateData, deriv, sMode);
        return;
    }
    auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* derivatives = locations.destDiffLoc;
    derivatives[0] = (-saturationFeedback(state[0]) + state[1]) / Te;
    derivatives[1] =
        opFlags[OUTSIDE_VOLTAGE_LIMITS] ? 0.0 : regulatorDrive(inputs, state) / Ta;
    derivatives[2] = (-state[2] + ((state[0] * Kf) / Tf)) / Tf;
    if (Tr > 0.0) {
        derivatives[3] = (inputs[VOLTAGE_IN_LOCATION] - state[3]) / Tr;
    }
}

void ExciterIEEEX1::timestep(CoreTime time,
                             const IOdata& inputs,
                             const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    index_t stateCount = hasLeadLag() ? 4 : 3;
    if (Tr > 0.0) {
        ++stateCount;
    }
    for (index_t state = 0; state < stateCount; ++state) {
        m_state[state] += timeStep * m_dstate_dt[state];
    }
    prevTime = time;
}

void ExciterIEEEX1::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    if (hasLeadLag()) {
        ExciterDC2A::jacobianElements(inputs, stateData, matrixData, inputLocs, sMode);
        return;
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const auto offset = offsets.getDiffOffset(sMode);
    const auto voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    matrixData.assign(offset,
                      offset,
                      (-saturationDerivative(state[0]) / Te) - stateData.cj);
    matrixData.assign(offset, offset + 1, 1.0 / Te);
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        matrixData.assign(offset + 1, offset + 1, -stateData.cj);
    } else {
        matrixData.assign(offset + 1, offset, (-Ka * Kf) / (Tf * Ta));
        matrixData.assign(offset + 1, offset + 1, (-1.0 / Ta) - stateData.cj);
        matrixData.assign(offset + 1, offset + 2, Ka / Ta);
        if (Tr > 0.0) {
            matrixData.assign(offset + 1, offset + 3, -Ka / Ta);
        } else {
            matrixData.assignCheckCol(offset + 1, voltageLoc, -Ka / Ta);
        }
    }
    matrixData.assign(offset + 2, offset, Kf / (Tf * Tf));
    matrixData.assign(offset + 2, offset + 2, (-1.0 / Tf) - stateData.cj);
    if (Tr > 0.0) {
        matrixData.assign(offset + 3, offset + 3, (-1.0 / Tr) - stateData.cj);
        matrixData.assignCheckCol(offset + 3, voltageLoc, 1.0 / Tr);
    }
}

void ExciterIEEEX1::limitJacobian(double /*voltage*/,
                                  int /*voltageLoc*/,
                                  int refLoc,
                                  double cj,
                                  MatrixData<double>& matrixData)
{
    // At either anti-windup bound the regulator differential equation is
    // replaced by -dot(V_R)=0.  The terminal-voltage dependence belongs to
    // limit detection and state projection, not to this frozen-state residual.
    matrixData.assign(refLoc, refLoc, -cj);
}

void ExciterIEEEX1::rootTest(const IOdata& inputs,
                              const StateData& stateData,
                              double roots[],
                              const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto rootOffset = offsets.getRootOffset(sMode);
    const double* state = locations.diffStateLoc;
    const double terminalVoltage = inputs[VOLTAGE_IN_LOCATION];
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        roots[rootOffset] = regulatorDrive(inputs, state);
    } else {
        roots[rootOffset] = std::min((regulatorUpperLimit() * terminalVoltage) - state[1],
                                     state[1] - (Vrmin * terminalVoltage)) +
            0.00001;
        if (state[1] >= (regulatorUpperLimit() * terminalVoltage)) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

ChangeCode ExciterIEEEX1::rootCheck(const IOdata& inputs,
                                     const StateData& stateData,
                                     const SolverMode& sMode,
                                     CheckLevel level)
{
    (void)stateData;
    (void)sMode;
    (void)level;
    double* state = m_state.data();
    const double terminalVoltage = inputs[VOLTAGE_IN_LOCATION];
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double drive = regulatorDrive(inputs, state);
        if ((opFlags[TRIGGER_HIGH] && (drive < 0.0)) ||
            (!opFlags[TRIGGER_HIGH] && (drive > 0.0))) {
            opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
            opFlags.reset(TRIGGER_HIGH);
            alert(this, JAC_COUNT_INCREASE);
            return ChangeCode::JACOBIAN_CHANGE;
        }
        return ChangeCode::NO_CHANGE;
    }
    if (state[1] > ((regulatorUpperLimit() * terminalVoltage) + 0.00001)) {
        opFlags.set(TRIGGER_HIGH);
        opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        state[1] = regulatorUpperLimit() * terminalVoltage;
        m_dstate_dt[1] = 0.0;
        alert(this, JAC_COUNT_DECREASE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    if (state[1] < ((Vrmin * terminalVoltage) - 0.00001)) {
        opFlags.reset(TRIGGER_HIGH);
        opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        state[1] = Vrmin * terminalVoltage;
        m_dstate_dt[1] = 0.0;
        alert(this, JAC_COUNT_DECREASE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

double ExciterIEEEX1::get(std::string_view param, units::unit unitType) const
{
    if (param == "ka") {
        return Ka;
    }
    if (param == "ta") {
        return Ta;
    }
    if (param == "tb") {
        return Tb;
    }
    if (param == "tc") {
        return Tc;
    }
    if ((param == "vrmax") || (param == "urmax")) {
        return Vrmax;
    }
    if ((param == "vrmin") || (param == "urmin")) {
        return Vrmin;
    }
    if (param == "ke") {
        return Ke;
    }
    if (param == "te") {
        return Te;
    }
    if (param == "kf") {
        return Kf;
    }
    if (param == "tf") {
        return Tf;
    }
    if (param == "switch") {
        return 0.0;
    }
    return ExciterDC2A::get(param, unitType);
}

stringVec ExciterIEEEX1::localStateNames() const
{
    return hasLeadLag() ? ExciterDC2A::localStateNames() : ExciterIEEEtype1::localStateNames();
}

}  // namespace griddyn::exciters
