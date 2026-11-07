/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WT4E1.h"

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
    constexpr std::array<RenewablePort, 5> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::electricalPower, .ioIndex = 1, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 2, .base = RenewableBase::machine},
        {.signal = RenewableSignal::activeReferenceIncrement,
         .ioIndex = 3,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveReferenceIncrement,
         .ioIndex = 4,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 3> outputPortMap{{
        {.signal = RenewableSignal::activeCurrentCommand,
         .ioIndex = 0,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactiveCurrentCommand,
         .ioIndex = 1,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::orderedPower, .ioIndex = 2, .base = RenewableBase::machine},
    }};
    constexpr index_t voltageFilterState = 0;
    constexpr index_t powerFilterState = 1;
    constexpr index_t powerIntegratorState = 2;
    constexpr index_t reactiveIntegratorState = 3;
    constexpr index_t reactiveCurrentFilterState = 4;
    double optional(const IOdata& inputData, std::size_t index)
    {
        return inputData.size() > index && inputData[index] != kNullVal ? inputData[index] : 0.0;
    }
}  // namespace

const ModelDescription& WT4E1::getModelDescription() const
{
    static const auto description = ModelDescription::define<WT4E1>(
        "WT4E1",
        &RenewableComponent::getModelDescription(),
        {parameter({"tfv"}, &WT4E1::Tfv, units::second, "Tfv"),
         parameter({"kpv"}, &WT4E1::Kpv, units::one, "Kpv"),
         parameter({"kiv"}, &WT4E1::Kiv, units::one, "Kiv"),
         parameter({"kpp"}, &WT4E1::Kpp, units::one, "Kpp"),
         parameter({"kip"}, &WT4E1::Kip, units::one, "Kip"),
         parameter({"kf"}, &WT4E1::Kf, units::one, "Kf"),
         parameter({"tf"}, &WT4E1::Tf, units::second, "Tf"),
         parameter({"qmx"}, &WT4E1::Qmax, units::one, "Qmax"),
         parameter({"qmn"}, &WT4E1::Qmin, units::one, "Qmin"),
         parameter({"ipmax"}, &WT4E1::Ipmax, units::one, "Ipmax"),
         parameter({"trv"}, &WT4E1::Trv, units::second, "Trv"),
         parameter({"dpmx"}, &WT4E1::dPmax, units::one, "D pmax"),
         parameter({"dpmn"}, &WT4E1::dPmin, units::one, "D pmin"),
         parameter({"tpower"}, &WT4E1::Tpower, units::second, "Tpower"),
         parameter({"kqi"}, &WT4E1::Kqi, units::one, "Kqi"),
         parameter({"vmincl"}, &WT4E1::Vmincl, units::one, "Vmincl"),
         parameter({"vmaxcl"}, &WT4E1::Vmaxcl, units::one, "Vmaxcl"),
         parameter({"kvi"}, &WT4E1::Kvi, units::one, "Kvi"),
         parameter({"tv"}, &WT4E1::Tv, units::second, "Tv"),
         parameter({"tp"}, &WT4E1::Tp, units::second, "Tp"),
         parameter({"imaxtd"}, &WT4E1::Imax, units::one, "Imax"),
         parameter({"iphl"}, &WT4E1::Iphl, units::one, "Iphl"),
         parameter({"iqhl"}, &WT4E1::Iqhl, units::one, "Iqhl"),
         parameter({"pfaflg"}, &WT4E1::pfFast, units::one, "Pf fast", parameterValidators::binary),
         parameter(
             {"varflg"}, &WT4E1::windVar, units::one, "Wind var", parameterValidators::binary),
         parameter({"pqflag"},
                   &WT4E1::pqPriority,
                   units::one,
                   "Pq priority",
                   parameterValidators::binary),
         parameter({"pssematch"},
                   &WT4E1::psseMatch,
                   units::one,
                   "Psse match",
                   parameterValidators::binary),
         state("Ipcmd", units::one, "Solver variable Ipcmd when active"),
         state("Iqcmd", units::one, "Solver variable Iqcmd when active"),
         state("Pord", units::one, "Solver variable Pord when active"),
         state("Vf", units::one, "Solver variable Vf when active"),
         state("Pf", units::one, "Solver variable Pf when active"),
         state("Pint", units::one, "Solver variable Pint when active"),
         state("Qint", units::one, "Solver variable Qint when active"),
         state("Iqfilter", units::one, "Solver variable Iqfilter when active"),
         localState("initialP", &WT4E1::initialP, "Initial p"),
         localState("initialQ", &WT4E1::initialQ, "Initial q"),
         localState("vref", &WT4E1::vref, "Vref")});
    return description;
}

void WT4E1::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = WT4E1::getModelDescription();
    getParamString<WT4E1, RenewableComponent>(this,
                                              pstr,
                                              description.numericNames(),
                                              description.stringNames(),
                                              description.flagNames(),
                                              pstype);
}

WT4E1::WT4E1(const std::string& objName): RenewableComponent(objName)
{
    m_inputSize = 5;
    m_outputSize = 3;
}

CoreObject* WT4E1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<WT4E1, RenewableComponent>(this, obj);
    if (out == nullptr) {
        return obj;
    }
    out->pfFast = pfFast;
    out->windVar = windVar;
    out->pqPriority = pqPriority;
    out->psseMatch = psseMatch;
    out->Tfv = Tfv;
    out->Kpv = Kpv;
    out->Kiv = Kiv;
    out->Kpp = Kpp;
    out->Kip = Kip;
    out->Kf = Kf;
    out->Tf = Tf;
    out->Qmax = Qmax;
    out->Qmin = Qmin;
    out->Ipmax = Ipmax;
    out->Trv = Trv;
    out->dPmax = dPmax;
    out->dPmin = dPmin;
    out->Tpower = Tpower;
    out->Tiq = Tiq;
    out->Kqi = Kqi;
    out->Vmincl = Vmincl;
    out->Vmaxcl = Vmaxcl;
    out->Kvi = Kvi;
    out->Tv = Tv;
    out->Tp = Tp;
    out->Imax = Imax;
    out->Iphl = Iphl;
    out->Iqhl = Iqhl;
    out->initialP = initialP;
    out->initialQ = initialQ;
    out->vref = vref;
    return out;
}
std::span<const RenewablePort> WT4E1::inputPorts() const
{
    return inputPortMap;
}
std::span<const RenewablePort> WT4E1::outputPorts() const
{
    return outputPortMap;
}

void WT4E1::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(WT4E1::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        return;
    }
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key != param &&
        isSetHandled(WT4E1::getModelDescription().trySetLocal(*this, key, val, unitType))) {
        return;
    }
    RenewableComponent::set(param, val, unitType);
}

double WT4E1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (const auto value = WT4E1::getModelDescription().tryGetLocal(*this, key, unitType)) {
        return *value;
    }

    return RenewableComponent::get(param, unitType);
}

void WT4E1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const std::array<double, 25> values{Tfv,  Kpv,    Kiv,      Kpp,     Kip,   Kf,    Tf,
                                        Qmax, Qmin,   Ipmax,    Trv,     dPmax, dPmin, Tpower,
                                        Kqi,  Vmincl, Vmaxcl,   Kvi,     Tv,    Tp,    Imax,
                                        Iphl, Iqhl,   initialP, initialQ};
    if (std::any_of(values.begin(),
                    values.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        Tfv <= 0 || Tf <= 0 || Trv <= 0 || Tpower <= 0 || Tv <= 0 || Tp <= 0 || Qmax < Qmin ||
        Ipmax <= 0 || Imax <= 0 || Iphl <= 0 || Iqhl <= 0 || Vmaxcl < Vmincl) {
        throw InvalidParameterValue("WT4E1 invalid time constant or limit");
    }
    auto& local = offsets.local().local;
    local.algSize = 3;
    local.diffSize = 5;
    local.jacSize = 64;
    prevTime = time0;
    (void)flags;
}

void WT4E1::dynObjectInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (inputs.empty() || desiredOutput.size() < 2 || inputs[0] <= 0.0 ||
        !std::isfinite(inputs[0])) {
        throw InvalidParameterValue("WT4E1 requires initial voltage and P/Q");
    }
    initialP = desiredOutput[0];
    initialQ = desiredOutput[1];
    vref = inputs[0];
    const double initialActiveCurrent = initialP / vref;
    const double initialReactiveCurrent = initialQ / vref;
    m_state[0] = initialActiveCurrent;
    m_state[1] = initialReactiveCurrent;
    m_state[2] = initialP;
    m_state[3 + voltageFilterState] = vref;
    m_state[3 + powerFilterState] = initialP;
    m_state[3 + powerIntegratorState] = 0.0;
    m_state[3 + reactiveIntegratorState] = 0.0;
    m_state[3 + reactiveCurrentFilterState] = initialReactiveCurrent;
    fieldSet = {initialActiveCurrent, initialReactiveCurrent, initialP};
}

std::array<double, 2> WT4E1::commands(const IOdata& inputs, const double state[]) const
{
    const double voltage = std::max(inputs[0], 0.01);
    const double pReference = initialP + optional(inputs, 3);
    const double qReference = initialQ + optional(inputs, 4);
    const double pOrder = std::clamp(pReference + (Kpp * (pReference - state[powerFilterState])) +
                                         state[powerIntegratorState],
                                     dPmin + initialP,
                                     dPmax + initialP);
    double activeCurrent = std::clamp(pOrder / voltage, -Ipmax, Ipmax);
    const double qOrder = std::clamp(qReference + (Kpv * (vref - state[voltageFilterState])) +
                                         state[reactiveIntegratorState],
                                     Qmin,
                                     Qmax);
    double reactiveCurrent = std::clamp(qOrder / voltage, -Iqhl, Iqhl);
    if (pqPriority == 1) {
        reactiveCurrent =
            std::clamp(reactiveCurrent,
                       -std::sqrt(std::max(0.0, (Imax * Imax) - (activeCurrent * activeCurrent))),
                       std::sqrt(std::max(0.0, (Imax * Imax) - (activeCurrent * activeCurrent))));
    } else {
        activeCurrent = std::clamp(
            activeCurrent,
            -std::sqrt(std::max(0.0, (Imax * Imax) - (reactiveCurrent * reactiveCurrent))),
            std::sqrt(std::max(0.0, (Imax * Imax) - (reactiveCurrent * reactiveCurrent))));
    }
    return {activeCurrent, reactiveCurrent};
}

std::array<double, 5> WT4E1::rates(const IOdata& inputs, const double state[]) const
{
    const auto command = commands(inputs, state);
    const double activePower = inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : initialP;
    const double reactivePower = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : initialQ;
    const double activePowerError = (initialP + optional(inputs, 3)) - state[powerFilterState];
    const double reactivePowerError = (initialQ + optional(inputs, 4)) - reactivePower;
    return {(inputs[0] - state[voltageFilterState]) / Trv,
            (activePower - state[powerFilterState]) / Tpower,
            std::clamp(Kip * activePowerError, dPmin, dPmax),
            (Kiv * (vref - state[voltageFilterState])) + (Kqi * reactivePowerError),
            (command[1] - state[reactiveCurrentFilterState]) / Tiq};
}

void WT4E1::derivative(const IOdata& inputs,
                       const StateData& stateData,
                       double deriv[],
                       const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const auto stateRates = rates(inputs, locations.diffStateLoc);
    for (index_t stateIndex = 0; stateIndex < 5; ++stateIndex) {
        locations.destDiffLoc[stateIndex] = stateRates[stateIndex];
    }
}
void WT4E1::residual(const IOdata& inputs,
                     const StateData& stateData,
                     double resid[],
                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto commandValues = commands(inputs, locations.diffStateLoc);
        locations.destLoc[0] = commandValues[0] - locations.algStateLoc[0];
        locations.destLoc[1] =
            locations.diffStateLoc[reactiveCurrentFilterState] - locations.algStateLoc[1];
        locations.destLoc[2] = initialP + optional(inputs, 3) - locations.algStateLoc[2];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t stateIndex = 0; stateIndex < 5; ++stateIndex) {
            locations.destDiffLoc[stateIndex] -= locations.dstateLoc[stateIndex];
        }
    }
}
void WT4E1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double update[],
                            const SolverMode& sMode,
                            double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    const auto commandValues = commands(inputs, locations.diffStateLoc);
    locations.destLoc[0] = commandValues[0];
    locations.destLoc[1] = locations.diffStateLoc[reactiveCurrentFilterState];
    locations.destLoc[2] = initialP + optional(inputs, 3);
    (void)alpha;
}

void WT4E1::jacobianElements(const IOdata& inputs,
                             const StateData& stateData,
                             MatrixData<double>& matrixData,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    constexpr double step = 1e-6;
    const auto assign = [&](const auto& upperValues,
                            const auto& lowerValues,
                            index_t columnIndex,
                            bool differential) {
        for (index_t rowIndex = 0; rowIndex < (differential ? 5 : 3); ++rowIndex) {
            matrixData.assign(differential ? locations.diffOffset + rowIndex :
                                             locations.algOffset + rowIndex,
                              columnIndex,
                              (upperValues[rowIndex] - lowerValues[rowIndex]) / (2 * step));
        }
    };
    if (hasAlgebraic(sMode)) {
        for (index_t rowIndex = 0; rowIndex < 3; ++rowIndex) {
            matrixData.assign(locations.algOffset + rowIndex, locations.algOffset + rowIndex, -1.0);
        }
        if (hasDifferential(sMode)) {
            for (index_t stateIndex = 0; stateIndex < 5; ++stateIndex) {
                auto plus = std::array<double, 5>{locations.diffStateLoc[0],
                                                  locations.diffStateLoc[1],
                                                  locations.diffStateLoc[2],
                                                  locations.diffStateLoc[3],
                                                  locations.diffStateLoc[4]};
                auto minus = plus;
                plus[stateIndex] += step;
                minus[stateIndex] -= step;
                const auto upperCommands = commands(inputs, plus.data());
                const auto lowerCommands = commands(inputs, minus.data());
                const std::array<double, 3> upperValues{upperCommands[0],
                                                        plus[reactiveCurrentFilterState],
                                                        initialP + optional(inputs, 3)};
                const std::array<double, 3> lowerValues{lowerCommands[0],
                                                        minus[reactiveCurrentFilterState],
                                                        initialP + optional(inputs, 3)};
                assign(upperValues, lowerValues, locations.diffOffset + stateIndex, false);
            }
        }
    }
    for (std::size_t inputIndex = 0; inputIndex < inputs.size() && inputIndex < inputLocs.size();
         ++inputIndex) {
        if (inputLocs[inputIndex] == kNullLocation || inputs[inputIndex] == kNullVal) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[inputIndex] += step;
        minus[inputIndex] -= step;
        if (hasAlgebraic(sMode)) {
            const auto upperCommands = commands(plus, locations.diffStateLoc);
            const auto lowerCommands = commands(minus, locations.diffStateLoc);
            const std::array<double, 3> upperValues{
                upperCommands[0],
                locations.diffStateLoc[reactiveCurrentFilterState],
                initialP + optional(plus, 3)};
            const std::array<double, 3> lowerValues{
                lowerCommands[0],
                locations.diffStateLoc[reactiveCurrentFilterState],
                initialP + optional(minus, 3)};
            assign(upperValues, lowerValues, inputLocs[inputIndex], false);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t stateIndex = 0; stateIndex < 5; ++stateIndex) {
        auto plus = std::array<double, 5>{locations.diffStateLoc[0],
                                          locations.diffStateLoc[1],
                                          locations.diffStateLoc[2],
                                          locations.diffStateLoc[3],
                                          locations.diffStateLoc[4]};
        auto minus = plus;
        plus[stateIndex] += step;
        minus[stateIndex] -= step;
        const auto upperRates = rates(inputs, plus.data());
        const auto lowerRates = rates(inputs, minus.data());
        for (index_t rowIndex = 0; rowIndex < 5; ++rowIndex) {
            matrixData.assign(locations.diffOffset + rowIndex,
                              locations.diffOffset + stateIndex,
                              ((upperRates[rowIndex] - lowerRates[rowIndex]) / (2 * step)) -
                                  (rowIndex == stateIndex ? stateData.cj : 0.0));
        }
    }
    for (std::size_t inputIndex = 0; inputIndex < inputs.size() && inputIndex < inputLocs.size();
         ++inputIndex) {
        if (inputLocs[inputIndex] == kNullLocation || inputs[inputIndex] == kNullVal) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[inputIndex] += step;
        minus[inputIndex] -= step;
        const auto upperRates = rates(plus, locations.diffStateLoc);
        const auto lowerRates = rates(minus, locations.diffStateLoc);
        for (index_t rowIndex = 0; rowIndex < 5; ++rowIndex) {
            matrixData.assign(locations.diffOffset + rowIndex,
                              inputLocs[inputIndex],
                              (upperRates[rowIndex] - lowerRates[rowIndex]) / (2 * step));
        }
    }
}

void WT4E1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    const double timeStep = time - prevTime;
    if (timeStep < 0) {
        throw InvalidParameterValue("WT4E1 timestep precedes current time");
    }
    const auto stateRates = rates(inputs, m_state.data());
    for (index_t stateIndex = 0; stateIndex < 5; ++stateIndex) {
        m_state[3 + stateIndex] += timeStep * stateRates[stateIndex];
    }
    const auto commandValues = commands(inputs, m_state.data() + 3);
    m_state[0] = commandValues[0];
    m_state[1] = m_state[3 + reactiveCurrentFilterState];
    m_state[2] = initialP + optional(inputs, 3);
    prevTime = time;
    (void)sMode;
}
IOdata WT4E1::getOutputs(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    (void)inputs;
    return {locations.algStateLoc[0], locations.algStateLoc[1], locations.algStateLoc[2]};
}
double WT4E1::getOutput(const IOdata& inputs,
                        const StateData& stateData,
                        const SolverMode& sMode,
                        index_t outputNum) const
{
    return getOutputs(inputs, stateData, sMode).at(static_cast<std::size_t>(outputNum));
}
index_t WT4E1::getOutputLoc(const SolverMode& sMode, index_t outputNum) const
{
    return offsets.getAlgOffset(sMode) + outputNum;
}
void WT4E1::outputPartialDerivatives(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    for (index_t outputIndex = 0; outputIndex < 3; ++outputIndex) {
        matrixData.assign(outputIndex, alg + outputIndex, 1.0);
    }
    (void)inputs;
    (void)stateData;
}
stringVec WT4E1::localStateNames() const
{
    return {"Ipcmd", "Iqcmd", "Pord", "Vf", "Pf", "Pint", "Qint", "Iqfilter"};
}
}  // namespace griddyn
