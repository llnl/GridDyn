/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DistributedConverter.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 5> inputPortsDefinition{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 1},
        {.signal = RenewableSignal::terminalFrequency, .ioIndex = 2},
        {.signal = RenewableSignal::regulationVoltage, .ioIndex = 3, .required = false},
        {.signal = RenewableSignal::activeReference,
         .ioIndex = 4,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 2> outputs{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};

    double lowerTrip(double value, double outer, double inner)
    {
        return std::clamp((value - outer) / (inner - outer), 0.0, 1.0);
    }
    double upperTrip(double value, double inner, double outer)
    {
        return std::clamp((outer - value) / (outer - inner), 0.0, 1.0);
    }
}  // namespace

DistributedConverter::DistributedConverter(Variant type, const std::string& objName):
    TerminalElectricalModel(objName), variant(type)
{
    m_inputSize = 5;
    if (variant == Variant::ev1 || variant == Variant::ev2) {
        pmn = -999.0;
    }
    if (variant == Variant::ev2) {
        pcap = 0.0;
        ddn = 1.0;
    }
}

std::span<const RenewablePort> DistributedConverter::inputPorts() const
{
    return inputPortsDefinition;
}
std::span<const RenewablePort> DistributedConverter::outputPorts() const
{
    return outputs;
}
std::string_view DistributedConverter::sourceName(RenewableSignal signal) const
{
    if (signal == RenewableSignal::terminalFrequency) {
        return frequencySensor;
    }
    if (signal == RenewableSignal::regulationVoltage) {
        return regulationBus;
    }
    return {};
}

void DistributedConverter::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("distributed converter parameter must be finite");
    }
#define SET_FIELD(name, field)                                                                     \
    if (key == (name)) {                                                                           \
        (field) = val;                                                                             \
        return;                                                                                    \
    }
    SET_FIELD("fn", fn)
    SET_FIELD("xc", xc)
    SET_FIELD("pqflag", pqflag)
    SET_FIELD("qmx", qmx)
    SET_FIELD("qmn", qmn)
    SET_FIELD("pmx", pmx)
    SET_FIELD("pmn", pmn)
    SET_FIELD("v0", v0)
    SET_FIELD("v1", v1)
    SET_FIELD("dqdv", dqdv)
    SET_FIELD("fdbd", fdbd)
    SET_FIELD("ddn", ddn)
    SET_FIELD("ialim", ialim)
    SET_FIELD("vt0", vt0)
    SET_FIELD("vt1", vt1)
    SET_FIELD("vt2", vt2)
    SET_FIELD("vt3", vt3)
    SET_FIELD("ft0", ft0)
    SET_FIELD("ft1", ft1)
    SET_FIELD("ft2", ft2)
    SET_FIELD("ft3", ft3)
    SET_FIELD("vrflag", vrflag)
    SET_FIELD("frflag", frflag)
    SET_FIELD("recflag", recflag)
    SET_FIELD("tip", tip)
    SET_FIELD("tiq", tiq)
    SET_FIELD("paux", paux)
    SET_FIELD("pext0", paux)
    SET_FIELD("pcap", pcap)
    SET_FIELD("tf", tf)
    SET_FIELD("socmin", socmin)
    SET_FIELD("socmax", socmax)
    SET_FIELD("socinit", socinit)
    SET_FIELD("en", en)
    SET_FIELD("etac", etac)
    SET_FIELD("etad", etad)
#undef SET_FIELD
    if (key == "pref" || key == "pref0") {
        pref = val;
        prefConfigured = true;
        return;
    }
    if (key == "qref" || key == "qref0") {
        qref = val;
        qrefConfigured = true;
        return;
    }
    if (key == "blocked") {
        blocked = val != 0.0;
        return;
    }
    if (key == "reset") {
        if (val != 0.0) {
            voltageLatched = false;
            frequencyLatched = false;
        }
        return;
    }
    TerminalElectricalModel::set(param, val, unitType);
}

void DistributedConverter::set(std::string_view param, std::string_view val)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "busf" || key == "frequency_sensor") {
        frequencySensor = std::string{val};
        return;
    }
    if (key == "igreg" || key == "regulation_bus") {
        regulationBus = std::string{val};
        return;
    }
    TerminalElectricalModel::set(param, val);
}

double DistributedConverter::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
#define GET_FIELD(name, field)                                                                     \
    if (key == (name)) {                                                                           \
        return (field);                                                                            \
    }
    GET_FIELD("fn", fn)
    GET_FIELD("xc", xc)
    GET_FIELD("pqflag", pqflag)
    GET_FIELD("qmx", qmx)
    GET_FIELD("qmn", qmn)
    GET_FIELD("pmx", pmx)
    GET_FIELD("pmn", pmn)
    GET_FIELD("v0", v0)
    GET_FIELD("v1", v1)
    GET_FIELD("dqdv", dqdv)
    GET_FIELD("fdbd", fdbd)
    GET_FIELD("ddn", ddn)
    GET_FIELD("ialim", ialim)
    GET_FIELD("vt0", vt0)
    GET_FIELD("vt1", vt1)
    GET_FIELD("vt2", vt2)
    GET_FIELD("vt3", vt3)
    GET_FIELD("ft0", ft0)
    GET_FIELD("ft1", ft1)
    GET_FIELD("ft2", ft2)
    GET_FIELD("ft3", ft3)
    GET_FIELD("vrflag", vrflag)
    GET_FIELD("frflag", frflag)
    GET_FIELD("recflag", recflag)
    GET_FIELD("tip", tip)
    GET_FIELD("tiq", tiq)
    GET_FIELD("pref", pref)
    GET_FIELD("pref0", pref)
    GET_FIELD("qref", qref)
    GET_FIELD("qref0", qref)
    GET_FIELD("paux", paux)
    GET_FIELD("pext0", paux)
    GET_FIELD("pcap", pcap)
    GET_FIELD("tf", tf)
    GET_FIELD("socmin", socmin)
    GET_FIELD("socmax", socmax)
    GET_FIELD("socinit", socinit)
    GET_FIELD("en", en)
    GET_FIELD("etac", etac)
    GET_FIELD("etad", etad)
#undef GET_FIELD
    if (key == "blocked") {
        return blocked ? 1.0 : 0.0;
    }
    if (key == "voltage_latched") {
        return voltageLatched ? 1.0 : 0.0;
    }
    if (key == "frequency_latched") {
        return frequencyLatched ? 1.0 : 0.0;
    }
    if (key == "soc") {
        if (variant == Variant::pv) {
            return kNullVal;
        }
        return m_state.size() > 4 ? m_state[4] : socinit;
    }
    return TerminalElectricalModel::get(param, unitType);
}

count_t DistributedConverter::stateCount() const
{
    return variant == Variant::pv ? 2 : 3;
}

void DistributedConverter::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (fn <= 0.0 || ialim <= 0.0 || tip <= 0.0 || tiq <= 0.0 || dqdv >= 0.0 || fdbd > 0.0 ||
        ddn < 0.0 || vt0 >= vt1 || vt1 > vt2 || vt2 >= vt3 || ft0 >= ft1 || ft1 > ft2 ||
        ft2 >= ft3 || v0 >= v1 || qmn > qmx || pmx < 0.0 || (pqflag != 0.0 && pqflag != 1.0) ||
        vrflag < 0.0 || vrflag > 1.0 || frflag < 0.0 || frflag > 1.0 || recflag < 0.0 ||
        recflag > 1.0 ||
        (variant != Variant::pv &&
         (en <= 0.0 || tf <= 0.0 || socmin >= socmax || socinit < socmin || socinit > socmax ||
          etac <= 0.0 || etac > 1.0 || etad <= 0.0 || etad > 1.0)) ||
        ((variant == Variant::ev1 || variant == Variant::ev2) && pmn > pmx) ||
        (variant == Variant::ev2 && (pcap < -1.0 || pcap > 1.0 || pmn > pmx * pcap))) {
        throw InvalidParameterValue("distributed converter limits, thresholds, or time constants");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = stateCount();
    local.jacSize = 32;
    local.algRoots = 8;
    systemMva = systemBasePower;
    prevTime = time0;
}

void DistributedConverter::dynObjectInitializeB(const IOdata& inputs,
                                                const IOdata& desiredOutput,
                                                IOdata& fieldSet)
{
    if (inputs.empty() || inputs[0] <= 0.0 || desiredOutput.size() < 2) {
        throw InvalidParameterValue("distributed converter initial voltage or power");
    }
    if (!prefConfigured) {
        pref = desiredOutput[0];
    }
    if (!qrefConfigured) {
        qref = desiredOutput[1];
    }
    m_state[0] = desiredOutput[0];
    m_state[1] = desiredOutput[1];
    m_state[2] = desiredOutput[0] / inputs[0];
    m_state[3] = desiredOutput[1] / inputs[0];
    if (variant != Variant::pv) {
        m_state[4] = socinit;
    }
    std::array<double, 2> power{};
    std::array<double, 3> rates{};
    evaluate(inputs, m_state.data() + 2, power, rates);
    for (index_t index = 0; index < stateCount(); ++index) {
        m_dstate_dt[2 + index] = rates[index];
    }
    fieldSet = {m_state[2], m_state[3]};
}

void DistributedConverter::updateLatches(const IOdata& inputs)
{
    if (inputs.empty()) {
        return;
    }
    const double measuredV = inputs.size() > 3 && inputs[3] != kNullVal ? inputs[3] : inputs[0];
    const double fHz = fn * (inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : 1.0);
    if (measuredV <= vt0 || measuredV >= vt3) {
        voltageLatched = true;
    }
    if (fHz <= ft0 || fHz >= ft3) {
        frequencyLatched = true;
    }
    if (vrflag == 1.0 && measuredV >= vt1 && measuredV <= vt2) {
        voltageLatched = false;
    }
    if (frflag == 1.0 && fHz >= ft1 && fHz <= ft2) {
        frequencyLatched = false;
    }
}

void DistributedConverter::evaluate(const IOdata& inputs,
                                    const double state[],
                                    std::array<double, 2>& power,
                                    std::array<double, 3>& rates) const
{
    const double voltage = inputs[0];
    const double angle = inputs.size() > 1 && inputs[1] != kNullVal ? inputs[1] : 0.0;
    const double frequencyHz = fn * (inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : 1.0);
    const double regulationVoltage =
        inputs.size() > 3 && inputs[3] != kNullVal ? inputs[3] : voltage;
    const double activeCurrent = state[0];
    const double reactiveCurrent = state[1];
    power = {voltage * activeCurrent, voltage * reactiveCurrent};
    const double vcomp = std::hypot((regulationVoltage * std::cos(angle)) - (xc * reactiveCurrent),
                                    (regulationVoltage * std::sin(angle)) + (xc * activeCurrent));
    double voltageDeviation = 0.0;
    if (vcomp < v0) {
        voltageDeviation = vcomp - v0;
    } else if (vcomp > v1) {
        voltageDeviation = vcomp - v1;
    }
    const double qdroop = dqdv * voltageDeviation;
    const double pDroop = ddn *
        (std::abs(fn - frequencyHz) > -fdbd ?
             std::copysign(std::abs(fn - frequencyHz) + fdbd, fn - frequencyHz) :
             0.0);
    const double upperPower = variant == Variant::ev2 ? pmx * pcap : pmx;
    double lowerPower = pmn;
    if (variant == Variant::pv) {
        lowerPower = 0.0;
    } else if (variant == Variant::storage) {
        lowerPower = -pmx;
    }
    const double scheduledPower = inputs.size() > 4 && inputs[4] != kNullVal ? inputs[4] : pref;
    const double pTarget = std::clamp(scheduledPower + paux + pDroop, lowerPower, upperPower);
    const double qTarget = std::clamp(qref + qdroop, qmn, qmx);
    const double voltageCurve =
        lowerTrip(regulationVoltage, vt0, vt1) * upperTrip(regulationVoltage, vt2, vt3);
    const double frequencyCurve =
        lowerTrip(frequencyHz, ft0, ft1) * upperTrip(frequencyHz, ft2, ft3);
    const double voltageFactor = voltageLatched ? vrflag : 1.0;
    const double frequencyFactor = frequencyLatched ? frflag : 1.0;
    const double response = blocked ? 0.0 :
                                      voltageFactor * frequencyFactor *
            (1.0 - recflag + (recflag * voltageCurve * frequencyCurve));
    const double safeVoltage = std::max(voltage, 0.01);
    double ipCommand = response * pTarget / safeVoltage;
    double iqCommand = response * qTarget / safeVoltage;
    if (variant != Variant::pv) {
        if (state[2] <= socmin && ipCommand > 0.0) {
            ipCommand = 0.0;
        }
        if (state[2] >= socmax && ipCommand < 0.0) {
            ipCommand = 0.0;
        }
    }
    if (pqflag == 1.0) {
        ipCommand = std::clamp(ipCommand, -ialim, ialim);
        const double remaining =
            std::sqrt(std::max(0.0, (ialim * ialim) - (ipCommand * ipCommand)));
        iqCommand = std::clamp(iqCommand, -remaining, remaining);
    } else {
        iqCommand = std::clamp(iqCommand, -ialim, ialim);
        const double remaining =
            std::sqrt(std::max(0.0, (ialim * ialim) - (iqCommand * iqCommand)));
        ipCommand = std::clamp(ipCommand, -remaining, remaining);
    }
    rates[0] = (ipCommand - activeCurrent) / tip;
    rates[1] = (iqCommand - reactiveCurrent) / tiq;
    rates[2] = 0.0;
    if (variant != Variant::pv) {
        const double efficiency = power[0] < 0.0 ? etac : 1.0 / etad;
        rates[2] = -systemMva * power[0] * efficiency / (3600.0 * en * tf);
        if ((state[2] <= socmin && rates[2] < 0.0) || (state[2] >= socmax && rates[2] > 0.0)) {
            rates[2] = 0.0;
        }
    }
}

void DistributedConverter::derivative(const IOdata& inputs,
                                      const StateData& stateData,
                                      double deriv[],
                                      const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    std::array<double, 2> power{};
    std::array<double, 3> rates{};
    evaluate(inputs, loc.diffStateLoc, power, rates);
    for (index_t index = 0; index < stateCount(); ++index) {
        loc.destDiffLoc[index] = rates[index];
    }
}

void DistributedConverter::residual(const IOdata& inputs,
                                    const StateData& stateData,
                                    double resid[],
                                    const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    std::array<double, 2> power{};
    std::array<double, 3> rates{};
    evaluate(inputs, loc.diffStateLoc, power, rates);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = power[0] - loc.algStateLoc[0];
        loc.destLoc[1] = power[1] - loc.algStateLoc[1];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < stateCount(); ++index) {
            loc.destDiffLoc[index] = rates[index] - loc.dstateLoc[index];
        }
    }
}

void DistributedConverter::algebraicUpdate(const IOdata& inputs,
                                           const StateData& stateData,
                                           double update[],
                                           const SolverMode& sMode,
                                           double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    std::array<double, 2> power{};
    std::array<double, 3> rates{};
    evaluate(inputs, loc.diffStateLoc, power, rates);
    loc.destLoc[0] = power[0];
    loc.destLoc[1] = power[1];
}

void DistributedConverter::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*mode*/)
{
    const double timeStep = time - prevTime;
    if (timeStep < 0.0) {
        throw InvalidParameterValue("distributed converter timestep precedes time");
    }
    updateLatches(inputs);
    std::array<double, 2> power{};
    std::array<double, 3> rates{};
    evaluate(inputs, m_state.data() + 2, power, rates);
    for (index_t index = 0; index < stateCount(); ++index) {
        m_state[2 + index] += timeStep * rates[index];
    }
    evaluate(inputs, m_state.data() + 2, power, rates);
    m_state[0] = power[0];
    m_state[1] = power[1];
    prevTime = time;
}

void DistributedConverter::jacobianElements(const IOdata& inputs,
                                            const StateData& stateData,
                                            MatrixData<double>& matrixData,
                                            const IOlocs& inputLocs,
                                            const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    std::array<double, 3> states{};
    std::copy_n(loc.diffStateLoc, stateCount(), states.begin());
    std::array<double, 2> basePower{};
    std::array<double, 3> baseRates{};
    evaluate(inputs, states.data(), basePower, baseRates);
    const auto addColumn = [&](index_t column,
                               const std::array<double, 2>& power,
                               const std::array<double, 3>& rates,
                               double step) {
        if (hasAlgebraic(sMode)) {
            matrixData.assignCheckCol(loc.algOffset, column, (power[0] - basePower[0]) / step);
            matrixData.assignCheckCol(loc.algOffset + 1, column, (power[1] - basePower[1]) / step);
        }
        if (hasDifferential(sMode)) {
            for (index_t row = 0; row < stateCount(); ++row) {
                if (column == loc.diffOffset + row) {
                    continue;
                }
                matrixData.assignCheckCol(loc.diffOffset + row,
                                          column,
                                          (rates[row] - baseRates[row]) / step);
            }
        }
    };
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        matrixData.assign(loc.algOffset + 1, loc.algOffset + 1, -1.0);
    }
    for (index_t index = 0; index < stateCount(); ++index) {
        const double step = 1e-7 * std::max(1.0, std::abs(states[index]));
        states[index] += step;
        std::array<double, 2> power{};
        std::array<double, 3> rates{};
        evaluate(inputs, states.data(), power, rates);
        addColumn(loc.diffOffset + index, power, rates, step);
        if (hasDifferential(sMode)) {
            matrixData.assign(loc.diffOffset + index,
                              loc.diffOffset + index,
                              ((rates[index] - baseRates[index]) / step) - stateData.cj);
        }
        states[index] -= step;
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        IOdata shifted = inputs;
        const double step = 1e-7 * std::max(1.0, std::abs(inputs[index]));
        shifted[index] += step;
        std::array<double, 2> power{};
        std::array<double, 3> rates{};
        evaluate(shifted, states.data(), power, rates);
        addColumn(index < inputLocs.size() ? inputLocs[index] : kNullLocation, power, rates, step);
    }
}

IOdata DistributedConverter::getOutputs(const IOdata& /*inputs*/,
                                        const StateData& stateData,
                                        const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[0], loc.algStateLoc[1]};
}
void DistributedConverter::outputPartialDerivatives(const IOdata& /*inputs*/,
                                                    const StateData& /*stateData*/,
                                                    MatrixData<double>& matrixData,
                                                    const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(0, alg, 1.0);
    matrixData.assign(1, alg + 1, 1.0);
}

void DistributedConverter::rootTest(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    double roots[],
                                    const SolverMode& sMode)
{
    const auto offset = offsets.getRootOffset(sMode);
    const double voltage = inputs.size() > 3 && inputs[3] != kNullVal ? inputs[3] : inputs[0];
    const double frequencyHz = fn * (inputs.size() > 2 && inputs[2] != kNullVal ? inputs[2] : 1.0);
    const std::array<double, 8> values{{voltage - vt0,
                                        voltage - vt1,
                                        voltage - vt2,
                                        voltage - vt3,
                                        frequencyHz - ft0,
                                        frequencyHz - ft1,
                                        frequencyHz - ft2,
                                        frequencyHz - ft3}};
    std::copy(values.begin(), values.end(), roots + offset);
}
void DistributedConverter::rootTrigger(CoreTime /*time*/,
                                       const IOdata& inputs,
                                       const std::vector<int>& /*rootMask*/,
                                       const SolverMode& /*sMode*/)
{
    updateLatches(inputs);
}
ChangeCode DistributedConverter::rootCheck(const IOdata& inputs,
                                           const StateData& /*stateData*/,
                                           const SolverMode& /*sMode*/,
                                           CheckLevel /*level*/)
{
    const bool oldVoltageLatch = voltageLatched;
    const bool oldFrequencyLatch = frequencyLatched;
    updateLatches(inputs);
    return oldVoltageLatch != voltageLatched || oldFrequencyLatch != frequencyLatched ?
        ChangeCode::NON_STATE_CHANGE :
        ChangeCode::NO_CHANGE;
}
stringVec DistributedConverter::localStateNames() const
{
    return variant == Variant::pv ? stringVec{"Pe", "Qe", "Ip", "Iq"} :
                                    stringVec{"Pe", "Qe", "Ip", "Iq", "SOC"};
}

void DistributedConverter::copyParametersTo(DistributedConverter* target) const
{
    target->fn = fn;
    target->xc = xc;
    target->pqflag = pqflag;
    target->qmx = qmx;
    target->qmn = qmn;
    target->pmx = pmx;
    target->pmn = pmn;
    target->v0 = v0;
    target->v1 = v1;
    target->dqdv = dqdv;
    target->fdbd = fdbd;
    target->ddn = ddn;
    target->ialim = ialim;
    target->vt0 = vt0;
    target->vt1 = vt1;
    target->vt2 = vt2;
    target->vt3 = vt3;
    target->ft0 = ft0;
    target->ft1 = ft1;
    target->ft2 = ft2;
    target->ft3 = ft3;
    target->vrflag = vrflag;
    target->frflag = frflag;
    target->recflag = recflag;
    target->tip = tip;
    target->tiq = tiq;
    target->pref = pref;
    target->qref = qref;
    target->paux = paux;
    target->pcap = pcap;
    target->tf = tf;
    target->socmin = socmin;
    target->socmax = socmax;
    target->socinit = socinit;
    target->en = en;
    target->etac = etac;
    target->etad = etad;
    target->systemMva = systemMva;
    target->blocked = blocked;
    target->voltageLatched = voltageLatched;
    target->frequencyLatched = frequencyLatched;
    target->frequencySensor = frequencySensor;
    target->regulationBus = regulationBus;
    target->prefConfigured = prefConfigured;
    target->qrefConfigured = qrefConfigured;
}

#define DISTRIBUTED_VARIANT(Model, kind)                                                           \
    Model::Model(const std::string& objName): DistributedConverter(Variant::kind, objName) {}     \
    CoreObject* Model::clone(CoreObject* obj) const                                                \
    {                                                                                              \
        auto* out = cloneBase<Model, DistributedConverter>(this, obj);                             \
        if (out != nullptr) {                                                                      \
            copyParametersTo(out);                                                                 \
        }                                                                                          \
        return out == nullptr ? obj : out;                                                         \
    }
DISTRIBUTED_VARIANT(PVD1, pv)
DISTRIBUTED_VARIANT(ESD1, storage)
DISTRIBUTED_VARIANT(EV1, ev1)
DISTRIBUTED_VARIANT(EV2, ev2)
#undef DISTRIBUTED_VARIANT
}  // namespace griddyn
