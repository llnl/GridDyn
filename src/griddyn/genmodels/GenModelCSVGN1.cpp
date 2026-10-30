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
namespace {
    double clampSlope(double value, double lower, double upper)
    {
        // Right-sided derivative, including at the lower limit.
        return (value >= lower && value < upper) ? 1.0 : 0.0;
    }

    bool outwardRate(double state, double rate, double lower, double upper)
    {
        return (state <= lower && rate < 0.0) || (state >= upper && rate > 0.0);
    }
}  // namespace

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
    if (!std::isfinite(K) || (K <= 0.0) || !std::isfinite(T1) || (T1 < 0.0) || !std::isfinite(T2) ||
        (T2 < 0.0) || !std::isfinite(T3) || (T3 <= 0.0) || !std::isfinite(T4) || (T4 < 0.0) ||
        (T2 > 0.0 && T4 == 0.0) || !std::isfinite(T5) || (T5 < 0.0) || !std::isfinite(VMIN) ||
        !std::isfinite(VMAX) || (VMIN > VMAX) || !std::isfinite(RMIN) || (RMIN < 0.0) ||
        !std::isfinite(CBASE) || (CBASE < 0.0) || !std::isfinite(machineBasePower) ||
        (machineBasePower <= 0.0) || (RMIN > machineBasePower)) {
        throw InvalidParameterValue("CSVGN1 parameters or machine MBASE");
    }

    // Keep bypassed blocks as algebraic equations instead of introducing fast poles.
    const count_t algebraicCount = (T4 == 0.0 ? 1 : 0) + (T5 == 0.0 ? 1 : 0);
    offsets.local().local.algSize = algebraicCount;
    offsets.local().local.diffSize = 3 - algebraicCount;
    offsets.local().local.jacSize = 11;
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
    if ((reactorCommand < reactorMin - initTolerance) || (reactorCommand > 1.0 + initTolerance) ||
        (reactorCommand < VMIN - initTolerance) || (reactorCommand > VMAX + initTolerance)) {
        throw InvalidParameterValue("CSVGN1 power-flow Q cannot be initialized within its limits");
    }

    const double initializedReactor = std::clamp(reactorCommand, reactorMin, 1.0);
    const double error = initializedReactor / K;
    m_voltageReference = voltage - error;

    // The first state is before K; the limited regulator's state and the
    // reactor command are after K, including for algebraic bypasses.
    const auto stateLocs = stateLocations(0, offsets.local().local.algSize);
    m_state[stateLocs[0]] = error;
    m_state[stateLocs[1]] = initializedReactor;
    m_state[stateLocs[2]] = initializedReactor;

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
    const auto stateLocs = stateLocations(0, (T4 == 0.0 ? 1 : 0) + (T5 == 0.0 ? 1 : 0));
    stringVec names(3);
    names[stateLocs[0]] = "regulator1";
    names[stateLocs[1]] = "regulator2";
    names[stateLocs[2]] = "thyristor";
    return names;
}

std::array<double, 3> GenModelCSVGN1::stateValues(const double alg[], const double diff[]) const
{
    return {diff[0],
            T4 == 0.0 ? alg[0] : diff[1],
            T5 == 0.0 ? alg[T4 == 0.0 ? 1 : 0] : diff[T4 == 0.0 ? 1 : 2]};
}

std::array<index_t, 3> GenModelCSVGN1::stateLocations(index_t algOffset, index_t diffOffset) const
{
    auto location = [](index_t base, index_t index) {
        return base == kNullLocation ? kNullLocation : base + index;
    };
    return {diffOffset,
            T4 == 0.0 ? algOffset : location(diffOffset, 1),
            T5 == 0.0 ? location(algOffset, T4 == 0.0 ? 1 : 0) :
                        location(diffOffset, T4 == 0.0 ? 1 : 2)};
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

GenModelCSVGN1::RegulatorSignals
    GenModelCSVGN1::getRegulatorSignals(double voltage, const std::array<double, 3>& state) const
{
    RegulatorSignals signals;
    signals.error = voltage - voltageReference();
    const double firstInputGain = T1 / T3;
    const double firstStateGain = 1.0 - firstInputGain;
    signals.firstOutput = (firstInputGain * signals.error) + (firstStateGain * state[0]);
    const double leadGain = T4 == 0.0 ? 1.0 : T2 / T4;
    const double stateGain = T4 == 0.0 ? 0.0 : 1.0 - leadGain;
    const double rawOutput = (K * leadGain * signals.firstOutput) + (stateGain * state[1]);
    signals.limitedOutput = std::clamp(rawOutput, VMIN, VMAX);
    const double slope = clampSlope(rawOutput, VMIN, VMAX);
    signals.outputDerivatives = {slope * K * leadGain * firstStateGain,
                                 slope * stateGain,
                                 slope * K * leadGain * firstInputGain};
    if (T2 > 0.0) {
        // Limited lead-lag: feed the limited output back to the internal lag,
        // as OpenIPSL's LeadLagLim does, so saturation does not wind up the state.
        signals.rate = (signals.limitedOutput - state[1]) / T2;
        signals.rateDerivatives = {signals.outputDerivatives[0] / T2,
                                   (signals.outputDerivatives[1] - 1.0) / T2,
                                   signals.outputDerivatives[2] / T2};
    } else if (T4 > 0.0) {
        const double rate = ((K * signals.firstOutput) - state[1]) / T4;
        if (!outwardRate(state[1], rate, VMIN, VMAX)) {
            signals.rate = rate;
            signals.rateDerivatives = {K * firstStateGain / T4, -1.0 / T4, K * firstInputGain / T4};
        }
    }
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
    const auto state = stateValues(locations.algStateLoc, locations.diffStateLoc);
    double* stateDerivative = locations.destDiffLoc;
    const auto signals = getRegulatorSignals(inputs[VOLTAGE_IN_LOCATION], state);

    stateDerivative[0] = (signals.error - state[0]) / T3;
    if (T4 > 0.0) {
        stateDerivative[1] = signals.rate;
    }
    if (T5 > 0.0) {
        const double rate = (signals.limitedOutput - state[2]) / T5;
        stateDerivative[T4 == 0.0 ? 1 : 2] =
            outwardRate(state[2], rate, reactorMinimumPU(), 1.0) ? 0.0 : rate;
    }
}

void GenModelCSVGN1::residual(const IOdata& inputs,
                              const StateData& stateDataValue,
                              double resid[],
                              const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateDataValue, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto state = stateValues(locations.algStateLoc, locations.diffStateLoc);
        const auto signals = getRegulatorSignals(inputs[VOLTAGE_IN_LOCATION], state);
        if (T4 == 0.0) {
            locations.destLoc[0] = signals.limitedOutput - state[1];
        }
        if (T5 == 0.0) {
            locations.destLoc[T4 == 0.0 ? 1 : 0] =
                std::clamp(signals.limitedOutput, reactorMinimumPU(), 1.0) - state[2];
        }
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateDataValue, resid, sMode);
        for (index_t ii = 0; ii < locations.diffSize; ++ii) {
            locations.destDiffLoc[ii] -= locations.dstateLoc[ii];
        }
    }
}

void GenModelCSVGN1::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateDataValue, update, sMode, this);
    const auto state = stateValues(locations.algStateLoc, locations.diffStateLoc);
    const auto signals = getRegulatorSignals(inputs[VOLTAGE_IN_LOCATION], state);
    if (T4 == 0.0) {
        locations.destLoc[0] = signals.limitedOutput;
    }
    if (T5 == 0.0) {
        locations.destLoc[T4 == 0.0 ? 1 : 0] =
            std::clamp(signals.limitedOutput, reactorMinimumPU(), 1.0);
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
        -reactiveOutput(inputs[VOLTAGE_IN_LOCATION],
                        stateValues(locations.algStateLoc, locations.diffStateLoc)[2]);
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
    return -reactiveOutput(inputs[VOLTAGE_IN_LOCATION],
                           stateValues(locations.algStateLoc, locations.diffStateLoc)[2]);
}

double GenModelCSVGN1::getOutput(index_t outNum) const
{
    if (outNum == POUT_LOCATION) {
        return 0.0;
    }
    if ((outNum != QOUT_LOCATION) || (bus == nullptr) || (m_state.size() < 3U)) {
        return kNullVal;
    }
    const auto stateLocs = stateLocations(0, offsets.local().local.algSize);
    return -reactiveOutput(bus->getVoltage(), m_state[stateLocs[2]]);
}

void GenModelCSVGN1::jacobianElements(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const auto state = stateValues(locations.algStateLoc, locations.diffStateLoc);
    const auto signals = getRegulatorSignals(inputs[VOLTAGE_IN_LOCATION], state);
    const auto cols = stateLocations(hasAlgebraic(sMode) ? locations.algOffset : kNullLocation,
                                     hasDifferential(sMode) ? locations.diffOffset : kNullLocation);
    const auto voltageCol = inputLocs[VOLTAGE_IN_LOCATION];
    if (hasDifferential(sMode)) {
        matrixDataValue.assign(cols[0], cols[0], (-1.0 / T3) - stateDataValue.cj);
        matrixDataValue.assignCheckCol(cols[0], voltageCol, 1.0 / T3);
    }
    if (cols[1] != kNullLocation) {
        const auto partials = T4 == 0.0 ? signals.outputDerivatives : signals.rateDerivatives;
        matrixDataValue.assignCheckCol(cols[1], cols[0], partials[0]);
        matrixDataValue.assign(cols[1],
                               cols[1],
                               partials[1] - (T4 == 0.0 ? 1.0 : stateDataValue.cj));
        matrixDataValue.assignCheckCol(cols[1], voltageCol, partials[2]);
    }
    if (cols[2] != kNullLocation) {
        double scale;
        if (T5 == 0.0) {
            scale = clampSlope(signals.limitedOutput, reactorMinimumPU(), 1.0);
        } else {
            const double rate = (signals.limitedOutput - state[2]) / T5;
            scale = outwardRate(state[2], rate, reactorMinimumPU(), 1.0) ? 0.0 : 1.0 / T5;
        }
        matrixDataValue.assignCheckCol(cols[2], cols[0], scale * signals.outputDerivatives[0]);
        matrixDataValue.assignCheckCol(cols[2], cols[1], scale * signals.outputDerivatives[1]);
        matrixDataValue.assign(cols[2], cols[2], T5 == 0.0 ? -1.0 : -scale - stateDataValue.cj);
        matrixDataValue.assignCheckCol(cols[2], voltageCol, scale * signals.outputDerivatives[2]);
    }
}

void GenModelCSVGN1::outputPartialDerivatives(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              MatrixData<double>& matrixDataValue,
                                              const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double reactor = stateValues(locations.algStateLoc, locations.diffStateLoc)[2];
    if ((reactor >= reactorMinimumPU()) && (reactor < 1.0)) {
        const double voltage = inputs[VOLTAGE_IN_LOCATION];
        const auto cols =
            stateLocations(hasAlgebraic(sMode) ? locations.algOffset : kNullLocation,
                           hasDifferential(sMode) ? locations.diffOffset : kNullLocation);
        matrixDataValue.assignCheckCol(QOUT_LOCATION, cols[2], voltage * voltage);
    }
}

count_t GenModelCSVGN1::outputDependencyCount(index_t outNum, const SolverMode& sMode) const
{
    return outNum == QOUT_LOCATION && (T5 == 0.0 ? hasAlgebraic(sMode) : hasDifferential(sMode)) ?
        1 :
        0;
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
    const double reactor = std::clamp(stateValues(locations.algStateLoc, locations.diffStateLoc)[2],
                                      reactorMinimumPU(),
                                      1.0);
    const double shuntSusceptance = (CBASE / machineBasePower) - reactor;
    matrixDataValue.assign(QOUT_LOCATION,
                           inputLocs[VOLTAGE_IN_LOCATION],
                           -2.0 * shuntSusceptance * voltage);
}
}  // namespace griddyn::genmodels
