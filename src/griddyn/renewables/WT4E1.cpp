/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WT4E1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
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
    constexpr index_t vf = 0, pf = 1, pInt = 2, qInt = 3, iqState = 4;
    double optional(const IOdata& in, std::size_t index)
    {
        return in.size() > index && in[index] != kNullVal ? in[index] : 0.0;
    }
    int flag(double value)
    {
        if (value != 0.0 && value != 1.0) {
            throw InvalidParameterValue("WT4E1 flag must be zero or one");
        }
        return static_cast<int>(value);
    }
}  // namespace

WT4E1::WT4E1(const std::string& name): RenewableComponent(name)
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
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "pfaflg") {
        pfFast = flag(val);
    } else if (key == "varflg") {
        windVar = flag(val);
    } else if (key == "pqflag") {
        pqPriority = flag(val);
    } else if (key == "pssematch") {
        psseMatch = flag(val);
    } else if (key == "tfv") {
        Tfv = val;
    } else if (key == "kpv") {
        Kpv = val;
    } else if (key == "kiv") {
        Kiv = val;
    } else if (key == "kpp") {
        Kpp = val;
    } else if (key == "kip") {
        Kip = val;
    } else if (key == "kf") {
        Kf = val;
    } else if (key == "tf") {
        Tf = val;
    } else if (key == "qmx") {
        Qmax = val;
    } else if (key == "qmn") {
        Qmin = val;
    } else if (key == "ipmax") {
        Ipmax = val;
    } else if (key == "trv") {
        Trv = val;
    } else if (key == "dpmx") {
        dPmax = val;
    } else if (key == "dpmn") {
        dPmin = val;
    } else if (key == "tpower") {
        Tpower = val;
    } else if (key == "kqi") {
        Kqi = val;
    } else if (key == "vmincl") {
        Vmincl = val;
    } else if (key == "vmaxcl") {
        Vmaxcl = val;
    } else if (key == "kvi") {
        Kvi = val;
    } else if (key == "tv") {
        Tv = val;
    } else if (key == "tp") {
        Tp = val;
    } else if (key == "imaxtd") {
        Imax = val;
    } else if (key == "iphl") {
        Iphl = val;
    } else if (key == "iqhl") {
        Iqhl = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double WT4E1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "pfaflg") {
        return pfFast;
    }
    if (key == "varflg") {
        return windVar;
    }
    if (key == "pqflag") {
        return pqPriority;
    }
    if (key == "pssematch") {
        return psseMatch;
    }
    if (key == "tfv") {
        return Tfv;
    }
    if (key == "kpv") {
        return Kpv;
    }
    if (key == "kiv") {
        return Kiv;
    }
    if (key == "kpp") {
        return Kpp;
    }
    if (key == "kip") {
        return Kip;
    }
    if (key == "kf") {
        return Kf;
    }
    if (key == "tf") {
        return Tf;
    }
    if (key == "qmx") {
        return Qmax;
    }
    if (key == "qmn") {
        return Qmin;
    }
    if (key == "ipmax") {
        return Ipmax;
    }
    if (key == "trv") {
        return Trv;
    }
    if (key == "dpmx") {
        return dPmax;
    }
    if (key == "dpmn") {
        return dPmin;
    }
    if (key == "tpower") {
        return Tpower;
    }
    if (key == "kqi") {
        return Kqi;
    }
    if (key == "vmincl") {
        return Vmincl;
    }
    if (key == "vmaxcl") {
        return Vmaxcl;
    }
    if (key == "kvi") {
        return Kvi;
    }
    if (key == "tv") {
        return Tv;
    }
    if (key == "tp") {
        return Tp;
    }
    if (key == "imaxtd") {
        return Imax;
    }
    if (key == "iphl") {
        return Iphl;
    }
    if (key == "iqhl") {
        return Iqhl;
    }
    return RenewableComponent::get(param, unitType);
}

void WT4E1::dynObjectInitializeA(CoreTime time0, std::uint32_t)
{
    const std::array<double, 25> values{Tfv,  Kpv,    Kiv,      Kpp,     Kip,   Kf,    Tf,
                                        Qmax, Qmin,   Ipmax,    Trv,     dPmax, dPmin, Tpower,
                                        Kqi,  Vmincl, Vmaxcl,   Kvi,     Tv,    Tp,    Imax,
                                        Iphl, Iqhl,   initialP, initialQ};
    if (std::any_of(values.begin(), values.end(), [](double v) { return !std::isfinite(v); }) ||
        Tfv <= 0 || Tf <= 0 || Trv <= 0 || Tpower <= 0 || Tv <= 0 || Tp <= 0 || Qmax < Qmin ||
        Ipmax <= 0 || Imax <= 0 || Iphl <= 0 || Iqhl <= 0 || Vmaxcl < Vmincl) {
        throw InvalidParameterValue("WT4E1 invalid time constant or limit");
    }
    auto& local = offsets.local().local;
    local.algSize = 3;
    local.diffSize = 5;
    local.jacSize = 64;
    prevTime = time0;
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
    const double initIp = initialP / vref, initIq = initialQ / vref;
    m_state[0] = initIp;
    m_state[1] = initIq;
    m_state[2] = initialP;
    m_state[3 + vf] = vref;
    m_state[3 + pf] = initialP;
    m_state[3 + pInt] = 0.0;
    m_state[3 + qInt] = 0.0;
    m_state[3 + iqState] = initIq;
    fieldSet = {initIp, initIq, initialP};
}

std::array<double, 2> WT4E1::commands(const IOdata& inputs, const double state[]) const
{
    const double voltage = std::max(inputs[0], 0.01);
    const double pReference = initialP + optional(inputs, 3);
    const double qReference = initialQ + optional(inputs, 4);
    const double pOrder = std::clamp(pReference + Kpp * (pReference - state[pf]) + state[pInt],
                                     dPmin + initialP,
                                     dPmax + initialP);
    double ip = std::clamp(pOrder / voltage, -Ipmax, Ipmax);
    const double qOrder =
        std::clamp(qReference + Kpv * (vref - state[vf]) + state[qInt], Qmin, Qmax);
    double iq = std::clamp(qOrder / voltage, -Iqhl, Iqhl);
    if (pqPriority == 1) {
        iq = std::clamp(iq,
                        -std::sqrt(std::max(0.0, Imax * Imax - ip * ip)),
                        std::sqrt(std::max(0.0, Imax * Imax - ip * ip)));
    } else {
        ip = std::clamp(ip,
                        -std::sqrt(std::max(0.0, Imax * Imax - iq * iq)),
                        std::sqrt(std::max(0.0, Imax * Imax - iq * iq)));
    }
    return {ip, iq};
}

std::array<double, 5> WT4E1::rates(const IOdata& inputs, const double state[]) const
{
    const auto command = commands(inputs, state);
    const double p = inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : initialP;
    const double q = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : initialQ;
    const double pError = (initialP + optional(inputs, 3)) - state[pf];
    const double qError = (initialQ + optional(inputs, 4)) - q;
    return {(inputs[0] - state[vf]) / Trv,
            (p - state[pf]) / Tpower,
            std::clamp(Kip * pError, dPmin, dPmax),
            Kiv * (vref - state[vf]) + Kqi * qError,
            (command[1] - state[iqState]) / Tiq};
}

void WT4E1::derivative(const IOdata& inputs,
                       const StateData& stateData,
                       double deriv[],
                       const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto r = rates(inputs, loc.diffStateLoc);
    for (index_t i = 0; i < 5; ++i) {
        loc.destDiffLoc[i] = r[i];
    }
}
void WT4E1::residual(const IOdata& inputs,
                     const StateData& stateData,
                     double resid[],
                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto c = commands(inputs, loc.diffStateLoc);
        loc.destLoc[0] = c[0] - loc.algStateLoc[0];
        loc.destLoc[1] = loc.diffStateLoc[iqState] - loc.algStateLoc[1];
        loc.destLoc[2] = initialP + optional(inputs, 3) - loc.algStateLoc[2];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t i = 0; i < 5; ++i) {
            loc.destDiffLoc[i] -= loc.dstateLoc[i];
        }
    }
}
void WT4E1::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateData,
                            double update[],
                            const SolverMode& sMode,
                            double)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const auto c = commands(inputs, loc.diffStateLoc);
    loc.destLoc[0] = c[0];
    loc.destLoc[1] = loc.diffStateLoc[iqState];
    loc.destLoc[2] = initialP + optional(inputs, 3);
}

void WT4E1::jacobianElements(const IOdata& inputs,
                             const StateData& stateData,
                             MatrixData<double>& matrixData,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    constexpr double h = 1e-6;
    const auto assign = [&](const auto& upper, const auto& lower, index_t col, bool differential) {
        for (index_t row = 0; row < (differential ? 5 : 3); ++row) {
            matrixData.assign(differential ? loc.diffOffset + row : loc.algOffset + row,
                              col,
                              (upper[row] - lower[row]) / (2 * h));
        }
    };
    if (hasAlgebraic(sMode)) {
        for (index_t i = 0; i < 3; ++i) {
            matrixData.assign(loc.algOffset + i, loc.algOffset + i, -1.0);
        }
        for (index_t c = 0; c < 5; ++c) {
            auto plus = std::array<double, 5>{loc.diffStateLoc[0],
                                              loc.diffStateLoc[1],
                                              loc.diffStateLoc[2],
                                              loc.diffStateLoc[3],
                                              loc.diffStateLoc[4]};
            auto minus = plus;
            plus[c] += h;
            minus[c] -= h;
            const auto cp = commands(inputs, plus.data());
            const auto cm = commands(inputs, minus.data());
            std::array<double, 3> up{cp[0], plus[iqState], initialP + optional(inputs, 3)},
                down{cm[0], minus[iqState], initialP + optional(inputs, 3)};
            assign(up, down, loc.diffOffset + c, false);
        }
    }
    for (std::size_t c = 0; c < inputs.size() && c < inputLocs.size(); ++c) {
        if (inputLocs[c] == kNullLocation || inputs[c] == kNullVal) {
            continue;
        }
        auto plus = inputs, minus = inputs;
        plus[c] += h;
        minus[c] -= h;
        if (hasAlgebraic(sMode)) {
            const auto cp = commands(plus, loc.diffStateLoc);
            const auto cm = commands(minus, loc.diffStateLoc);
            std::array<double, 3> up{cp[0],
                                     loc.diffStateLoc[iqState],
                                     initialP + optional(plus, 3)},
                down{cm[0], loc.diffStateLoc[iqState], initialP + optional(minus, 3)};
            assign(up, down, inputLocs[c], false);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t c = 0; c < 5; ++c) {
        auto plus = std::array<double, 5>{loc.diffStateLoc[0],
                                          loc.diffStateLoc[1],
                                          loc.diffStateLoc[2],
                                          loc.diffStateLoc[3],
                                          loc.diffStateLoc[4]};
        auto minus = plus;
        plus[c] += h;
        minus[c] -= h;
        const auto up = rates(inputs, plus.data()), down = rates(inputs, minus.data());
        for (index_t row = 0; row < 5; ++row) {
            matrixData.assign(loc.diffOffset + row,
                              loc.diffOffset + c,
                              (up[row] - down[row]) / (2 * h) - (row == c ? stateData.cj : 0.0));
        }
    }
    for (std::size_t c = 0; c < inputs.size() && c < inputLocs.size(); ++c) {
        if (inputLocs[c] == kNullLocation || inputs[c] == kNullVal) {
            continue;
        }
        auto plus = inputs, minus = inputs;
        plus[c] += h;
        minus[c] -= h;
        const auto up = rates(plus, loc.diffStateLoc), down = rates(minus, loc.diffStateLoc);
        for (index_t row = 0; row < 5; ++row) {
            matrixData.assign(loc.diffOffset + row, inputLocs[c], (up[row] - down[row]) / (2 * h));
        }
    }
}

void WT4E1::timestep(CoreTime time, const IOdata& inputs, const SolverMode&)
{
    const double dt = time - prevTime;
    if (dt < 0) {
        throw InvalidParameterValue("WT4E1 timestep precedes current time");
    }
    const auto r = rates(inputs, m_state.data());
    for (index_t i = 0; i < 5; ++i)
        m_state[3 + i] += dt * r[i];
    const auto c = commands(inputs, m_state.data() + 3);
    m_state[0] = c[0];
    m_state[1] = m_state[3 + iqState];
    m_state[2] = initialP + optional(inputs, 3);
    prevTime = time;
}
IOdata WT4E1::getOutputs(const IOdata&, const StateData& stateData, const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[0], loc.algStateLoc[1], loc.algStateLoc[2]};
}
double WT4E1::getOutput(const IOdata&,
                        const StateData& stateData,
                        const SolverMode& sMode,
                        index_t index) const
{
    return getOutputs({}, stateData, sMode).at(static_cast<std::size_t>(index));
}
index_t WT4E1::getOutputLoc(const SolverMode& sMode, index_t index) const
{
    return offsets.getAlgOffset(sMode) + index;
}
void WT4E1::outputPartialDerivatives(const IOdata&,
                                     const StateData&,
                                     MatrixData<double>& matrixData,
                                     const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    for (index_t i = 0; i < 3; ++i) {
        matrixData.assign(i, alg + i, 1.0);
    }
}
stringVec WT4E1::localStateNames() const
{
    return {"Ipcmd", "Iqcmd", "Pord", "Vf", "Pf", "Pint", "Qint", "Iqfilter"};
}
}  // namespace griddyn
