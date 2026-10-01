/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WT3E1.h"

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
    constexpr index_t vf = 0, pf = 1, pInt = 2, qInt = 3;
    double optional(const IOdata& input, std::size_t index)
    {
        return input.size() > index && input[index] != kNullVal ? input[index] : 0.0;
    }
}  // namespace
WT3E1::WT3E1(const std::string& name): RenewableComponent(name)
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
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "varflg") {
        if (value != -1 && value != 0 && value != 1) {
            throw InvalidParameterValue("WT3E1 VARFLG must be -1, 0, or 1");
        }
        varFlag = static_cast<int>(value);
    } else if (key == "vlrflg") {
        if (value < 0 || value > 2 || std::floor(value) != value) {
            throw InvalidParameterValue("WT3E1 VLRFLG must be 0, 1, or 2");
        }
        vlrFlag = static_cast<int>(value);
    } else if (key == "tfv") {
        Tfv = value;
    } else if (key == "kpv") {
        Kpv = value;
    } else if (key == "kiv") {
        Kiv = value;
    } else if (key == "xc") {
        Xc = value;
    } else if (key == "tfp") {
        Tfp = value;
    } else if (key == "kpp") {
        Kpp = value;
    } else if (key == "kip") {
        Kip = value;
    } else if (key == "pmx") {
        Pmax = value;
    } else if (key == "pmn") {
        Pmin = value;
    } else if (key == "qmx") {
        Qmax = value;
    } else if (key == "qmn") {
        Qmin = value;
    } else if (key == "ipmax") {
        Ipmax = value;
    } else if (key == "trv") {
        Trv = value;
    } else if (key == "rpmx") {
        Rpmax = value;
    } else if (key == "rpmn") {
        Rpmin = value;
    } else if (key == "tpower") {
        Tpower = value;
    } else if (key == "kqi") {
        Kqi = value;
    } else if (key == "vmincl") {
        Vmincl = value;
    } else if (key == "vmaxcl") {
        Vmaxcl = value;
    } else if (key == "kqv") {
        Kqv = value;
    } else if (key == "xiqmin") {
        Xiqmin = value;
    } else if (key == "xiqmax") {
        Xiqmax = value;
    } else if (key == "tv") {
        Tv = value;
    } else if (key == "tp") {
        Tp = value;
    } else if (key == "fn") {
        Fn = value;
    } else if (key == "wpmin") {
        Wpmin = value;
    } else if (key == "wp20") {
        Wp20 = value;
    } else if (key == "wp40") {
        Wp40 = value;
    } else if (key == "wp60") {
        Wp60 = value;
    } else if (key == "pminspeed") {
        PminSpeed = value;
    } else if (key == "wp100") {
        Wp100 = value;
    } else {
        RenewableComponent::set(param, value, unitType);
    }
}
double WT3E1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "varflg") {
        return varFlag;
    }
    if (key == "vlrflg") {
        return vlrFlag;
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
    if (key == "xc") {
        return Xc;
    }
    if (key == "tfp") {
        return Tfp;
    }
    if (key == "kpp") {
        return Kpp;
    }
    if (key == "kip") {
        return Kip;
    }
    if (key == "pmx") {
        return Pmax;
    }
    if (key == "pmn") {
        return Pmin;
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
    if (key == "rpmx") {
        return Rpmax;
    }
    if (key == "rpmn") {
        return Rpmin;
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
    if (key == "kqv") {
        return Kqv;
    }
    if (key == "xiqmin") {
        return Xiqmin;
    }
    if (key == "xiqmax") {
        return Xiqmax;
    }
    if (key == "tv") {
        return Tv;
    }
    if (key == "tp") {
        return Tp;
    }
    if (key == "fn") {
        return Fn;
    }
    if (key == "wpmin") {
        return Wpmin;
    }
    if (key == "wp20") {
        return Wp20;
    }
    if (key == "wp40") {
        return Wp40;
    }
    if (key == "wp60") {
        return Wp60;
    }
    if (key == "pminspeed") {
        return PminSpeed;
    }
    if (key == "wp100") {
        return Wp100;
    }
    return RenewableComponent::get(param, unitType);
}
void WT3E1::dynObjectInitializeA(CoreTime time0, std::uint32_t)
{
    const std::array<double, 31> values{Tfv,    Kpv,       Kiv,  Xc,     Tfp,    Kpp,  Kip,
                                        Pmax,   Pmin,      Qmax, Qmin,   Ipmax,  Trv,  Rpmax,
                                        Rpmin,  Tpower,    Kqi,  Vmincl, Vmaxcl, Kqv,  Xiqmin,
                                        Xiqmax, Tv,        Tp,   Fn,     Wpmin,  Wp20, Wp40,
                                        Wp60,   PminSpeed, Wp100};
    if (std::any_of(values.begin(), values.end(), [](double v) { return !std::isfinite(v); }) ||
        Tfv <= 0 || Tfp <= 0 || Trv <= 0 || Tpower <= 0 || Tv <= 0 || Tp <= 0 || Pmax < Pmin ||
        Qmax < Qmin || Ipmax <= 0 || Vmaxcl < Vmincl || Xiqmax < Xiqmin || Fn <= 0) {
        throw InvalidParameterValue("WT3E1 invalid time constant or limit");
    }
    auto& local = offsets.local().local;
    local.algSize = 4;
    local.diffSize = 4;
    local.jacSize = 64;
    prevTime = time0;
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
    m_state[0] = initialIp;
    m_state[1] = initialEq;
    m_state[2] = initialP;
    m_state[3] = 1.0;
    m_state[4 + vf] = vref;
    m_state[4 + pf] = initialP;
    m_state[4 + pInt] = 0.0;
    m_state[4 + qInt] = 0.0;
    fieldSet = {initialIp, initialEq, initialP, 1.0};
}
std::array<double, 3> WT3E1::commands(const IOdata& inputs, const double state[]) const
{
    const double voltage = std::max(inputs[0], .01);
    const double pref = initialP + optional(inputs, 4);
    const double qref = initialQ + optional(inputs, 5);
    const double porder = std::clamp(pref + Kpp * (pref - state[pf]) + state[pInt], Pmin, Pmax);
    const double ip = std::clamp(porder / voltage, -Ipmax, Ipmax);
    const double qerror =
        qref - (inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : initialQ);
    const double eq = vlrFlag == 0 ?
        initialEq :
        std::clamp(initialEq + Kpv * (vref - state[vf]) + state[qInt] + Kqi * qerror,
                   Xiqmin - 1.0,
                   Xiqmax + 1.0);
    return {ip, eq, porder};
}
std::array<double, 4> WT3E1::rates(const IOdata& inputs, const double state[]) const
{
    const double p = inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : initialP;
    const double q = inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : initialQ;
    const double pref = initialP + optional(inputs, 4);
    const double qref = initialQ + optional(inputs, 5);
    return {(inputs[0] - state[vf]) / Trv,
            (p - state[pf]) / Tpower,
            std::clamp(Kip * (pref - state[pf]), Rpmin, Rpmax),
            Kiv * (vref - state[vf]) + Kqv * (qref - q)};
}
void WT3E1::derivative(const IOdata& i, const StateData& s, double* d, const SolverMode& m)
{
    if (!hasDifferential(m)) {
        return;
    }
    const auto l = offsets.getLocations(s, d, m, this);
    const auto r = rates(i, l.diffStateLoc);
    for (index_t n = 0; n < 4; ++n) {
        l.destDiffLoc[n] = r[n];
    }
}
void WT3E1::residual(const IOdata& i, const StateData& s, double* r, const SolverMode& m)
{
    const auto l = offsets.getLocations(s, r, m, this);
    if (hasAlgebraic(m)) {
        const auto c = commands(i, l.diffStateLoc);
        l.destLoc[0] = c[0] - l.algStateLoc[0];
        l.destLoc[1] = c[1] - l.algStateLoc[1];
        l.destLoc[2] = c[2] - l.algStateLoc[2];
        l.destLoc[3] = 1.0 - l.algStateLoc[3];
    }
    if (hasDifferential(m)) {
        derivative(i, s, r, m);
        for (index_t n = 0; n < 4; ++n) {
            l.destDiffLoc[n] -= l.dstateLoc[n];
        }
    }
}
void WT3E1::algebraicUpdate(const IOdata& i,
                            const StateData& s,
                            double* u,
                            const SolverMode& m,
                            double)
{
    if (!hasAlgebraic(m)) {
        return;
    }
    const auto l = offsets.getLocations(s, u, m, this);
    const auto c = commands(i, l.diffStateLoc);
    l.destLoc[0] = c[0];
    l.destLoc[1] = c[1];
    l.destLoc[2] = c[2];
    l.destLoc[3] = 1.0;
}
void WT3E1::jacobianElements(const IOdata& i,
                             const StateData& s,
                             MatrixData<double>& m,
                             const IOlocs& locs,
                             const SolverMode& mode)
{
    const auto l = offsets.getLocations(s, mode, this);
    constexpr double h = 1e-6;
    if (hasAlgebraic(mode)) {
        for (index_t n = 0; n < 4; ++n) {
            m.assign(l.algOffset + n, l.algOffset + n, -1.0);
        }
        for (index_t c = 0; c < 4; ++c) {
            auto plus = std::array<double, 4>{l.diffStateLoc[0],
                                              l.diffStateLoc[1],
                                              l.diffStateLoc[2],
                                              l.diffStateLoc[3]},
                 minus = plus;
            plus[c] += h;
            minus[c] -= h;
            const auto up = commands(i, plus.data()), down = commands(i, minus.data());
            for (index_t row = 0; row < 3; ++row) {
                m.assign(l.algOffset + row, l.diffOffset + c, (up[row] - down[row]) / (2 * h));
            }
        }
    }
    for (std::size_t c = 0; c < i.size() && c < locs.size(); ++c) {
        if (locs[c] == kNullLocation || i[c] == kNullVal) {
            continue;
        }
        auto plus = i, minus = i;
        plus[c] += h;
        minus[c] -= h;
        if (hasAlgebraic(mode)) {
            const auto up = commands(plus, l.diffStateLoc), down = commands(minus, l.diffStateLoc);
            for (index_t row = 0; row < 3; ++row) {
                m.assign(l.algOffset + row, locs[c], (up[row] - down[row]) / (2 * h));
            }
        }
    }
    if (!hasDifferential(mode)) {
        return;
    }
    for (index_t c = 0; c < 4; ++c) {
        auto plus = std::array<double, 4>{l.diffStateLoc[0],
                                          l.diffStateLoc[1],
                                          l.diffStateLoc[2],
                                          l.diffStateLoc[3]},
             minus = plus;
        plus[c] += h;
        minus[c] -= h;
        const auto up = rates(i, plus.data()), down = rates(i, minus.data());
        for (index_t row = 0; row < 4; ++row) {
            m.assign(l.diffOffset + row,
                     l.diffOffset + c,
                     (up[row] - down[row]) / (2 * h) - (row == c ? s.cj : 0.0));
        }
    }
    for (std::size_t c = 0; c < i.size() && c < locs.size(); ++c) {
        if (locs[c] == kNullLocation || i[c] == kNullVal) {
            continue;
        }
        auto plus = i, minus = i;
        plus[c] += h;
        minus[c] -= h;
        const auto up = rates(plus, l.diffStateLoc), down = rates(minus, l.diffStateLoc);
        for (index_t row = 0; row < 4; ++row) {
            m.assign(l.diffOffset + row, locs[c], (up[row] - down[row]) / (2 * h));
        }
    }
}
void WT3E1::timestep(CoreTime t, const IOdata& i, const SolverMode&)
{
    const double dt = t - prevTime;
    if (dt < 0) {
        throw InvalidParameterValue("WT3E1 timestep precedes current time");
    }
    const auto r = rates(i, m_state.data() + 4);
    for (index_t n = 0; n < 4; ++n)
        m_state[4 + n] += dt * r[n];
    const auto c = commands(i, m_state.data() + 4);
    m_state[0] = c[0];
    m_state[1] = c[1];
    m_state[2] = c[2];
    m_state[3] = 1.0;
    prevTime = t;
}
IOdata WT3E1::getOutputs(const IOdata&, const StateData& s, const SolverMode& m) const
{
    const auto l = offsets.getLocations(s, m, this);
    return {l.algStateLoc[0], l.algStateLoc[1], l.algStateLoc[2], l.algStateLoc[3]};
}
double WT3E1::getOutput(const IOdata&, const StateData& s, const SolverMode& m, index_t n) const
{
    return getOutputs({}, s, m).at(static_cast<std::size_t>(n));
}
index_t WT3E1::getOutputLoc(const SolverMode& m, index_t n) const
{
    return offsets.getAlgOffset(m) + n;
}
void WT3E1::outputPartialDerivatives(const IOdata&,
                                     const StateData&,
                                     MatrixData<double>& m,
                                     const SolverMode& mode)
{
    const auto a = offsets.getAlgOffset(mode);
    for (index_t n = 0; n < 4; ++n) {
        m.assign(n, a + n, 1.0);
    }
}
stringVec WT3E1::localStateNames() const
{
    return {"Ipcmd", "Eqcmd", "Pord", "wref", "Vf", "Pf", "Pint", "Qint"};
}
}  // namespace griddyn
