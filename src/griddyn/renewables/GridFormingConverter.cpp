/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "GridFormingConverter.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace griddyn {
namespace {
    enum Parameter : std::uint8_t {
        FN,
        TC,
        KW,
        KV,
        INERTIA,
        DAMPING,
        RESISTANCE,
        REACTANCE,
        KPVD,
        KIVD,
        KPVQ,
        KIVQ,
        KPID,
        KIID,
        KPIQ,
        KIIQ,
        TID,
        TIQ,
        WDRP,
        QDRP,
        TR,
        TE,
        KPI,
        KII,
        KPV,
        KIV,
        PMAX,
        PMIN,
        KPPLIM,
        KIPLIM,
        QMAX,
        QMIN,
        KPQLIM,
        KIQLIM,
        TPM,
        DWMAX,
        DWMIN,
        MF,
        DD,
        COUNT
    };
    constexpr std::array<std::string_view, COUNT> names{
        {"fn",   "tc",   "kw",     "kv",     "m",    "d",     "ra",    "xs",   "kpvd",   "kivd",
         "kpvq", "kivq", "kpid",   "kiid",   "kpiq", "kiiq",  "tid",   "tiq",  "wdrp",   "qdrp",
         "tr",   "te",   "kpi",    "kii",    "kpv",  "kiv",   "pmax",  "pmin", "kpplim", "kiplim",
         "qmax", "qmin", "kpqlim", "kiqlim", "tpm",  "dwmax", "dwmin", "mf",   "dd"}};
    constexpr std::array<double, COUNT> defaults{
        {60.0, .01,  0.0, 0.0,  10.0, 0.0,  0.0,  .2,   .5,   .02,  .5,    .02, .2,
         .01,  .2,   .01, .01,  .01,  .033, .045, .005, .005, .5,   20.0,  3.0, 10.0,
         1.0,  -1.0, 5.0, 30.0, 1.0,  -1.0, .1,   1.5,  .025, 75.0, -75.0, .15, .11}};
    constexpr std::array<RenewablePort, 2> terminals{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 1},
    }};
    constexpr std::array<RenewablePort, 3> pllTerminals{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 1},
        {.signal = RenewableSignal::frequencyDeviation, .ioIndex = 2},
    }};
    constexpr std::array<RenewablePort, 2> powers{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};
    constexpr double piConstant = 3.14159265358979323846;
}  // namespace

GridFormingConverter::GridFormingConverter(Variant variantType, const std::string& objName):
    TerminalElectricalModel(objName), variant(variantType)
{
    std::copy(defaults.begin(), defaults.end(), parameters.begin());
    m_inputSize = variant == Variant::f2 ? 3 : 2;
}

GridFormingConverter::GridFormingConverter(const std::string& objName):
    GridFormingConverter(Variant::cv1, objName)
{
}

CoreObject* GridFormingConverter::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GridFormingConverter, TerminalElectricalModel>(this, obj);
    if (out != nullptr) {
        out->variant = variant;
        out->parameters = parameters;
        out->pllName = pllName;
        out->initialActivePower = initialActivePower;
        out->initialReactivePower = initialReactivePower;
        out->initialVoltage = initialVoltage;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> GridFormingConverter::inputPorts() const
{
    return variant == Variant::f2 ? std::span<const RenewablePort>{pllTerminals} :
                                    std::span<const RenewablePort>{terminals};
}

std::span<const RenewablePort> GridFormingConverter::outputPorts() const
{
    return powers;
}

std::string_view GridFormingConverter::sourceName(RenewableSignal signal) const
{
    return signal == RenewableSignal::frequencyDeviation ? std::string_view{pllName} :
                                                           std::string_view{};
}

void GridFormingConverter::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    for (index_t parameterIndex = 0; parameterIndex < COUNT; ++parameterIndex) {
        if (key == names[parameterIndex] || (key == "rf" && parameterIndex == RESISTANCE) ||
            (key == "xf" && parameterIndex == REACTANCE)) {
            if (!std::isfinite(val)) {
                throw InvalidParameterValue("grid-forming converter parameter must be finite");
            }
            parameters[parameterIndex] = val;
            return;
        }
    }
    TerminalElectricalModel::set(param, val, unitType);
}

void GridFormingConverter::set(std::string_view param, std::string_view val)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "pll") {
        pllName = std::string{val};
        return;
    }
    GridComponent::set(param, val);
}

double GridFormingConverter::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    for (index_t parameterIndex = 0; parameterIndex < COUNT; ++parameterIndex) {
        if (key == names[parameterIndex] || (key == "rf" && parameterIndex == RESISTANCE) ||
            (key == "xf" && parameterIndex == REACTANCE)) {
            return parameters[parameterIndex];
        }
    }
    return TerminalElectricalModel::get(param, unitType);
}

index_t GridFormingConverter::stateCount() const
{
    if (variant == Variant::cv1) {
        return 8;
    }
    if (variant == Variant::cv2) {
        return 6;
    }
    return variant == Variant::f1 ? 13 : 14;
}

void GridFormingConverter::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const bool isVoltageControlled = variant == Variant::cv1 || variant == Variant::cv2;
    const double impedanceSquared = (parameters[RESISTANCE] * parameters[RESISTANCE]) +
        (parameters[REACTANCE] * parameters[REACTANCE]);
    if (std::any_of(parameters.begin(),
                    parameters.begin() + COUNT,
                    [](double value) { return !std::isfinite(value); }) ||
        parameters[FN] <= 0.0 || impedanceSquared <= 0.0 ||
        (isVoltageControlled && parameters[INERTIA] <= 0.0) ||
        (variant == Variant::cv1 && parameters[TC] <= 0.0) ||
        (variant == Variant::cv2 && (parameters[TID] <= 0.0 || parameters[TIQ] <= 0.0)) ||
        (!isVoltageControlled &&
         (parameters[TR] <= 0.0 || parameters[TE] <= 0.0 || parameters[TPM] <= 0.0 ||
          parameters[WDRP] <= 0.0 || parameters[PMIN] > parameters[PMAX] ||
          parameters[QMIN] > parameters[QMAX] || parameters[DWMIN] > parameters[DWMAX])) ||
        (variant == Variant::f2 && (parameters[MF] <= 0.0 || pllName.empty()))) {
        throw InvalidParameterValue("grid-forming converter parameters or PLL binding");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = stateCount();
    local.jacSize = (stateCount() + 2) * (stateCount() + 5);
    prevTime = time0;
}

void GridFormingConverter::dynObjectInitializeB(const IOdata& inputs,
                                                const IOdata& desiredOutput,
                                                IOdata& fieldSet)
{
    if (inputs.size() < static_cast<std::size_t>(m_inputSize) || desiredOutput.size() < 2 ||
        !std::isfinite(inputs[0]) || inputs[0] <= 0.0 || !std::isfinite(inputs[1]) ||
        !std::isfinite(desiredOutput[0]) || !std::isfinite(desiredOutput[1])) {
        throw InvalidParameterValue("grid-forming converter terminal or initial P/Q");
    }
    if (variant == Variant::f2 && (!std::isfinite(inputs[2]) || inputs[2] == kNullVal)) {
        throw InvalidParameterValue("REGF2 requires a named PLL frequency deviation");
    }
    const double dCurrent = desiredOutput[0] / inputs[0];
    const double reactiveCurrent = -desiredOutput[1] / inputs[0];
    const double internalVoltageD =
        inputs[0] + (parameters[RESISTANCE] * dCurrent) - (parameters[REACTANCE] * reactiveCurrent);
    const double internalVoltageQ =
        (parameters[RESISTANCE] * reactiveCurrent) + (parameters[REACTANCE] * dCurrent);
    m_state[0] = desiredOutput[0];
    m_state[1] = desiredOutput[1];
    initialActivePower = desiredOutput[0];
    initialReactivePower = desiredOutput[1];
    initialVoltage = inputs[0];
    if (variant == Variant::cv1 || variant == Variant::cv2) {
        m_state[2] = 0.0;  // dw
        m_state[3] = inputs[1];  // delta
        m_state[4] = dCurrent;  // outer d integral
        m_state[5] = reactiveCurrent;  // outer q integral
        if (variant == Variant::cv1) {
            m_state[6] = parameters[RESISTANCE] * dCurrent;
            m_state[7] = parameters[RESISTANCE] * reactiveCurrent;
            m_state[8] = internalVoltageD;
            m_state[9] = internalVoltageQ;
        } else {
            m_state[6] = dCurrent;
            m_state[7] = reactiveCurrent;
        }
    } else {
        m_state[2] = inputs[1];  // delta
        m_state[3] = desiredOutput[0];  // Psen
        m_state[4] = desiredOutput[1];  // Qsen
        m_state[5] = desiredOutput[0];  // Psig
        m_state[6] = desiredOutput[1];  // Qsig
        m_state[7] = desiredOutput[0];  // PIplim integral
        m_state[8] = desiredOutput[1];  // PIqlim integral
        m_state[9] = dCurrent;  // outer d integral
        m_state[10] = reactiveCurrent;  // outer q integral
        m_state[11] = 0.0;  // inner d integral
        m_state[12] = 0.0;  // inner q integral
        m_state[13] = internalVoltageD;
        m_state[14] = internalVoltageQ;
        if (variant == Variant::f2) {
            m_state[15] = 1.0;
        }
        if (variant == Variant::f3) {
            m_state[15] = inputs[0];
        }
    }
    fieldSet = {};
}

void GridFormingConverter::evaluate(const IOdata& inputs,
                                    const double* alg,
                                    const double* state,
                                    double* powerResidual,
                                    double* rates) const
{
    const bool isVoltageControlled = variant == Variant::cv1 || variant == Variant::cv2;
    const double voltage = inputs[0];
    const double angle = inputs[1];
    const double delta = state[isVoltageControlled ? 1 : 0];
    const double terminalVoltageD = voltage * std::cos(delta - angle);
    const double terminalVoltageQ = -voltage * std::sin(delta - angle);
    const double resistanceValue = parameters[RESISTANCE];
    const double react = parameters[REACTANCE];
    const double impedanceSquared = (resistanceValue * resistanceValue) + (react * react);
    const double internalVoltageD =
        variant == Variant::cv2 ? 0.0 : state[isVoltageControlled ? 6 : 11];
    const double internalVoltageQ =
        variant == Variant::cv2 ? 0.0 : state[isVoltageControlled ? 7 : 12];
    const double dCurrent = variant == Variant::cv2 ?
        state[4] :
        ((resistanceValue * (internalVoltageD - terminalVoltageD)) +
         (react * (internalVoltageQ - terminalVoltageQ))) /
            impedanceSquared;
    const double reactiveCurrent = variant == Variant::cv2 ?
        state[5] :
        ((-react * (internalVoltageD - terminalVoltageD)) +
         (resistanceValue * (internalVoltageQ - terminalVoltageQ))) /
            impedanceSquared;
    powerResidual[0] =
        (terminalVoltageD * dCurrent) + (terminalVoltageQ * reactiveCurrent) - alg[0];
    powerResidual[1] =
        (-terminalVoltageD * reactiveCurrent) + (terminalVoltageQ * dCurrent) - alg[1];
    const double nominalAngularFrequency = 2.0 * piConstant * parameters[FN];
    if (isVoltageControlled) {
        const double pref2 = initialActivePower - (parameters[KW] * state[0]);
        const double vref2 = initialVoltage + (parameters[KV] * (initialReactivePower - alg[1]));
        const double voltageErrorD = terminalVoltageD - vref2;
        const double voltageErrorQ = terminalVoltageQ;
        const double idref = state[2] + (parameters[KPVD] * voltageErrorD);
        const double iqref = state[3] + (parameters[KPVQ] * voltageErrorQ);
        rates[0] = (pref2 - alg[0] - (parameters[DAMPING] * state[0])) / parameters[INERTIA];
        rates[1] = nominalAngularFrequency * state[0];
        rates[2] = parameters[KIVD] * voltageErrorD;
        rates[3] = parameters[KIVQ] * voltageErrorQ;
        if (variant == Variant::cv2) {
            rates[4] = (idref - dCurrent) / parameters[TID];
            rates[5] = (iqref - reactiveCurrent) / parameters[TIQ];
        } else {
            const double eid = dCurrent - idref;
            const double eiq = reactiveCurrent - iqref;
            rates[4] = parameters[KIID] * eid;
            rates[5] = parameters[KIIQ] * eiq;
            const double udref =
                state[4] + (parameters[KPID] * eid) + terminalVoltageD - (iqref * react);
            const double uqref =
                state[5] + (parameters[KPIQ] * eiq) + terminalVoltageQ + (idref * react);
            rates[6] = (udref - internalVoltageD) / parameters[TC];
            rates[7] = (uqref - internalVoltageQ) / parameters[TC];
        }
        return;
    }
    const double perr = state[3] - state[1];
    const double qerr = state[4] - state[2];
    const double plim = state[5] + (parameters[KPPLIM] * perr);
    const double qlim = state[6] + (parameters[KPQLIM] * qerr);
    const double vref2 = variant == Variant::f3 ?
        state[13] :
        initialVoltage + (parameters[QDRP] * (qlim - state[2]));
    double frequencyDeviationRate = nominalAngularFrequency * parameters[WDRP] * (plim - state[1]);
    if (variant == Variant::f2) {
        frequencyDeviationRate = nominalAngularFrequency * (state[13] - 1.0);
    }
    if (variant == Variant::f3) {
        frequencyDeviationRate /= vref2 * vref2;
    }
    rates[0] = std::clamp(frequencyDeviationRate, parameters[DWMIN], parameters[DWMAX]);
    rates[1] = (alg[0] - state[1]) / parameters[TR];
    rates[2] = (alg[1] - state[2]) / parameters[TR];
    const double pLimitRate = (state[1] - state[3]) / parameters[TPM];
    const double qLimitRate = (state[2] - state[4]) / parameters[TPM];
    rates[3] = ((state[3] >= parameters[PMAX] && pLimitRate > 0.0) ||
                (state[3] <= parameters[PMIN] && pLimitRate < 0.0)) ?
        0.0 :
        pLimitRate;
    rates[4] = ((state[4] >= parameters[QMAX] && qLimitRate > 0.0) ||
                (state[4] <= parameters[QMIN] && qLimitRate < 0.0)) ?
        0.0 :
        qLimitRate;
    rates[5] = parameters[KIPLIM] * perr;
    rates[6] = parameters[KIQLIM] * qerr;
    const double voltageErrorD = vref2 - terminalVoltageD;
    const double voltageErrorQ = -terminalVoltageQ;
    const double idref = state[7] + (parameters[KPV] * voltageErrorD);
    const double iqref = state[8] + (parameters[KPV] * voltageErrorQ);
    rates[7] = parameters[KIV] * voltageErrorD;
    rates[8] = parameters[KIV] * voltageErrorQ;
    const double eid = idref - dCurrent;
    const double eiq = iqref - reactiveCurrent;
    rates[9] = parameters[KII] * eid;
    rates[10] = parameters[KII] * eiq;
    const double udref = state[9] + (parameters[KPI] * eid) + terminalVoltageD +
        (resistanceValue * dCurrent) - (react * reactiveCurrent);
    const double uqref = state[10] + (parameters[KPI] * eiq) + terminalVoltageQ +
        (resistanceValue * reactiveCurrent) + (react * dCurrent);
    rates[11] = (udref - internalVoltageD) / parameters[TE];
    rates[12] = (uqref - internalVoltageQ) / parameters[TE];
    if (variant == Variant::f2) {
        rates[13] = ((inputs[2] * parameters[DD] * parameters[WDRP]) + 1.0 +
                     (parameters[WDRP] * (plim - state[1])) - state[13]) /
            (parameters[MF] * parameters[WDRP]);
    } else if (variant == Variant::f3) {
        const double reactiveDroop = parameters[QDRP];
        const double denom =
            100000000.0 - (2.0 * std::pow(100.0 - (100.0 * reactiveDroop), 2.0)) - 10000.0;
        const double kdvoc = parameters[WDRP] * 400000000.0 / (denom * denom);
        rates[13] = nominalAngularFrequency *
            ((parameters[WDRP] * (qlim - state[2]) / vref2) +
             (vref2 * kdvoc * (initialVoltage + vref2) * (initialVoltage - vref2)));
    }
}

void GridFormingConverter::derivative(const IOdata& inputs,
                                      const StateData& stateData,
                                      double deriv[],
                                      const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    std::array<double, 2> residual{};
    evaluate(inputs, loc.algStateLoc, loc.diffStateLoc, residual.data(), loc.destDiffLoc);
}

void GridFormingConverter::residual(const IOdata& inputs,
                                    const StateData& stateData,
                                    double resid[],
                                    const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    std::array<double, 14> rates{};
    std::array<double, 2> power{};
    evaluate(inputs, loc.algStateLoc, loc.diffStateLoc, power.data(), rates.data());
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = power[0];
        loc.destLoc[1] = power[1];
    }
    if (hasDifferential(sMode)) {
        for (index_t parameterIndex = 0; parameterIndex < stateCount(); ++parameterIndex) {
            loc.destDiffLoc[parameterIndex] = rates[parameterIndex] - loc.dstateLoc[parameterIndex];
        }
    }
}

void GridFormingConverter::algebraicUpdate(const IOdata& inputs,
                                           const StateData& stateData,
                                           double update[],
                                           const SolverMode& sMode,
                                           double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    std::array<double, 14> rates{};
    std::array<double, 2> power{};
    evaluate(inputs, loc.algStateLoc, loc.diffStateLoc, power.data(), rates.data());
    loc.destLoc[0] = loc.algStateLoc[0] + power[0];
    loc.destLoc[1] = loc.algStateLoc[1] + power[1];
}

void GridFormingConverter::timestep(CoreTime time,
                                    const IOdata& inputs,
                                    const SolverMode& /*sMode*/)
{
    const double timeIncrement = time - prevTime;
    if (timeIncrement < 0.0) {
        throw InvalidParameterValue("grid-forming converter timestep precedes current time");
    }
    std::array<double, 14> rates{};
    std::array<double, 2> power{};
    evaluate(inputs, m_state.data(), m_state.data() + 2, power.data(), rates.data());
    for (index_t parameterIndex = 0; parameterIndex < stateCount(); ++parameterIndex) {
        m_state[2 + parameterIndex] += timeIncrement * rates[parameterIndex];
    }
    evaluate(inputs, m_state.data(), m_state.data() + 2, power.data(), rates.data());
    m_state[0] += power[0];
    m_state[1] += power[1];
    prevTime = time;
}

void GridFormingConverter::jacobianElements(const IOdata& inputs,
                                            const StateData& stateData,
                                            MatrixData<double>& matrixData,
                                            const IOlocs& inputLocs,
                                            const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const index_t differentialCount = stateCount();
    std::array<double, 2> alg{{loc.algStateLoc[0], loc.algStateLoc[1]}};
    std::array<double, 14> state{};
    std::copy_n(loc.diffStateLoc, differentialCount, state.begin());
    std::array<double, 2> basePower{};
    std::array<double, 14> baseRates{};
    evaluate(inputs, alg.data(), state.data(), basePower.data(), baseRates.data());
    const auto addColumn = [&](index_t column,
                               const std::array<double, 2>& shiftedPower,
                               const std::array<double, 14>& shiftedRates,
                               double stepSize) {
        if (hasAlgebraic(sMode)) {
            matrixData.assignCheckCol(loc.algOffset,
                                      column,
                                      (shiftedPower[0] - basePower[0]) / stepSize);
            matrixData.assignCheckCol(loc.algOffset + 1,
                                      column,
                                      (shiftedPower[1] - basePower[1]) / stepSize);
        }
        if (hasDifferential(sMode)) {
            for (index_t row = 0; row < differentialCount; ++row) {
                matrixData.assignCheckCol(loc.diffOffset + row,
                                          column,
                                          (shiftedRates[row] - baseRates[row]) / stepSize);
            }
        }
    };
    if (hasAlgebraic(sMode)) {
        for (index_t parameterIndex = 0; parameterIndex < 2; ++parameterIndex) {
            const double stepSize = 1e-7 * std::max(1.0, std::abs(alg[parameterIndex]));
            alg[parameterIndex] += stepSize;
            std::array<double, 2> shiftedPower{};
            std::array<double, 14> shiftedRates{};
            evaluate(inputs, alg.data(), state.data(), shiftedPower.data(), shiftedRates.data());
            addColumn(loc.algOffset + parameterIndex, shiftedPower, shiftedRates, stepSize);
            alg[parameterIndex] -= stepSize;
        }
    }
    if (hasDifferential(sMode)) {
        for (index_t parameterIndex = 0; parameterIndex < differentialCount; ++parameterIndex) {
            const double stepSize = 1e-7 * std::max(1.0, std::abs(state[parameterIndex]));
            state[parameterIndex] += stepSize;
            std::array<double, 2> shiftedPower{};
            std::array<double, 14> shiftedRates{};
            evaluate(inputs, alg.data(), state.data(), shiftedPower.data(), shiftedRates.data());
            addColumn(loc.diffOffset + parameterIndex, shiftedPower, shiftedRates, stepSize);
            state[parameterIndex] -= stepSize;
            matrixData.assign(loc.diffOffset + parameterIndex,
                              loc.diffOffset + parameterIndex,
                              -stateData.cj);
        }
    }
    for (std::size_t parameterIndex = 0; parameterIndex < inputs.size(); ++parameterIndex) {
        IOdata perturbed = inputs;
        const double stepSize = 1e-7 * std::max(1.0, std::abs(inputs[parameterIndex]));
        perturbed[parameterIndex] += stepSize;
        std::array<double, 2> shiftedPower{};
        std::array<double, 14> shiftedRates{};
        evaluate(perturbed, alg.data(), state.data(), shiftedPower.data(), shiftedRates.data());
        addColumn(inputLocs[parameterIndex], shiftedPower, shiftedRates, stepSize);
    }
}

IOdata GridFormingConverter::getOutputs(const IOdata& /*inputs*/,
                                        const StateData& stateData,
                                        const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[0], loc.algStateLoc[1]};
}

void GridFormingConverter::outputPartialDerivatives(const IOdata& /*inputs*/,
                                                    const StateData& /*stateData*/,
                                                    MatrixData<double>& matrixData,
                                                    const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(0, alg, 1.0);
    matrixData.assign(1, alg + 1, 1.0);
}

stringVec GridFormingConverter::localStateNames() const
{
    if (variant == Variant::cv1) {
        return {"Pe", "Qe", "dw", "delta", "PIvd", "PIvq", "PIId", "PIIq", "ud", "uq"};
    }
    if (variant == Variant::cv2) {
        return {"Pe", "Qe", "dw", "delta", "PIvd", "PIvq", "Id", "Iq"};
    }
    stringVec result{"Pe",
                     "Qe",
                     "delta",
                     "Psen",
                     "Qsen",
                     "Psig",
                     "Qsig",
                     "PIplim",
                     "PIqlim",
                     "PIvd",
                     "PIvq",
                     "PIId",
                     "PIIq",
                     "ud",
                     "uq"};
    if (variant == Variant::f2) {
        result.emplace_back("INTw");
    }
    if (variant == Variant::f3) {
        result.emplace_back("vref2");
    }
    return result;
}

#define GRID_FORMING_MODEL_IMPL(Model, kind)                                                       \
    Model::Model(const std::string& objName): GridFormingConverter(Variant::kind, objName) {}      \
    CoreObject* Model::clone(CoreObject* obj) const                                                \
    {                                                                                              \
        auto* out = cloneBase<Model, GridFormingConverter>(this, obj);                             \
        if (out != nullptr) {                                                                      \
            out->parameters = parameters;                                                          \
            out->pllName = pllName;                                                                \
            out->initialActivePower = initialActivePower;                                          \
            out->initialReactivePower = initialReactivePower;                                      \
            out->initialVoltage = initialVoltage;                                                  \
        }                                                                                          \
        return out == nullptr ? obj : out;                                                         \
    }
GRID_FORMING_MODEL_IMPL(REGCV1, cv1)
GRID_FORMING_MODEL_IMPL(REGCV2, cv2)
GRID_FORMING_MODEL_IMPL(REGF1, f1)
GRID_FORMING_MODEL_IMPL(REGF2, f2)
GRID_FORMING_MODEL_IMPL(REGF3, f3)
#undef GRID_FORMING_MODEL_IMPL

}  // namespace griddyn
