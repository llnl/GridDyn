/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WT3E1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include "utilities/logger.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 8> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::electricalPower, .ioIndex = 1, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 2, .base = RenewableBase::machine},
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 3, .required = false},
        {.signal = RenewableSignal::activeReferenceIncrement,
         .ioIndex = 4,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveReferenceIncrement,
         .ioIndex = 5,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::initialActiveCurrentCommand,
         .ioIndex = 6,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::initialReactiveVoltageCommand,
         .ioIndex = 7,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 4> outputPortMap{{
        {.signal = RenewableSignal::activeCurrentCommand,
         .ioIndex = 0,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactiveVoltageCommand,
         .ioIndex = 1,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::orderedPower, .ioIndex = 2, .base = RenewableBase::machine},
        {.signal = RenewableSignal::speedReference, .ioIndex = 3},
    }};
    constexpr index_t voltageFilterState = 0;
    constexpr index_t powerFilterState = 1;
    constexpr index_t powerIntegratorState = 2;
    constexpr index_t reactiveIntegratorState = 3;
    double optional(const IOdata& inputData, std::size_t index)
    {
        return inputData.size() > index && inputData[index] != kNullVal ? inputData[index] : 0.0;
    }
}  // namespace
const ModelDescription& WT3E1::getModelDescription() const
{
    static const auto description = ModelDescription::define<WT3E1>(
        "WT3E1",
        &RenewableComponent::getModelDescription(),
        {parameter({"varflg"},
                   &WT3E1::varFlag,
                   units::one,
                   "Var flag",
                   [](int value) { return value >= -1 && value <= 1; }),
         parameter({"vlrflg"},
                   &WT3E1::vlrFlag,
                   units::one,
                   "Vlr flag",
                   [](int value) { return value >= 0 && value <= 2; }),
         parameter({"tfv"}, &WT3E1::Tfv, units::second, "Tfv"),
         parameter({"kpv"}, &WT3E1::Kpv, units::one, "Kpv"),
         parameter({"kiv"}, &WT3E1::Kiv, units::one, "Kiv"),
         parameter({"xc"}, &WT3E1::Xc, units::one, "Xc"),
         parameter({"tfp"}, &WT3E1::Tfp, units::second, "Tfp"),
         parameter({"kpp"}, &WT3E1::Kpp, units::one, "Kpp"),
         parameter({"kip"}, &WT3E1::Kip, units::one, "Kip"),
         parameter({"pmx"}, &WT3E1::Pmax, units::one, "Pmax"),
         parameter({"pmn"}, &WT3E1::Pmin, units::one, "Pmin"),
         parameter({"qmx"}, &WT3E1::Qmax, units::one, "Qmax"),
         parameter({"qmn"}, &WT3E1::Qmin, units::one, "Qmin"),
         parameter({"ipmax"}, &WT3E1::Ipmax, units::one, "Ipmax"),
         parameter({"trv"}, &WT3E1::Trv, units::second, "Trv"),
         parameter({"rpmx"}, &WT3E1::Rpmax, units::one, "Rpmax"),
         parameter({"rpmn"}, &WT3E1::Rpmin, units::one, "Rpmin"),
         parameter({"tpower"}, &WT3E1::Tpower, units::second, "Tpower"),
         parameter({"kqi"}, &WT3E1::Kqi, units::one, "Kqi"),
         parameter({"vmincl"}, &WT3E1::Vmincl, units::one, "Vmincl"),
         parameter({"vmaxcl"}, &WT3E1::Vmaxcl, units::one, "Vmaxcl"),
         parameter({"kqv"}, &WT3E1::Kqv, units::one, "Kqv"),
         parameter({"xiqmin"}, &WT3E1::Xiqmin, units::one, "Xiqmin"),
         parameter({"xiqmax"}, &WT3E1::Xiqmax, units::one, "Xiqmax"),
         parameter({"tv"}, &WT3E1::Tv, units::second, "Tv"),
         parameter({"tp"}, &WT3E1::Tp, units::second, "Tp"),
         parameter({"fn"}, &WT3E1::Fn, units::Hz, "Fn"),
         parameter({"wpmin"}, &WT3E1::Wpmin, units::one, "Wpmin"),
         parameter({"wp20"}, &WT3E1::Wp20, units::one, "Wp20"),
         parameter({"wp40"}, &WT3E1::Wp40, units::one, "Wp40"),
         parameter({"wp60"}, &WT3E1::Wp60, units::one, "Wp60"),
         parameter({"pminspeed"}, &WT3E1::PminSpeed, units::one, "Pmin speed"),
         parameter({"wp100"}, &WT3E1::Wp100, units::one, "Wp100"),
         state("Ipcmd", units::one, "Solver variable Ipcmd when active"),
         state("Eqcmd", units::one, "Solver variable Eqcmd when active"),
         state("Pord", units::one, "Solver variable Pord when active"),
         state("wref", units::one, "Solver variable wref when active"),
         state("Vf", units::one, "Solver variable Vf when active"),
         state("Pf", units::one, "Solver variable Pf when active"),
         state("Pint", units::one, "Solver variable Pint when active"),
         state("Qint", units::one, "Solver variable Qint when active"),
         localState("initialP", &WT3E1::initialP, "Initial p"),
         localState("initialQ", &WT3E1::initialQ, "Initial q"),
         localState("vref", &WT3E1::vref, "Vref"),
         localState("initialEq", &WT3E1::initialEq, "Initial eq")});
    return description;
}

void WT3E1::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = WT3E1::getModelDescription();
    getParamString<WT3E1, RenewableComponent>(this,
                                              pstr,
                                              description.numericNames(),
                                              description.stringNames(),
                                              description.flagNames(),
                                              pstype);
}

WT3E1::WT3E1(const std::string& objName): RenewableComponent(objName)
{
    m_inputSize = 8;
    m_outputSize = 4;
}
CoreObject* WT3E1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<WT3E1, RenewableComponent>(this, obj);
    if (out == nullptr) {
        return obj;
    }
    out->varFlag = varFlag;
    out->vlrFlag = vlrFlag;
    out->Tfv = Tfv;
    out->Kpv = Kpv;
    out->Kiv = Kiv;
    out->Xc = Xc;
    out->Tfp = Tfp;
    out->Kpp = Kpp;
    out->Kip = Kip;
    out->Pmax = Pmax;
    out->Pmin = Pmin;
    out->Qmax = Qmax;
    out->Qmin = Qmin;
    out->Ipmax = Ipmax;
    out->Trv = Trv;
    out->Rpmax = Rpmax;
    out->Rpmin = Rpmin;
    out->Tpower = Tpower;
    out->Kqi = Kqi;
    out->Vmincl = Vmincl;
    out->Vmaxcl = Vmaxcl;
    out->Kqv = Kqv;
    out->Xiqmin = Xiqmin;
    out->Xiqmax = Xiqmax;
    out->Tv = Tv;
    out->Tp = Tp;
    out->Fn = Fn;
    out->Wpmin = Wpmin;
    out->Wp20 = Wp20;
    out->Wp40 = Wp40;
    out->Wp60 = Wp60;
    out->PminSpeed = PminSpeed;
    out->Wp100 = Wp100;
    out->initialP = initialP;
    out->initialQ = initialQ;
    out->vref = vref;
    out->initialEq = initialEq;
    return out;
}
std::span<const RenewablePort> WT3E1::inputPorts() const
{
    return inputPortMap;
}
std::span<const RenewablePort> WT3E1::outputPorts() const
{
    return outputPortMap;
}
void WT3E1::set(std::string_view param, double value, units::unit unitType)
{
    if (WT3E1::getModelDescription().trySetLocal(*this, param, value, unitType)) {
        return;
    }
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key != param && WT3E1::getModelDescription().trySetLocal(*this, key, value, unitType)) {
        return;
    }
    RenewableComponent::set(param, value, unitType);
}
double WT3E1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (const auto value = WT3E1::getModelDescription().tryGetLocal(*this, key, unitType)) {
        return *value;
    }
    return RenewableComponent::get(param, unitType);
}
void WT3E1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const std::array<double, 31> values{Tfv,    Kpv,       Kiv,  Xc,     Tfp,    Kpp,  Kip,
                                        Pmax,   Pmin,      Qmax, Qmin,   Ipmax,  Trv,  Rpmax,
                                        Rpmin,  Tpower,    Kqi,  Vmincl, Vmaxcl, Kqv,  Xiqmin,
                                        Xiqmax, Tv,        Tp,   Fn,     Wpmin,  Wp20, Wp40,
                                        Wp60,   PminSpeed, Wp100};
    if (std::any_of(values.begin(),
                    values.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        Tfv <= 0 || Tfp <= 0 || Trv <= 0 || Tpower <= 0 || Tv <= 0 || Tp <= 0 || Pmax < Pmin ||
        Qmax < Qmin || Ipmax <= 0 || Vmaxcl < Vmincl || Xiqmax < Xiqmin || Fn <= 0) {
        throw InvalidParameterValue("WT3E1 invalid time constant or limit");
    }
    auto& local = offsets.local().local;
    local.algSize = 4;
    local.diffSize = 4;
    local.jacSize = 64;
    prevTime = time0;
    (void)flags;
}
void WT3E1::dynObjectInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (inputs.empty() || desiredOutput.size() < 2 || inputs[0] <= 0 || !std::isfinite(inputs[0])) {
        throw InvalidParameterValue("WT3E1 requires initial voltage and P/Q");
    }
    initialP = desiredOutput[0];
    initialQ = desiredOutput[1];
    vref = inputs[0];
    const double initialIp =
        inputs.size() > 6 && inputs[6] != kNullVal ? inputs[6] : initialP / vref;
    initialEq = inputs.size() > 7 && inputs[7] != kNullVal ? inputs[7] : 0.0;
    if (vlrFlag != 0) {
        const double lowerLimit = Xiqmin - 1.0;
        const double upperLimit = Xiqmax + 1.0;
        if (initialEq < lowerLimit) {
            const double originalLimit = Xiqmin;
            Xiqmin = initialEq + 1.0;
            logging::warning(this,
                             "WT3E1 initial Eqcmd {} is below its effective lower limit {}; "
                             "adjusting XIQMIN from {} to {}",
                             initialEq,
                             lowerLimit,
                             originalLimit,
                             Xiqmin);
        }
        if (initialEq > upperLimit) {
            const double originalLimit = Xiqmax;
            Xiqmax = initialEq - 1.0;
            logging::warning(this,
                             "WT3E1 initial Eqcmd {} is above its effective upper limit {}; "
                             "adjusting XIQMAX from {} to {}",
                             initialEq,
                             upperLimit,
                             originalLimit,
                             Xiqmax);
        }
    }
    m_state[0] = initialIp;
    m_state[1] = initialEq;
    m_state[2] = initialP;
    m_state[3] = 1.0;
    m_state[4 + voltageFilterState] = vref;
    m_state[4 + powerFilterState] = initialP;
    m_state[4 + powerIntegratorState] = 0.0;
    m_state[4 + reactiveIntegratorState] = 0.0;
    fieldSet = {initialIp, initialEq, initialP, 1.0};
}
std::array<double, 3> WT3E1::commands(const IOdata& inputs, const double state[]) const
{
    const double voltage = std::max(inputs[0], .01);
    const double pref = initialP + optional(inputs, 4);
    const double qref = initialQ + optional(inputs, 5);
    const double porder =
        std::clamp(pref + (Kpp * (pref - state[powerFilterState])) + state[powerIntegratorState],
                   Pmin,
                   Pmax);
    const double activeCurrent = std::clamp(porder / voltage, -Ipmax, Ipmax);
    const double qerror =
        qref - (inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : initialQ);
    const double reactiveVoltage = vlrFlag == 0 ?
        initialEq :
        std::clamp(initialEq + (Kpv * (vref - state[voltageFilterState])) +
                       state[reactiveIntegratorState] + (Kqi * qerror),
                   Xiqmin - 1.0,
                   Xiqmax + 1.0);
    return {activeCurrent, reactiveVoltage, porder};
}
std::array<double, 4> WT3E1::rates(const IOdata& inputs, const double state[]) const
{
    const double activePower = inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : initialP;
    const double reactivePower = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : initialQ;
    const double pref = initialP + optional(inputs, 4);
    const double qref = initialQ + optional(inputs, 5);
    return {(inputs[0] - state[voltageFilterState]) / Trv,
            (activePower - state[powerFilterState]) / Tpower,
            std::clamp(Kip * (pref - state[powerFilterState]), Rpmin, Rpmax),
            (Kiv * (vref - state[voltageFilterState])) + (Kqv * (qref - reactivePower))};
}
void WT3E1::derivative(const IOdata& inputs,
                       const StateData& stateData,
                       double* deriv,
                       const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const auto stateRates = rates(inputs, locations.diffStateLoc);
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        locations.destDiffLoc[stateIndex] = stateRates[stateIndex];
    }
}
void WT3E1::residual(const IOdata& inputs,
                     const StateData& stateData,
                     double* resid,
                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto commandValues = commands(inputs, locations.diffStateLoc);
        locations.destLoc[0] = commandValues[0] - locations.algStateLoc[0];
        locations.destLoc[1] = commandValues[1] - locations.algStateLoc[1];
        locations.destLoc[2] = commandValues[2] - locations.algStateLoc[2];
        locations.destLoc[3] = 1.0 - locations.algStateLoc[3];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
            locations.destDiffLoc[stateIndex] -= locations.dstateLoc[stateIndex];
        }
    }
}
void WT3E1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double* update,
                            const SolverMode& sMode,
                            double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    const auto commandValues = commands(inputs, locations.diffStateLoc);
    locations.destLoc[0] = commandValues[0];
    locations.destLoc[1] = commandValues[1];
    locations.destLoc[2] = commandValues[2];
    locations.destLoc[3] = 1.0;
    (void)alpha;
}
void WT3E1::jacobianElements(const IOdata& inputs,
                             const StateData& stateData,
                             MatrixData<double>& matrixData,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    constexpr double step = 1e-6;
    if (hasAlgebraic(sMode)) {
        for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
            matrixData.assign(locations.algOffset + stateIndex,
                              locations.algOffset + stateIndex,
                              -1.0);
        }
        if (hasDifferential(sMode)) {
            for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
                auto plus = std::array<double, 4>{locations.diffStateLoc[0],
                                                  locations.diffStateLoc[1],
                                                  locations.diffStateLoc[2],
                                                  locations.diffStateLoc[3]};
                auto minus = plus;
                plus[stateIndex] += step;
                minus[stateIndex] -= step;
                const auto upper = commands(inputs, plus.data());
                const auto lower = commands(inputs, minus.data());
                for (index_t row = 0; row < 3; ++row) {
                    matrixData.assign(locations.algOffset + row,
                                      locations.diffOffset + stateIndex,
                                      (upper[row] - lower[row]) / (2 * step));
                }
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
            const auto upper = commands(plus, locations.diffStateLoc);
            const auto lower = commands(minus, locations.diffStateLoc);
            for (index_t row = 0; row < 3; ++row) {
                matrixData.assign(locations.algOffset + row,
                                  inputLocs[inputIndex],
                                  (upper[row] - lower[row]) / (2 * step));
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        auto plus = std::array<double, 4>{locations.diffStateLoc[0],
                                          locations.diffStateLoc[1],
                                          locations.diffStateLoc[2],
                                          locations.diffStateLoc[3]};
        auto minus = plus;
        plus[stateIndex] += step;
        minus[stateIndex] -= step;
        const auto upper = rates(inputs, plus.data());
        const auto lower = rates(inputs, minus.data());
        for (index_t row = 0; row < 4; ++row) {
            matrixData.assign(locations.diffOffset + row,
                              locations.diffOffset + stateIndex,
                              ((upper[row] - lower[row]) / (2 * step)) -
                                  (row == stateIndex ? stateData.cj : 0.0));
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
        const auto upper = rates(plus, locations.diffStateLoc);
        const auto lower = rates(minus, locations.diffStateLoc);
        for (index_t row = 0; row < 4; ++row) {
            matrixData.assign(locations.diffOffset + row,
                              inputLocs[inputIndex],
                              (upper[row] - lower[row]) / (2 * step));
        }
    }
}
void WT3E1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    const double timeStep = time - prevTime;
    if (timeStep < 0) {
        throw InvalidParameterValue("WT3E1 timestep precedes current time");
    }
    const auto stateRates = rates(inputs, m_state.data() + 4);
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        m_state[4 + stateIndex] += timeStep * stateRates[stateIndex];
    }
    const auto commandValues = commands(inputs, m_state.data() + 4);
    m_state[0] = commandValues[0];
    m_state[1] = commandValues[1];
    m_state[2] = commandValues[2];
    m_state[3] = 1.0;
    prevTime = time;
    (void)sMode;
}
IOdata WT3E1::getOutputs(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    (void)inputs;
    return {locations.algStateLoc[0],
            locations.algStateLoc[1],
            locations.algStateLoc[2],
            locations.algStateLoc[3]};
}
double WT3E1::getOutput(const IOdata& inputs,
                        const StateData& stateData,
                        const SolverMode& sMode,
                        index_t outputNum) const
{
    return getOutputs(inputs, stateData, sMode).at(static_cast<std::size_t>(outputNum));
}
index_t WT3E1::getOutputLoc(const SolverMode& sMode, index_t outputNum) const
{
    return offsets.getAlgOffset(sMode) + outputNum;
}
void WT3E1::outputPartialDerivatives(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto algebraicOffset = offsets.getAlgOffset(sMode);
    for (index_t outputIndex = 0; outputIndex < 4; ++outputIndex) {
        matrixData.assign(outputIndex, algebraicOffset + outputIndex, 1.0);
    }
    (void)inputs;
    (void)stateData;
}
stringVec WT3E1::localStateNames() const
{
    return {"Ipcmd", "Eqcmd", "Pord", "wref", "Vf", "Pf", "Pint", "Qint"};
}
}  // namespace griddyn
