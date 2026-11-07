/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REPCA1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 2> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};
    constexpr std::array<RenewablePort, 5> inputPortMapWithFrequency{{
        inputPortMap[0],
        inputPortMap[1],
        {.signal = RenewableSignal::terminalFrequency, .ioIndex = 2},
        {.signal = RenewableSignal::electricalPower, .ioIndex = 3, .base = RenewableBase::machine},
        {.signal = RenewableSignal::activeReference,
         .ioIndex = 4,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 2> outputPortMap{{
        {.signal = RenewableSignal::activeReferenceIncrement,
         .ioIndex = 0,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactiveReferenceIncrement,
         .ioIndex = 1,
         .base = RenewableBase::machine},
    }};
    constexpr index_t pext = 0, qext = 1, voltageFilter = 0, reactiveFilter = 1, integral = 2,
                      lag = 3, activePowerFilter = 4, activeIntegral = 5, activeLag = 6;
    constexpr index_t frequencyInput = 2, electricalPowerInput = 3, activeReferenceInput = 4;
}  // namespace

const ModelDescription& REPCA1::getModelDescription() const
{
    static const auto description = ModelDescription::define<REPCA1>(
        "REPCA1",
        &RenewableComponent::getModelDescription(),
        {parameter({"tfltr"}, &REPCA1::Tfltr, units::second, "Tfltr"),
         parameter({"kp"}, &REPCA1::Kp, units::one, "Kp"),
         parameter({"ki"}, &REPCA1::Ki, units::one, "Ki"),
         parameter({"tft"}, &REPCA1::Tft, units::second, "Tft"),
         parameter({"tfv"}, &REPCA1::Tfv, units::second, "Tfv"),
         parameter({"vfrz"}, &REPCA1::Vfrz, units::one, "Vfrz"),
         parameter({"rc"}, &REPCA1::Rc, units::one, "Rc"),
         parameter({"xc"}, &REPCA1::Xc, units::one, "Xc"),
         parameter({"kc"}, &REPCA1::Kc, units::one, "Kc"),
         parameter({"emax"}, &REPCA1::emax, units::one, "Emax"),
         parameter({"emin"}, &REPCA1::emin, units::one, "Emin"),
         parameter({"dbd1"}, &REPCA1::dbd1, units::one, "Dbd1"),
         parameter({"dbd2"}, &REPCA1::dbd2, units::one, "Dbd2"),
         parameter({"qmax"}, &REPCA1::Qmax, units::one, "Qmax"),
         parameter({"qmin"}, &REPCA1::Qmin, units::one, "Qmin"),
         parameter({"kpg"}, &REPCA1::Kpg, units::one, "Kpg"),
         parameter({"kig"}, &REPCA1::Kig, units::one, "Kig"),
         parameter({"tp"}, &REPCA1::Tp, units::second, "Tp"),
         parameter({"fdbd1"}, &REPCA1::fdbd1, units::one, "Fdbd1"),
         parameter({"fdbd2"}, &REPCA1::fdbd2, units::one, "Fdbd2"),
         parameter({"femax"}, &REPCA1::femax, units::one, "Femax"),
         parameter({"femin"}, &REPCA1::femin, units::one, "Femin"),
         parameter({"pmax"}, &REPCA1::Pmax, units::one, "Pmax"),
         parameter({"pmin"}, &REPCA1::Pmin, units::one, "Pmin"),
         parameter({"tg", "tlag"}, &REPCA1::Tg, units::second, "Tg"),
         parameter({"ddn"}, &REPCA1::Ddn, units::one, "Ddn"),
         parameter({"dup"}, &REPCA1::Dup, units::one, "Dup"),
         parameter({"freqref"}, &REPCA1::FreqRef, units::one, "Freq ref"),
         parameter({"vref"}, &REPCA1::vReference, units::one, "V reference"),
         parameter({"qref"}, &REPCA1::qReference, units::one, "Q reference"),
         parameter({"vcflag"}, &REPCA1::VCFlag, units::one, "Vcflag", parameterValidators::binary),
         parameter(
             {"refflag"}, &REPCA1::RefFlag, units::one, "Ref flag", parameterValidators::binary),
         parameter({"fflag"}, &REPCA1::Fflag, units::one, "Fflag", parameterValidators::binary),
         parameter({"plflag"}, &REPCA1::PLflag, units::one, "Plflag", parameterValidators::binary),
         command({"vbus"}, units::defunit, "Set vbus"),
         state("Pext", units::one, "Solver variable Pext when active"),
         state("Qext", units::one, "Solver variable Qext when active"),
         state("Vf", units::one, "Solver variable Vf when active"),
         state("Qf", units::one, "Solver variable Qf when active"),
         state("Qi", units::one, "Solver variable Qi when active"),
         state("Qlag", units::one, "Solver variable Qlag when active"),
         state("PbranchFilter", units::one, "Solver variable PbranchFilter when active"),
         state("Pintegral", units::one, "Solver variable Pintegral when active"),
         state("Plag", units::one, "Solver variable Plag when active"),
         localState("initialActiveReference",
                    &REPCA1::initialActiveReference,
                    "Initial active reference")});
    return description;
}

void REPCA1::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = REPCA1::getModelDescription();
    getParamString<REPCA1, RenewableComponent>(this,
                                               pstr,
                                               description.numericNames(),
                                               description.stringNames(),
                                               description.flagNames(),
                                               pstype);
}

REPCA1::REPCA1(const std::string& objName): RenewableComponent(objName)
{
    m_inputSize = 2;
    m_outputSize = 2;
}

CoreObject* REPCA1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REPCA1, RenewableComponent>(this, obj);
    if (out == nullptr) {
        return obj;
    }
    out->VCFlag = VCFlag;
    out->RefFlag = RefFlag;
    out->Fflag = Fflag;
    out->PLflag = PLflag;
    out->Tfltr = Tfltr;
    out->Kp = Kp;
    out->Ki = Ki;
    out->Tft = Tft;
    out->Tfv = Tfv;
    out->Vfrz = Vfrz;
    out->Rc = Rc;
    out->Xc = Xc;
    out->Kc = Kc;
    out->emax = emax;
    out->emin = emin;
    out->dbd1 = dbd1;
    out->dbd2 = dbd2;
    out->Qmax = Qmax;
    out->Qmin = Qmin;
    out->Kpg = Kpg;
    out->Kig = Kig;
    out->Tp = Tp;
    out->fdbd1 = fdbd1;
    out->fdbd2 = fdbd2;
    out->femax = femax;
    out->femin = femin;
    out->Pmax = Pmax;
    out->Pmin = Pmin;
    out->Tg = Tg;
    out->Ddn = Ddn;
    out->Dup = Dup;
    out->FreqRef = FreqRef;
    out->vReference = vReference;
    out->qReference = qReference;
    out->initialActiveReference = initialActiveReference;
    out->Vbus = Vbus;
    out->updateInputSize();
    return out;
}

void REPCA1::updateInputSize()
{
    m_inputSize = Fflag == 0 ? 2 : 5;
}

std::span<const RenewablePort> REPCA1::inputPorts() const
{
    return Fflag == 0 ? std::span<const RenewablePort>{inputPortMap} :
                        std::span<const RenewablePort>{inputPortMapWithFrequency};
}
std::span<const RenewablePort> REPCA1::outputPorts() const
{
    return outputPortMap;
}

index_t REPCA1::sourceBusID(RenewableSignal signal) const
{
    if (signal == RenewableSignal::terminalVoltage ||
        (Fflag == 1 && signal == RenewableSignal::terminalFrequency)) {
        return Vbus;
    }
    return kNullLocation;
}

void REPCA1::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(REPCA1::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        if (param == "fflag") {
            updateInputSize();
        }
        return;
    }
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key != param &&
        isSetHandled(REPCA1::getModelDescription().trySetLocal(*this, key, val, unitType))) {
        if (key == "fflag") {
            updateInputSize();
        }
        return;
    }
    if (key == "vbus") {
        if (val < 0 || std::trunc(val) != val || val >= static_cast<double>(kNullLocation)) {
            throw InvalidParameterValue("REPCA1 Vbus must be a non-negative integer bus ID");
        }
        Vbus = val == 0.0 ? kNullLocation : static_cast<index_t>(val);
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double REPCA1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (const auto value = REPCA1::getModelDescription().tryGetLocal(*this, key, unitType)) {
        return *value;
    }
    if (key == "vbus") {
        return Vbus == kNullLocation ? 0.0 : static_cast<double>(Vbus);
    }

    return RenewableComponent::get(param, unitType);
}

void REPCA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 28> inputs{Tfltr, Kp,   Ki,   Tft,  Tfv,   Vfrz,  Rc,
                                        Xc,    Kc,   emax, emin, dbd1,  dbd2,  Qmax,
                                        Qmin,  Kpg,  Kig,  Tp,   fdbd1, fdbd2, femax,
                                        femin, Pmax, Pmin, Tg,   Ddn,   Dup,   FreqRef};
    const bool invalidFrequencyParameters = Fflag == 1 &&
        (Tp <= 0 || Tg <= 0 || Kpg < 0 || Kig < 0 || femax < femin || Pmax < Pmin || fdbd1 > 0 ||
         fdbd2 < 0 || Ddn < 0 || Dup < 0 || FreqRef <= 0);
    if (std::any_of(inputs.begin(),
                    inputs.end(),
                    [](double stateValue) { return !std::isfinite(stateValue); }) ||
        Tfltr <= 0 || Tfv <= 0 || Tft < 0 || Kp < 0 || Ki < 0 || Qmax < Qmin || emax < emin ||
        dbd1 > 0 || dbd2 < 0 || (RefFlag != 0 && RefFlag != 1) || (VCFlag != 0 && VCFlag != 1) ||
        (PLflag != 0 && PLflag != 1) || (Fflag != 0 && Fflag != 1) || invalidFrequencyParameters ||
        Rc != 0 || Xc != 0 || Kc != 0) {
        throw InvalidParameterValue(
            "REPCA1 unsupported remote measurement mode or invalid parameters");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = Fflag == 0 ? 4 : 7;
    local.jacSize = Fflag == 0 ? 48 : 128;
    prevTime = time0;
}

void REPCA1::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet)
{
    if (inputs.size() < (Fflag == 0 ? 2U : 5U) || desiredOutput.size() < 2 ||
        !std::isfinite(inputs[0]) || !std::isfinite(inputs[1]) || inputs[0] <= 0 ||
        inputs[1] == kNullVal || !std::isfinite(desiredOutput[0]) ||
        !std::isfinite(desiredOutput[1]) || desiredOutput[0] == kNullVal ||
        desiredOutput[1] == kNullVal ||
        (Fflag == 1 &&
         (!std::isfinite(inputs[frequencyInput]) || !std::isfinite(inputs[electricalPowerInput]) ||
          inputs[frequencyInput] == kNullVal || inputs[electricalPowerInput] == kNullVal))) {
        throw InvalidParameterValue("REPCA1 initial terminal voltage or Q");
    }
    vReference = inputs[0];
    qReference = inputs[1];
    initialActiveReference = desiredOutput[0];
    const double plantPref = activePowerReference(inputs);
    const double initialActiveCommand = Fflag == 1 ? std::clamp(plantPref, Pmin, Pmax) : 0.0;
    m_state[pext] = Fflag == 1 ? initialActiveCommand - plantPref : 0.0;
    m_state[qext] = 0;
    m_state[2 + voltageFilter] = inputs[0];
    m_state[2 + reactiveFilter] = inputs[1];
    m_state[2 + integral] = 0;
    m_state[2 + lag] = 0;
    if (Fflag == 1) {
        m_state[2 + activePowerFilter] = inputs[electricalPowerInput];
        const double initialError = activePowerError(inputs, m_state.data() + 2);
        m_state[2 + activeIntegral] = initialActiveCommand - (Kpg * initialError);
        m_state[2 + activeLag] = initialActiveCommand;
    }
    fieldSet = {m_state[pext], 0.0};
}

double REPCA1::controllerError(double voltage, const double state[]) const
{
    if (voltage < Vfrz) {
        return 0.0;
    }
    const double raw =
        (RefFlag == 1 ? vReference - state[voltageFilter] : qReference - state[reactiveFilter]);
    double deadband = 0.0;
    if (raw > dbd2) {
        deadband = raw - dbd2;
    } else if (raw < dbd1) {
        deadband = raw - dbd1;
    }
    return std::clamp(deadband, emin, emax);
}

double REPCA1::piOutput(double voltage, const double state[]) const
{
    return std::clamp((Kp * controllerError(voltage, state)) + (Ki * state[integral]), Qmin, Qmax);
}

double REPCA1::reactiveIncrement(double voltage, const double state[]) const
{
    const double ratio = Tft / Tfv;
    return (ratio * piOutput(voltage, state)) + ((1.0 - ratio) * state[lag]);
}

double REPCA1::activePowerReference(const IOdata& inputs) const
{
    if (Fflag == 1 && inputs.size() > activeReferenceInput &&
        inputs[activeReferenceInput] != kNullVal && std::isfinite(inputs[activeReferenceInput])) {
        return inputs[activeReferenceInput];
    }
    return initialActiveReference;
}

double REPCA1::activePowerError(const IOdata& inputs, const double state[]) const
{
    // A positive deviation is underfrequency (Dup); a negative one is overfrequency (Ddn).
    const double frequencyDeviation = FreqRef - inputs[frequencyInput];
    double deadbandedDeviation = 0.0;
    if (frequencyDeviation > fdbd2) {
        deadbandedDeviation = frequencyDeviation - fdbd2;
    } else if (frequencyDeviation < fdbd1) {
        deadbandedDeviation = frequencyDeviation - fdbd1;
    }
    const double frequencyPower =
        deadbandedDeviation >= 0.0 ? Dup * deadbandedDeviation : Ddn * deadbandedDeviation;
    const double rawError =
        activePowerReference(inputs) - state[activePowerFilter] + frequencyPower;
    return std::clamp(rawError, femin, femax);
}

double REPCA1::activePiOutput(const IOdata& inputs, const double state[]) const
{
    const double error = activePowerError(inputs, state);
    return std::clamp((Kpg * error) + state[activeIntegral], Pmin, Pmax);
}

double REPCA1::activeIncrement(const IOdata& inputs, const double state[]) const
{
    return Fflag == 1 ? state[activeLag] - activePowerReference(inputs) : 0.0;
}

std::array<double, 7> REPCA1::rates(const IOdata& inputs, const double state[]) const
{
    const double voltage = inputs[0];
    const double reactivePower = inputs[1];
    const double err = controllerError(voltage, state);
    const double raw = (Kp * err) + (Ki * state[integral]);
    const bool upper = raw >= Qmax && err > 0;
    const bool lower = raw <= Qmin && err < 0;
    std::array<double, 7> result{(voltage - state[voltageFilter]) / Tfltr,
                                 (reactivePower - state[reactiveFilter]) / Tfltr,
                                 upper || lower ? 0.0 : err,
                                 (piOutput(voltage, state) - state[lag]) / Tfv,
                                 0.0,
                                 0.0,
                                 0.0};
    if (Fflag == 1) {
        const double activeError = activePowerError(inputs, state);
        const double activeRaw = (Kpg * activeError) + state[activeIntegral];
        const bool activeUpper = activeRaw >= Pmax && activeError > 0;
        const bool activeLower = activeRaw <= Pmin && activeError < 0;
        result[activePowerFilter] = (inputs[electricalPowerInput] - state[activePowerFilter]) / Tp;
        result[activeIntegral] = activeUpper || activeLower ? 0.0 : Kig * activeError;
        result[activeLag] = (activePiOutput(inputs, state) - state[activeLag]) / Tg;
    }
    return result;
}

void REPCA1::derivative(const IOdata& inputs,
                        const StateData& stateData,
                        double deriv[],
                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = rates(inputs, loc.diffStateLoc);
    for (index_t index = 0; index < loc.diffSize; ++index) {
        loc.destDiffLoc[index] = result[index];
    }
}

void REPCA1::residual(const IOdata& inputs,
                      const StateData& stateData,
                      double resid[],
                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[pext] = activeIncrement(inputs, loc.diffStateLoc) - loc.algStateLoc[pext];
        loc.destLoc[qext] = reactiveIncrement(inputs[0], loc.diffStateLoc) - loc.algStateLoc[qext];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void REPCA1::algebraicUpdate(const IOdata& inputs,
                             const StateData& stateData,
                             double update[],
                             const SolverMode& sMode,
                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[pext] = activeIncrement(inputs, loc.diffStateLoc);
    loc.destLoc[qext] = reactiveIncrement(inputs[0], loc.diffStateLoc);
}

void REPCA1::jacobianElements(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto alg = loc.algOffset;
    const auto diff = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    constexpr double step = 1e-6;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg + pext, alg + pext, -1.0);
        matrixData.assign(alg + qext, alg + qext, -1.0);
    }
    if (!hasDifferential(sMode) && !hasAlgebraic(sMode)) {
        return;
    }
    if (!isAlgebraicOnly(sMode)) {
        for (index_t column = 0; column < loc.diffSize; ++column) {
            std::array<double, 7> plus{};
            std::copy_n(state, loc.diffSize, plus.begin());
            auto minus = plus;
            plus[column] += step;
            minus[column] -= step;
            if (hasAlgebraic(sMode)) {
                matrixData.assign(alg + pext,
                                  diff + column,
                                  (activeIncrement(inputs, plus.data()) -
                                   activeIncrement(inputs, minus.data())) /
                                      (2 * step));
                matrixData.assign(alg + qext,
                                  diff + column,
                                  (reactiveIncrement(inputs[0], plus.data()) -
                                   reactiveIncrement(inputs[0], minus.data())) /
                                      (2 * step));
            }
            if (hasDifferential(sMode)) {
                const auto upper = rates(inputs, plus.data());
                const auto lower = rates(inputs, minus.data());
                for (index_t index = 0; index < loc.diffSize; ++index) {
                    matrixData.assign(diff + index,
                                      diff + column,
                                      ((upper[index] - lower[index]) / (2 * step)) -
                                          (index == column ? stateData.cj : 0.0));
                }
            }
        }
    }
    for (std::size_t column = 0; column < inputs.size() && column < inputLocs.size(); ++column) {
        if (inputLocs[column] == kNullLocation) {
            continue;
        }
        if (column == activeReferenceInput && inputs[column] == kNullVal) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[column] += step;
        minus[column] -= step;
        if (hasAlgebraic(sMode)) {
            matrixData.assign(alg + pext,
                              inputLocs[column],
                              (activeIncrement(plus, state) - activeIncrement(minus, state)) /
                                  (2 * step));
            if (column == 0) {
                matrixData.assign(alg + qext,
                                  inputLocs[column],
                                  (reactiveIncrement(plus[0], state) -
                                   reactiveIncrement(minus[0], state)) /
                                      (2 * step));
            }
        }
        if (hasDifferential(sMode)) {
            const auto upper = rates(plus, state);
            const auto lower = rates(minus, state);
            for (index_t index = 0; index < loc.diffSize; ++index) {
                matrixData.assign(diff + index,
                                  inputLocs[column],
                                  (upper[index] - lower[index]) / (2 * step));
            }
        }
    }
}

void REPCA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime = time - prevTime;
    if (deltaTime < 0) {
        throw InvalidParameterValue("REPCA1 timestep precedes current time");
    }
    const auto rate = rates(inputs, m_state.data() + 2);
    const index_t diffSize = Fflag == 0 ? 4 : 7;
    for (index_t index = 0; index < diffSize; ++index) {
        m_state[2 + index] += deltaTime * rate[index];
    }
    m_state[pext] = activeIncrement(inputs, m_state.data() + 2);
    m_state[qext] = reactiveIncrement(inputs[0], m_state.data() + 2);
    prevTime = time;
}

IOdata REPCA1::getOutputs(const IOdata& /*inputs*/,
                          const StateData& stateData,
                          const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[pext], loc.algStateLoc[qext]};
}

void REPCA1::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const StateData& /*stateDataValue*/,
                                      MatrixData<double>& matrixData,
                                      const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(pext, alg + pext, 1.0);
    matrixData.assign(qext, alg + qext, 1.0);
}

stringVec REPCA1::localStateNames() const
{
    stringVec names{"Pext", "Qext", "Vf", "Qf", "Qi", "Qlag"};
    if (Fflag == 1) {
        names.insert(names.end(), {"PbranchFilter", "Pintegral", "Plag"});
    }
    return names;
}
}  // namespace griddyn
