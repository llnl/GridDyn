/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelCSVGN1.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::genmodels {
GenModelCSVGN1::GenModelCSVGN1(const std::string& objName): GenModel(objName) {}

CoreObject* GenModelCSVGN1::clone(CoreObject* obj) const
{
    auto* modelClone = cloneBase<GenModelCSVGN1, GenModel>(this, obj);
    if (modelClone == nullptr) {
        return obj;
    }
    modelClone->K = K;
    modelClone->T1 = T1;
    modelClone->T2 = T2;
    modelClone->T3 = T3;
    modelClone->T4 = T4;
    modelClone->T5 = T5;
    modelClone->RMIN = RMIN;
    modelClone->VMAX = VMAX;
    modelClone->VMIN = VMIN;
    modelClone->CBASE = CBASE;
    modelClone->m_voltageReference = m_voltageReference;
    return modelClone;
}

void GenModelCSVGN1::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(K) || (K <= 0.0) || !std::isfinite(T1) || (T1 < 0.0) ||
        !std::isfinite(T2) || (T2 < 0.0) || !std::isfinite(T3) || (T3 <= 0.0) ||
        !std::isfinite(T4) || (T4 <= 0.0) || !std::isfinite(T5) || (T5 <= 0.0) ||
        !std::isfinite(VMIN) || !std::isfinite(VMAX) || (VMIN > VMAX) ||
        !std::isfinite(RMIN) || (RMIN < 0.0) || !std::isfinite(CBASE) || (CBASE < 0.0) ||
        !std::isfinite(machineBasePower) || (machineBasePower <= 0.0) ||
        (RMIN > machineBasePower)) {
        throw InvalidParameterValue("CSVGN1 parameters or machine MBASE");
    }

    // This first realization uses one differential state for each documented
    // lag. Zero denominator time constants need an algebraic-state realization
    // and are rejected rather than approximated by a fast pole.
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 9;
}

void GenModelCSVGN1::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    GenModel::dynObjectInitializeB(inputs, desiredOutput, fieldSet);

    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    if (!std::isfinite(voltage) || (voltage <= 0.0) || (desiredOutput.size() <= QOUT_LOCATION)) {
        throw InvalidParameterValue("CSVGN1 initial voltage or reactive output");
    }

    const double reactorCommand =
        (CBASE / machineBasePower) - (desiredOutput[QOUT_LOCATION] / (voltage * voltage));
    const double reactorMin = reactorMinimumPU();
    constexpr double initTolerance = 1.0e-6;
    if ((reactorCommand < reactorMin - initTolerance) ||
        (reactorCommand > 1.0 + initTolerance) || (reactorCommand < VMIN - initTolerance) ||
        (reactorCommand > VMAX + initTolerance)) {
        throw InvalidParameterValue("CSVGN1 power-flow Q cannot be initialized within its limits");
    }

    const double initializedReactor = std::clamp(reactorCommand, reactorMin, 1.0);
    const double error = initializedReactor / K;
    m_voltageReference = voltage - error;

    // At steady state each lead-lag section has unity DC gain. Initializing
    // both regulator states to the input error and the thyristor state to the
    // power-flow reactor command makes all three state derivatives zero.
    m_state[0] = error;
    m_state[1] = error;
    m_state[2] = initializedReactor;

    // CSVGN1 has no mechanical-power or exciter-field input. It injects only
    // the shunt reactive power represented by its machine record.
    fieldSet[genModelPmechInLocation] = 0.0;
    fieldSet[genModelEftInLocation] = 0.0;
}

void GenModelCSVGN1::set(std::string_view param, std::string_view val)
{
    GenModel::set(param, val);
}

void GenModelCSVGN1::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "k") {
        K = val;
    } else if (param == "t1") {
        T1 = val;
    } else if (param == "t2") {
        T2 = val;
    } else if (param == "t3") {
        T3 = val;
    } else if (param == "t4") {
        T4 = val;
    } else if (param == "t5") {
        T5 = val;
    } else if (param == "rmin") {
        RMIN = val;
    } else if (param == "vmax") {
        VMAX = val;
    } else if (param == "vmin") {
        VMIN = val;
    } else if (param == "cbase") {
        CBASE = val;
    } else {
        GenModel::set(param, val, unitType);
    }
}

double GenModelCSVGN1::get(std::string_view param, units::unit unitType) const
{
    if (param == "k") {
        return K;
    }
    if (param == "t1") {
        return T1;
    }
    if (param == "t2") {
        return T2;
    }
    if (param == "t3") {
        return T3;
    }
    if (param == "t4") {
        return T4;
    }
    if (param == "t5") {
        return T5;
    }
    if (param == "rmin") {
        return RMIN;
    }
    if (param == "vmax") {
        return VMAX;
    }
    if (param == "vmin") {
        return VMIN;
    }
    if (param == "cbase") {
        return CBASE;
    }
    return GenModel::get(param, unitType);
}

stringVec GenModelCSVGN1::localStateNames() const
{
    return {"regulator1", "regulator2", "thyristor"};
}

double GenModelCSVGN1::voltageReference() const
{
    return m_voltageReference;
}

double GenModelCSVGN1::reactorMinimumPU() const
{
    return RMIN / machineBasePower;
}

double GenModelCSVGN1::reactiveOutput(double voltage, double reactorCommand) const
{
    const double reactor = std::clamp(reactorCommand, reactorMinimumPU(), 1.0);
    return ((CBASE / machineBasePower) - reactor) * voltage * voltage;
}

GenModelCSVGN1::RegulatorSignals GenModelCSVGN1::getRegulatorSignals(double voltage,
                                                                     const double state[]) const
{
    RegulatorSignals signals;
    signals.error = voltage - voltageReference();
    signals.firstOutput = (T1 / T3) * signals.error + (1.0 - (T1 / T3)) * state[0];
    signals.secondOutput = (T2 / T4) * signals.firstOutput + (1.0 - (T2 / T4)) * state[1];
    const double rawOutput = K * signals.secondOutput;
    signals.limitedOutput = std::clamp(rawOutput, VMIN, VMAX);
    signals.limitedOutputGain =
        ((rawOutput > VMIN) && (rawOutput < VMAX)) ? K : 0.0;
    return signals;
}

void GenModelCSVGN1::derivative(const IOdata& inputs,
                                const StateData& stateDataValue,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateDataValue, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* stateDerivative = locations.destDiffLoc;
    const auto signals = getRegulatorSignals(inputs[VOLTAGE_IN_LOCATION], state);

    stateDerivative[0] = (signals.error - state[0]) / T3;
    stateDerivative[1] = (signals.firstOutput - state[1]) / T4;

    const double reactorMin = reactorMinimumPU();
    const double reactorCommand = state[2];
    const double thyristorRate = (signals.limitedOutput - reactorCommand) / T5;
    const bool blockedAtLowerLimit =
        (state[2] <= reactorMin) && (thyristorRate < 0.0);
    const bool blockedAtUpperLimit = (state[2] >= 1.0) && (thyristorRate > 0.0);
    stateDerivative[2] = (blockedAtLowerLimit || blockedAtUpperLimit) ?
        0.0 :
        thyristorRate;
}

void GenModelCSVGN1::residual(const IOdata& inputs,
                              const StateData& stateDataValue,
                              double resid[],
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateDataValue, resid, sMode);
    const auto locations = offsets.getLocations(stateDataValue, resid, sMode, this);
    for (index_t ii = 0; ii < 3; ++ii) {
        locations.destDiffLoc[ii] -= locations.dstateLoc[ii];
    }
}

IOdata GenModelCSVGN1::getOutputs(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    IOdata outputs(2, 0.0);
    // GenModel outputs use the network load convention (negative for machine
    // injection), while reactiveOutput is positive for capacitive injection.
    outputs[QOUT_LOCATION] =
        -reactiveOutput(inputs[VOLTAGE_IN_LOCATION], locations.diffStateLoc[2]);
    return outputs;
}

double GenModelCSVGN1::getOutput(const IOdata& inputs,
                                const StateData& stateDataValue,
                                const SolverMode& sMode,
                                index_t outNum) const
{
    if (outNum == POUT_LOCATION) {
        return 0.0;
    }
    if (outNum != QOUT_LOCATION) {
        return kNullVal;
    }
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    return -reactiveOutput(inputs[VOLTAGE_IN_LOCATION], locations.diffStateLoc[2]);
}

double GenModelCSVGN1::getOutput(index_t outNum) const
{
    if (outNum == POUT_LOCATION) {
        return 0.0;
    }
    if ((outNum != QOUT_LOCATION) || (bus == nullptr) || (m_state.size() < 3U)) {
        return kNullVal;
    }
    return -reactiveOutput(bus->getVoltage(), m_state[2]);
}

void GenModelCSVGN1::jacobianElements(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double* state = locations.diffStateLoc;
    const auto signals = getRegulatorSignals(inputs[VOLTAGE_IN_LOCATION], state);
    const index_t row = locations.diffOffset;
    const index_t col = locations.diffOffset;

    const double firstOutputStateGain = 1.0 - (T1 / T3);
    const double firstOutputInputGain = T1 / T3;
    const double secondOutputFirstGain = T2 / T4;
    const double secondOutputStateGain = 1.0 - (T2 / T4);

    matrixDataValue.assign(row, col, (-1.0 / T3) - stateDataValue.cj);
    matrixDataValue.assignCheckCol(row,
                                   inputLocs[VOLTAGE_IN_LOCATION],
                                   1.0 / T3);

    matrixDataValue.assign(row + 1, col, firstOutputStateGain / T4);
    matrixDataValue.assign(row + 1, col + 1, (-1.0 / T4) - stateDataValue.cj);
    matrixDataValue.assignCheckCol(row + 1,
                                   inputLocs[VOLTAGE_IN_LOCATION],
                                   firstOutputInputGain / T4);

    const double reactorMin = reactorMinimumPU();
    const double thyristorRate = (signals.limitedOutput - state[2]) / T5;
    const bool blockedAtLowerLimit =
        (state[2] <= reactorMin) && (thyristorRate < 0.0);
    const bool blockedAtUpperLimit = (state[2] >= 1.0) && (thyristorRate > 0.0);
    if (blockedAtLowerLimit || blockedAtUpperLimit) {
        matrixDataValue.assign(row + 2, col + 2, -stateDataValue.cj);
        return;
    }

    const double thirdRowScale = signals.limitedOutputGain / T5;
    matrixDataValue.assign(row + 2,
                           col,
                           thirdRowScale * secondOutputFirstGain * firstOutputStateGain);
    matrixDataValue.assign(row + 2, col + 1, thirdRowScale * secondOutputStateGain);
    matrixDataValue.assign(row + 2, col + 2, (-1.0 / T5) - stateDataValue.cj);
    matrixDataValue.assignCheckCol(row + 2,
                                   inputLocs[VOLTAGE_IN_LOCATION],
                                   thirdRowScale * secondOutputFirstGain * firstOutputInputGain);
}

void GenModelCSVGN1::outputPartialDerivatives(const IOdata& inputs,
                                             const StateData& stateDataValue,
                                             MatrixData<double>& matrixDataValue,
                                             const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double reactor = locations.diffStateLoc[2];
    if ((reactor >= reactorMinimumPU()) && (reactor <= 1.0)) {
        const double voltage = inputs[VOLTAGE_IN_LOCATION];
        matrixDataValue.assign(QOUT_LOCATION,
                               locations.diffOffset + 2,
                               voltage * voltage);
    }
}

count_t GenModelCSVGN1::outputDependencyCount(index_t outNum, const SolverMode& /*sMode*/) const
{
    return (outNum == QOUT_LOCATION) ? 1 : 0;
}

void GenModelCSVGN1::ioPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const IOlocs& inputLocs,
                                          const SolverMode& sMode)
{
    if (inputLocs[VOLTAGE_IN_LOCATION] == kNullLocation) {
        return;
    }
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double reactor = std::clamp(locations.diffStateLoc[2], reactorMinimumPU(), 1.0);
    const double shuntSusceptance = (CBASE / machineBasePower) - reactor;
    matrixDataValue.assign(QOUT_LOCATION,
                           inputLocs[VOLTAGE_IN_LOCATION],
                           -2.0 * shuntSusceptance * voltage);
}
}  // namespace griddyn::genmodels
