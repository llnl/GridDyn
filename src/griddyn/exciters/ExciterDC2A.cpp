/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterDC2A.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

// only differences from dc1a are gains and voltage limit is a function of
// terminal voltage

namespace griddyn::exciters {
namespace {
    // Keep this distinct from Exciter::limiterRootTolerance, the generic 2e-6 threshold.
    constexpr double dc2aLimiterRootTolerance = 0.0001;
}  // namespace

const ModelDescription& ExciterDC2A::getModelDescription() const
{
    static const auto description =
        ModelDescription::define<ExciterDC2A>("ExciterDC2A",
                                              &ExciterDC1A::getModelDescription(),
                                              {});
    return description;
}

ExciterDC2A::ExciterDC2A(const std::string& objName): ExciterDC1A(objName)
{
    // default values
    Ka = 300;
    Ta = 0.01;
    Ke = 1.0;
    Te = 1.33;
    Kf = .1;
    Tf = 0.675;
    Tc = 0;
    Tb = 1.0;  // can't be zero
    // PSS/E-form saturation points derived from the historic GridDyn
    // exponential fit's calibration points.
    E1 = 2.29;
    Se1 = 0.117;
    E2 = 3.05;
    Se2 = 0.279;
    Vrmin = -4.9;
    Vrmax = 4.95;
    offsets.local().local.jacSize = 15;
}

// cloning function
CoreObject* ExciterDC2A::clone(CoreObject* obj) const
{
    auto* gdE = cloneBase<ExciterDC2A, ExciterDC1A>(this, obj);
    if (gdE == nullptr) {
        return obj;
    }
    return gdE;
}

bool ExciterDC2A::adjustInitialRegulatorLimit(double initialValue, double terminalVoltage)
{
    if (!std::isfinite(terminalVoltage) || (terminalVoltage <= 0.0) ||
        (initialValue < (terminalVoltage * Vrmin) - 1e-7)) {
        return false;
    }
    return adjustInitialUpperLimit(initialValue / terminalVoltage,
                                   Vrmax,
                                   "DC2A initial regulator output");
}

double ExciterDC2A::initialRegulatorUpperBound(double terminalVoltage) const
{
    return terminalVoltage * regulatorUpperLimit();
}

double ExciterDC2A::initialRegulatorLowerBound(double terminalVoltage) const
{
    return terminalVoltage * Vrmin;
}

double ExciterDC2A::initialRegulatorOverdrive(double originalUpperLimit,
                                              double terminalVoltage) const
{
    const double correction = std::max(0.0, Vrmax - originalUpperLimit) * terminalVoltage;
    const double leadLagGain = std::max(1.0, std::abs(Tc / Tb));
    const double guard =
        10.0 * dc2aLimiterRootTolerance + (std::abs(Ka) * leadLagGain + std::abs(Vrmax)) * 1e-5;
    return std::max(0.2 * correction, guard);
}

// residual
void ExciterDC2A::residual(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double resid[],
                           const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    ExciterDC1A::residual(inputs, stateDataValue, resid, sMode);
}

void ExciterDC2A::derivative(const IOdata& inputs,
                             const StateData& stateDataValue,
                             double deriv[],
                             const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    ExciterDC1A::derivative(inputs,
                            stateDataValue,
                            deriv,
                            sMode);  // use DC1A but overwrite if we are at a limiter
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const auto offset = offsets.getDiffOffset(sMode);
        const double* state = stateDataValue.state ? stateDataValue.state + offset : m_state.data();
        const double limit = opFlags[TRIGGER_HIGH] ? regulatorUpperLimit() : Vrmin;
        // The antiwindup regulator state is held, but the limited output
        // follows the terminal-voltage-dependent bound in the field equation.
        deriv[offset] = (-saturationFeedback(state[0]) + limit * inputs[VOLTAGE_IN_LOCATION]) / Te;
    }
}

void ExciterDC2A::fieldRegulatorJacobian(MatrixData<double>& matrixDataValue,
                                         index_t fieldRow,
                                         index_t regulatorCol,
                                         index_t voltageCol) const
{
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double limit = opFlags[TRIGGER_HIGH] ? regulatorUpperLimit() : Vrmin;
        matrixDataValue.assignCheckCol(fieldRow, voltageCol, limit / Te);
    } else {
        ExciterDC1A::fieldRegulatorJacobian(matrixDataValue, fieldRow, regulatorCol, voltageCol);
    }
}

void ExciterDC2A::limitJacobian(double /*V*/,
                                int /*voltageLoc*/,
                                int refLoc,
                                double cjValue,
                                MatrixData<double>& matrixDataValue)
{
    // The frozen antiwindup state still has residual -dot(V_R). The moving
    // limited output contributes a voltage term to the field row instead.
    matrixDataValue.assign(refLoc, refLoc, -cjValue);
}

void ExciterDC2A::rootTest(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double roots[],
                           const SolverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    const int rootOffset = offsets.getRootOffset(sMode);
    const double* exciterState = stateDataValue.state + offset;
    const double measured = measuredVoltage(inputs, exciterState);
    const double terminalVoltage = inputs[VOLTAGE_IN_LOCATION];
    const double drive =
        ((((Vref + vBias - measured) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka * Tc /
         Tb) +
        ((exciterState[2] * (Tb - Tc) * Ka) / Tb) -
        (opFlags[OUTSIDE_VOLTAGE_LIMITS] ?
             terminalVoltage * (opFlags[TRIGGER_HIGH] ? regulatorUpperLimit() : Vrmin) :
             exciterState[1]);
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        // rootCheck() releases a high limiter when the drive is negative and
        // a low limiter when it is positive. Keep a zero drive inside the
        // held branch so it is not reported repeatedly as a root.
        roots[rootOffset] = opFlags[TRIGGER_HIGH] ? drive + dc2aLimiterRootTolerance :
                                                    drive - dc2aLimiterRootTolerance;
    } else {
        roots[rootOffset] = std::min((regulatorUpperLimit() * terminalVoltage) - exciterState[1],
                                     exciterState[1] - (Vrmin * terminalVoltage)) +
            dc2aLimiterRootTolerance;
        if (exciterState[1] > (terminalVoltage * regulatorUpperLimit())) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

ChangeCode ExciterDC2A::rootCheck(const IOdata& inputs,
                                  const StateData& /*sD*/,
                                  const SolverMode& /*sMode*/,
                                  CheckLevel /*level*/)
{
    double* exciterState = m_state.data();
    const double measured = measuredVoltage(inputs, exciterState);
    const double terminalVoltage = inputs[VOLTAGE_IN_LOCATION];
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double test =
            ((((Vref + vBias - measured) - ((exciterState[0] * Kf) / Tf)) + exciterState[3]) * Ka *
             Tc / Tb) +
            ((exciterState[2] * (Tb - Tc) * Ka) / Tb) -
            terminalVoltage * (opFlags[TRIGGER_HIGH] ? regulatorUpperLimit() : Vrmin);
        if (opFlags[TRIGGER_HIGH]) {
            if (test < 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                exciterState[1] = terminalVoltage * regulatorUpperLimit();
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                opFlags.reset(TRIGGER_HIGH);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                exciterState[1] = terminalVoltage * Vrmin;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (exciterState[1] > ((terminalVoltage * regulatorUpperLimit()) + 0.00005)) {
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            exciterState[1] = terminalVoltage * regulatorUpperLimit();
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (exciterState[1] < ((terminalVoltage * Vrmin) - 0.00005)) {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            exciterState[1] = terminalVoltage * Vrmin;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

}  // namespace griddyn::exciters
