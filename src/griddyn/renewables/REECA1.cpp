/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REECA1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 4> inputPortMap{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::activeReferenceIncrement,
         .ioIndex = 1,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::reactiveReferenceIncrement,
         .ioIndex = 2,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::activeReference,
         .ioIndex = 3,
         .base = RenewableBase::machine,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 5> inputPortMapWithSpeed{{
        inputPortMap[0],
        inputPortMap[1],
        inputPortMap[2],
        inputPortMap[3],
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 4},
    }};
    constexpr std::array<RenewablePort, 5> inputPortMapWithReactivePower{{
        inputPortMap[0],
        inputPortMap[1],
        inputPortMap[2],
        inputPortMap[3],
        {.signal = RenewableSignal::reactivePower, .ioIndex = 4, .base = RenewableBase::machine},
    }};
    constexpr std::array<RenewablePort, 6> inputPortMapWithReactivePowerAndSpeed{{
        inputPortMap[0],
        inputPortMap[1],
        inputPortMap[2],
        inputPortMap[3],
        {.signal = RenewableSignal::reactivePower, .ioIndex = 4, .base = RenewableBase::machine},
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 5},
    }};
    constexpr std::array<RenewablePort, 5> inputPortMapWithElectricalPower{{
        inputPortMap[0],
        inputPortMap[1],
        inputPortMap[2],
        inputPortMap[3],
        {.signal = RenewableSignal::electricalPower, .ioIndex = 4, .base = RenewableBase::machine},
    }};
    constexpr std::array<RenewablePort, 6> inputPortMapWithSpeedAndElectricalPower{{
        inputPortMapWithSpeed[0],
        inputPortMapWithSpeed[1],
        inputPortMapWithSpeed[2],
        inputPortMapWithSpeed[3],
        inputPortMapWithSpeed[4],
        {.signal = RenewableSignal::electricalPower, .ioIndex = 5, .base = RenewableBase::machine},
    }};
    constexpr std::array<RenewablePort, 6> inputPortMapWithReactivePowerAndElectricalPower{{
        inputPortMapWithReactivePower[0],
        inputPortMapWithReactivePower[1],
        inputPortMapWithReactivePower[2],
        inputPortMapWithReactivePower[3],
        inputPortMapWithReactivePower[4],
        {.signal = RenewableSignal::electricalPower, .ioIndex = 5, .base = RenewableBase::machine},
    }};
    constexpr std::array<RenewablePort, 7> inputPortMapWithReactivePowerSpeedAndElectricalPower{{
        inputPortMapWithReactivePowerAndSpeed[0],
        inputPortMapWithReactivePowerAndSpeed[1],
        inputPortMapWithReactivePowerAndSpeed[2],
        inputPortMapWithReactivePowerAndSpeed[3],
        inputPortMapWithReactivePowerAndSpeed[4],
        inputPortMapWithReactivePowerAndSpeed[5],
        {.signal = RenewableSignal::electricalPower, .ioIndex = 6, .base = RenewableBase::machine},
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
    constexpr std::array<RenewablePort, 4> outputPortMapWithSoc{{
        outputPortMap[0],
        outputPortMap[1],
        outputPortMap[2],
        {.signal = RenewableSignal::stateOfCharge, .ioIndex = 3},
    }};
    constexpr index_t ipCommand = 0, iqCommand = 1;
    constexpr index_t voltageFilter = 0, powerFilter = 1, powerOrder = 2, reactiveFilter = 3;
    constexpr index_t reactivePowerIntegrator = 4;
    double optionalIncrement(const IOdata& inputs, std::size_t index)
    {
        return inputs.size() > index && inputs[index] != kNullVal ? inputs[index] : 0.0;
    }
    int binaryFlag(double value)
    {
        if (value != 0.0 && value != 1.0) {
            throw InvalidParameterValue("REECA1 flag must be zero or one");
        }
        return static_cast<int>(value);
    }
    bool disabledCurve(const std::array<double, 4>& voltage, const std::array<double, 4>& current)
    {
        return std::all_of(voltage.begin(),
                           voltage.end(),
                           [](double voltage) { return voltage == 0.0; }) &&
            std::all_of(current.begin(), current.end(), [](double index) { return index == 0.0; });
    }
}  // namespace

REECA1::REECA1(const std::string& name): RenewableComponent(name)
{
    // Base flags start disabled; derived models update their input size after
    // construction once virtual storage capabilities are available.
    m_inputSize = 4;
    m_outputSize = 3;
}

void REECA1::updateInputSize()
{
    m_inputSize =
        4 + (PFLAG == 1 ? 1 : 0) + (cascadedVoltageControl() ? 1 : 0) + (hasStorageSoc() ? 1 : 0);
}

CoreObject* REECA1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REECA1, RenewableComponent>(this, obj);
    if (out == nullptr) {
        return obj;
    }
    out->Vdip = Vdip;
    out->Vup = Vup;
    out->Trv = Trv;
    out->dbd1 = dbd1;
    out->dbd2 = dbd2;
    out->Kqv = Kqv;
    out->Iqh1 = Iqh1;
    out->Iql1 = Iql1;
    out->Vref0 = Vref0;
    out->Tp = Tp;
    out->Tiq = Tiq;
    out->Tpord = Tpord;
    out->Tpfilt = Tpfilt;
    out->dPmax = dPmax;
    out->dPmin = dPmin;
    out->PMAX = PMAX;
    out->PMIN = PMIN;
    out->Imax = Imax;
    out->pfaref = pfaref;
    out->QMax = QMax;
    out->QMin = QMin;
    out->VMAX = VMAX;
    out->VMIN = VMIN;
    out->Vref1 = Vref1;
    out->Kqp = Kqp;
    out->Kqi = Kqi;
    out->Kvp = Kvp;
    out->Kvi = Kvi;
    out->Iqfrz = Iqfrz;
    out->Thld = Thld;
    out->Thld2 = Thld2;
    out->PFFLAG = PFFLAG;
    out->VFLAG = VFLAG;
    out->QFLAG = QFLAG;
    out->PFLAG = PFLAG;
    out->PQFLAG = PQFLAG;
    out->Vq = Vq;
    out->Iq = Iq;
    out->Vp = Vp;
    out->Ip = Ip;
    out->initialP = initialP;
    out->initialQ = initialQ;
    out->initialVref = initialVref;
    out->initialPowerFactorAngle = initialPowerFactorAngle;
    out->voltageDipActive = voltageDipActive;
    out->activeLimitHeld = activeLimitHeld;
    out->activeLimitRelease = activeLimitRelease;
    out->heldActiveLimit = heldActiveLimit;
    out->heldPowerOrder = heldPowerOrder;
    return out;
}

std::span<const RenewablePort> REECA1::inputPorts() const
{
    if (hasStorageSoc()) {
        if (cascadedVoltageControl()) {
            return PFLAG == 1 ?
                std::span<const RenewablePort>{
                    inputPortMapWithReactivePowerSpeedAndElectricalPower} :
                std::span<const RenewablePort>{inputPortMapWithReactivePowerAndElectricalPower};
        }
        return PFLAG == 1 ?
            std::span<const RenewablePort>{inputPortMapWithSpeedAndElectricalPower} :
            std::span<const RenewablePort>{inputPortMapWithElectricalPower};
    }
    if (cascadedVoltageControl()) {
        return PFLAG == 1 ? std::span<const RenewablePort>{inputPortMapWithReactivePowerAndSpeed} :
                            std::span<const RenewablePort>{inputPortMapWithReactivePower};
    }
    return PFLAG == 1 ? std::span<const RenewablePort>{inputPortMapWithSpeed} :
                        std::span<const RenewablePort>{inputPortMap};
}
std::span<const RenewablePort> REECA1::outputPorts() const
{
    return hasStorageSoc() ? std::span<const RenewablePort>{outputPortMapWithSoc} :
                             std::span<const RenewablePort>{outputPortMap};
}

void REECA1::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "pfflag") {
        PFFLAG = binaryFlag(val);
    } else if (key == "vflag") {
        VFLAG = binaryFlag(val);
        updateInputSize();
    } else if (key == "qflag") {
        QFLAG = binaryFlag(val);
        updateInputSize();
    } else if (key == "pflag") {
        PFLAG = binaryFlag(val);
        updateInputSize();
    } else if (key == "pqflag") {
        PQFLAG = binaryFlag(val);
    } else if (key == "vdip") {
        Vdip = val;
    } else if (key == "vup") {
        Vup = val;
    } else if (key == "trv") {
        Trv = val;
    } else if (key == "dbd1") {
        dbd1 = val;
    } else if (key == "dbd2") {
        dbd2 = val;
    } else if (key == "kqv") {
        Kqv = val;
    } else if (key == "iqh1") {
        Iqh1 = val;
    } else if (key == "iql1") {
        Iql1 = val;
    } else if (key == "vref0") {
        Vref0 = val;
    } else if (key == "pfaref") {
        pfaref = val;
    } else if (key == "iqfrz") {
        Iqfrz = val;
    } else if (key == "thld") {
        Thld = val;
    } else if (key == "thld2") {
        Thld2 = val;
    } else if (key == "tp") {
        Tp = val;
    } else if (key == "qmax") {
        QMax = val;
    } else if (key == "qmin") {
        QMin = val;
    } else if (key == "vmax") {
        VMAX = val;
    } else if (key == "vmin") {
        VMIN = val;
    } else if (key == "kqp") {
        Kqp = val;
    } else if (key == "kqi") {
        Kqi = val;
    } else if (key == "kvp") {
        Kvp = val;
    } else if (key == "kvi") {
        Kvi = val;
    } else if (key == "vref1") {
        Vref1 = val;
    } else if (key == "tiq") {
        Tiq = val;
    } else if (key == "dpmax") {
        dPmax = val;
    } else if (key == "dpmin") {
        dPmin = val;
    } else if (key == "pmax") {
        PMAX = val;
    } else if (key == "pmin") {
        PMIN = val;
    } else if (key == "imax") {
        Imax = val;
    } else if (key == "tpord") {
        Tpord = val;
    } else if (key == "tpfilt") {
        Tpfilt = val;
    } else if (key.size() == 3 &&
               (key.starts_with("vq") || key.starts_with("iq") || key.starts_with("vp") ||
                key.starts_with("ip")) &&
               key[2] >= '1' && key[2] <= '4') {
        const auto index = static_cast<std::size_t>(key[2] - '1');
        if (key.starts_with("vq")) {
            Vq[index] = val;
        } else if (key.starts_with("iq")) {
            Iq[index] = val;
        } else if (key.starts_with("vp")) {
            Vp[index] = val;
        } else {
            Ip[index] = val;
        }
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double REECA1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "pfflag") {
        return PFFLAG;
    }
    if (key == "vflag") {
        return VFLAG;
    }
    if (key == "qflag") {
        return QFLAG;
    }
    if (key == "pflag") {
        return PFLAG;
    }
    if (key == "pqflag") {
        return PQFLAG;
    }
    if (key == "vdip") {
        return Vdip;
    }
    if (key == "vup") {
        return Vup;
    }
    if (key == "trv") {
        return Trv;
    }
    if (key == "dbd1") {
        return dbd1;
    }
    if (key == "dbd2") {
        return dbd2;
    }
    if (key == "kqv") {
        return Kqv;
    }
    if (key == "iqh1") {
        return Iqh1;
    }
    if (key == "iql1") {
        return Iql1;
    }
    if (key == "iqfrz") {
        return Iqfrz;
    }
    if (key == "thld") {
        return Thld;
    }
    if (key == "thld2") {
        return Thld2;
    }
    if (key == "tp") {
        return Tp;
    }
    if (key == "tiq") {
        return Tiq;
    }
    if (key == "tpord") {
        return Tpord;
    }
    if (key == "imax") {
        return Imax;
    }
    if (key == "pmax") {
        return PMAX;
    }
    if (key == "pmin") {
        return PMIN;
    }
    if (key == "qmax") {
        return QMax;
    }
    if (key == "qmin") {
        return QMin;
    }
    if (key == "dpmax") {
        return dPmax;
    }
    if (key == "dpmin") {
        return dPmin;
    }
    if (key == "vref0") {
        return Vref0;
    }
    if (key == "pfaref") {
        return pfaref;
    }
    if (key == "vref1") {
        return Vref1;
    }
    if (key == "vmax") {
        return VMAX;
    }
    if (key == "vmin") {
        return VMIN;
    }
    if (key == "kqp") {
        return Kqp;
    }
    if (key == "kqi") {
        return Kqi;
    }
    if (key == "kvp") {
        return Kvp;
    }
    if (key == "kvi") {
        return Kvi;
    }
    if (key == "tpfilt") {
        return Tpfilt;
    }
    if (key.size() == 3 && key[2] >= '1' && key[2] <= '4') {
        const auto index = static_cast<std::size_t>(key[2] - '1');
        if (key.starts_with("vq")) {
            return Vq[index];
        }
        if (key.starts_with("iq")) {
            return Iq[index];
        }
        if (key.starts_with("vp")) {
            return Vp[index];
        }
        if (key.starts_with("ip")) {
            return Ip[index];
        }
    }
    return RenewableComponent::get(param, unitType);
}

void REECA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 27> inputs{Vdip, Vup,  Trv,   dbd1,   dbd2,  Kqv,   Iqh1, Iql1, Vref0,
                                        Tp,   Tiq,  Tpord, Tpfilt, dPmax, dPmin, PMAX, PMIN, Imax,
                                        QMax, QMin, VMAX,  VMIN,   Kqp,   Kqi,   Kvp,  Kvi,  Vref1};
    if (std::any_of(inputs.begin(),
                    inputs.end(),
                    [](double stateValue) { return !std::isfinite(stateValue); }) ||
        !std::isfinite(Vq[0]) || !std::isfinite(Iq[0]) || !std::isfinite(Vp[0]) ||
        !std::isfinite(Ip[0]) || Trv <= 0 || Tiq <= 0 || Tpord < 0 || Tpfilt <= 0 || Vdip >= Vup ||
        dbd1 > 0 || dbd2 < 0 || Iqh1 < Iql1 || dPmax < 0 || dPmin > 0 || PMAX < PMIN ||
        QMax < QMin || Imax < 0 || !std::isfinite(Iqfrz) || !std::isfinite(Thld) ||
        !std::isfinite(Thld2) || (PQFLAG != 0 && PQFLAG != 1) || (VFLAG != 0 && VFLAG != 1) ||
        (PFFLAG != 0 && !supportsPowerFactorControl()) || (QFLAG != 0 && QFLAG != 1) ||
        (PFFLAG == 1 && pfaref != kNullVal &&
         (!std::isfinite(pfaref) || std::abs(pfaref) >= 1.5707963267948966))) {
        throw InvalidParameterValue("REECA1 unsupported flags or invalid parameters");
    }
    if (PFLAG == 1 && Tpord == 0.0) {
        throw InvalidParameterValue("REECA1 PFLAG=1 with Tpord=0 is not implemented");
    }
    if (Iqfrz != 0.0 || Thld != 0.0 || Thld2 < 0.0) {
        throw InvalidParameterValue(
            "REECA1 reactive-current hold is not implemented or Thld2 is negative");
    }
    const bool noQCurve = disabledCurve(Vq, Iq);
    const bool noPCurve = disabledCurve(Vp, Ip);
    for (std::size_t index = 1; index < 4; ++index) {
        if (!std::isfinite(Vq[index]) || !std::isfinite(Iq[index]) || !std::isfinite(Vp[index]) ||
            !std::isfinite(Ip[index]) ||
            (!noQCurve && (Vq[index] <= Vq[index - 1] || Iq[index] < Iq[index - 1])) ||
            (!noPCurve && (Vp[index] <= Vp[index - 1] || Ip[index] < Ip[index - 1]))) {
            throw InvalidParameterValue("REECA1 V-I limit curves must increase");
        }
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = baseDifferentialStateCount() + (hasStorageSoc() ? 1 : 0) +
        ((PFFLAG == 1 && supportsPowerFactorControl()) ? 1 : 0);
    local.jacSize = (cascadedVoltageControl() ? 54 : 42) + (hasStorageSoc() ? 4 : 0) +
        ((PFFLAG == 1 && supportsPowerFactorControl()) ? 6 : 0);
    local.algRoots = Thld2 > 0.0 ? 2 : 0;
    local.diffRoots = 0;
    voltageDipActive = false;
    activeLimitHeld = false;
    activeLimitRelease = time0;
    heldActiveLimit = 0.0;
    heldPowerOrder = 0.0;
    prevTime = time0;
}

double REECA1::curve(double voltage,
                     const std::array<double, 4>& points,
                     const std::array<double, 4>& currents)
{
    if (voltage <= points[0]) {
        return currents[0];
    }
    for (std::size_t index = 1; index < 4; ++index) {
        if (voltage <= points[index]) {
            return currents[index - 1] +
                ((currents[index] - currents[index - 1]) * (voltage - points[index - 1]) /
                 (points[index] - points[index - 1]));
        }
    }
    return currents[3];
}

double REECA1::voltageReference(const IOdata& inputs, const double state[]) const
{
    if (!cascadedVoltageControl()) {
        // In REEC_B direct-voltage mode QEXT is a voltage reference.  GridDyn's
        // renewable signal is an increment, so the local Vref0 remains the
        // operating-point reference and a connected plant signal adjusts it.
        return initialVref + optionalIncrement(inputs, 2);
    }
    const double qReference = reactivePowerReference(inputs, state);
    const double qError = std::clamp(qReference, QMin, QMax) - reactivePowerFeedback(inputs);
    return std::clamp((Kqp * qError) + state[reactivePowerIntegrator], VMIN, VMAX);
}

double REECA1::reactivePowerReference(const IOdata& inputs, const double state[]) const
{
    if (PFFLAG == 1 && supportsPowerFactorControl()) {
        return state[powerFactorFilterStateIndex()] * std::tan(initialPowerFactorAngle);
    }
    return initialQ + optionalIncrement(inputs, 2);
}

double REECA1::reactivePowerFeedback(const IOdata& inputs) const
{
    if (!cascadedVoltageControl() || inputs.size() <= 4 || !std::isfinite(inputs[4])) {
        return initialQ;
    }
    return inputs[4];
}

double REECA1::reactiveControl(const IOdata& inputs, const double state[]) const
{
    if (QFLAG == 0) {
        return state[reactiveFilter];
    }
    return state[reactiveFilter] + (Kvp * (voltageReference(inputs, state) - state[voltageFilter]));
}

double REECA1::activeLimit(const IOdata& inputs, const double state[]) const
{
    const double voltage = inputs[0];
    const double err = initialVref - state[voltageFilter];
    double deadband = 0.0;
    if (err > dbd2) {
        deadband = err - dbd2;
    } else if (err < dbd1) {
        deadband = err - dbd1;
    }
    const double injection =
        useVoltageInjection(voltage) ? std::clamp(Kqv * deadband, Iql1, Iqh1) : 0.0;
    const double ipCap =
        disabledCurve(Vp, Ip) ? Imax : std::min(Imax, curve(state[voltageFilter], Vp, Ip));
    if (PQFLAG != 0) {
        return ipCap;
    }
    const double iqCap =
        disabledCurve(Vq, Iq) ? Imax : std::min(Imax, curve(state[voltageFilter], Vq, Iq));
    const double reactiveCurrent =
        std::clamp(reactiveControl(inputs, state) + injection, -iqCap, iqCap);
    return std::min(ipCap,
                    std::sqrt(std::max(0.0, (Imax * Imax) - (reactiveCurrent * reactiveCurrent))));
}

std::array<double, 2> REECA1::commands(const IOdata& inputs, const double state[]) const
{
    const double voltage = inputs[0];
    if (Tpord == 0.0 && Thld2 == 0.0 && voltageDip(voltage)) {
        throw InvalidParameterValue(
            "REECA1 zero-Tpord power-order hold during a voltage dip is not implemented");
    }
    const double voltagePoint = std::max(voltage, 0.01);
    const double err = initialVref - state[voltageFilter];
    double deadband = 0.0;
    if (err > dbd2) {
        deadband = err - dbd2;
    } else if (err < dbd1) {
        deadband = err - dbd1;
    }
    const double injection =
        useVoltageInjection(voltage) ? std::clamp(Kqv * deadband, Iql1, Iqh1) : 0.0;
    const double iqCap =
        disabledCurve(Vq, Iq) ? Imax : std::min(Imax, curve(state[voltageFilter], Vq, Iq));
    double activePowerOrder = state[powerOrder];
    if (Tpord == 0.0) {
        activePowerOrder = std::clamp(state[powerFilter], PMIN, PMAX);
        if (voltageDipActive) {
            activePowerOrder = heldPowerOrder;
        }
    }
    const double rawIp = activePowerOrder / voltagePoint;
    const double rawIq = reactiveControl(inputs, state) + injection;
    const double ipCap = activeLimitHeld ? heldActiveLimit : activeLimit(inputs, state);
    const auto [ipMin, ipMax] = activeCurrentBounds(inputs, state, ipCap);
    double activeCurrent = 0.0;
    double reactiveCurrent = 0.0;
    if (PQFLAG == 0) {
        reactiveCurrent = std::clamp(rawIq, -iqCap, iqCap);
        activeCurrent = std::clamp(rawIp, ipMin, ipMax);
    } else {
        activeCurrent = std::clamp(rawIp, ipMin, ipMax);
        reactiveCurrent = std::clamp(
            rawIq,
            -std::min(iqCap,
                      std::sqrt(std::max(0.0, (Imax * Imax) - (activeCurrent * activeCurrent)))),
            std::min(iqCap,
                     std::sqrt(std::max(0.0, (Imax * Imax) - (activeCurrent * activeCurrent)))));
    }
    return {activeCurrent, -reactiveCurrent};
}

double REECA1::generatorSpeed(const IOdata& inputs) const
{
    if (PFLAG == 0) {
        return 1.0;
    }
    const auto index = speedInputIndex();
    if (std::cmp_less_equal(inputs.size(), index) || !std::isfinite(inputs[index]) ||
        inputs[index] <= 0.0) {
        throw InvalidParameterValue("REECA1 PFLAG=1 requires positive generator speed");
    }
    return inputs[index];
}

std::array<double, 7> REECA1::rates(const IOdata& inputs, const double state[]) const
{
    std::array<double, 7> result{};
    const double voltage = inputs[0];
    const double external = inputs.size() > 3 ? inputs[3] : kNullVal;
    const double speed = generatorSpeed(inputs);
    const double pref =
        (((external == kNullVal ? initialP : external) + optionalIncrement(inputs, 1)) / speed) +
        activeReferenceAdjustment(inputs);
    const double qref = std::clamp(reactivePowerReference(inputs, state), QMin, QMax);
    const double pRate = std::clamp((pref - state[powerFilter]) / Tpfilt, dPmin, dPmax);
    result[0] = (voltage - state[voltageFilter]) / Trv;
    result[1] = pRate;
    if (dipMode(voltage)) {
        if (hasStorageSoc()) {
            result[storageSocStateIndex()] = storageSocRate(inputs);
        }
        if (PFFLAG == 1 && supportsPowerFactorControl()) {
            result[powerFactorFilterStateIndex()] =
                (inputs[electricalPowerInputIndex()] - state[powerFactorFilterStateIndex()]) /
                Tpfilt;
        }
        return result;
    }
    result[2] = Tpord == 0.0 ?
        0.0 :
        (std::clamp(speed * state[powerFilter], PMIN, PMAX) - state[powerOrder]) / Tpord;
    if (QFLAG != 0) {
        const double qError = qref - reactivePowerFeedback(inputs);
        const double qPiRate = cascadedVoltageControl() ? Kqi * qError : 0.0;
        const double vPiRate = Kvi * (voltageReference(inputs, state) - state[voltageFilter]);
        result[3] = vPiRate;
        result[4] = qPiRate;
    } else {
        result[3] = ((qref / std::max(voltage, 0.01)) - state[reactiveFilter]) / Tiq;
    }
    if (hasStorageSoc()) {
        result[storageSocStateIndex()] = storageSocRate(inputs);
    }
    if (PFFLAG == 1 && supportsPowerFactorControl()) {
        result[powerFactorFilterStateIndex()] =
            (inputs[electricalPowerInputIndex()] - state[powerFactorFilterStateIndex()]) / Tpfilt;
    }
    return result;
}

void REECA1::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet)
{
    if (inputs.empty() || inputs[0] <= 0 || desiredOutput.size() < 2 || !std::isfinite(inputs[0]) ||
        !std::isfinite(desiredOutput[0]) || !std::isfinite(desiredOutput[1])) {
        throw InvalidParameterValue("REECA1 initial voltage and P/Q");
    }
    initialP = desiredOutput[0];
    initialQ = desiredOutput[1];
    initialVref = Vref0 == 0.0 ? inputs[0] : Vref0;
    if (PFFLAG == 1 && supportsPowerFactorControl()) {
        if (pfaref == kNullVal) {
            initialPowerFactorAngle = (initialP == 0.0) ? 0.0 : std::atan(initialQ / initialP);
        } else {
            initialPowerFactorAngle = pfaref;
        }
        if (!std::isfinite(initialPowerFactorAngle) ||
            std::abs(initialPowerFactorAngle) >= 1.5707963267948966) {
            throw InvalidParameterValue(
                "REECC1 initial power-factor angle must be within +/- pi/2");
        }
    }
    if (Thld2 > 0.0 && voltageDip(inputs[0])) {
        throw InvalidParameterValue("REECA1 must initialize outside voltage-dip range");
    }
    if (initialP < PMIN || initialP > PMAX || initialQ < QMin || initialQ > QMax) {
        throw InvalidParameterValue("REECA1 initial power outside reference limits");
    }
    m_state[2 + voltageFilter] = inputs[0];
    m_state[2 + powerFilter] = initialP / generatorSpeed(inputs);
    m_state[2 + powerOrder] = initialP;
    if (hasStorageSoc()) {
        m_state[2 + storageSocStateIndex()] = initialStorageSoc();
    }
    if (PFFLAG == 1 && supportsPowerFactorControl()) {
        m_state[2 + powerFactorFilterStateIndex()] = initialP;
    }
    const double injection = [&]() {
        const double err = initialVref - inputs[0];
        double deadband = 0.0;
        if (err > dbd2) {
            deadband = err - dbd2;
        } else if (err < dbd1) {
            deadband = err - dbd1;
        }
        return useVoltageInjection(inputs[0]) ? std::clamp(Kqv * deadband, Iql1, Iqh1) : 0.0;
    }();
    if (cascadedVoltageControl()) {
        // At the operating point the measured reactive power equals the
        // requested power in the usual case.  Preserve the general Q-PI
        // error as well, since an attached plant controller can provide a
        // nonzero reactive-power reference increment at initialization.
        const double qError =
            std::clamp(reactivePowerReference(inputs, m_state.data() + 2), QMin, QMax) -
            reactivePowerFeedback(inputs);
        const double voltageControlReference = std::clamp(inputs[0], VMIN, VMAX);
        m_state[2 + reactivePowerIntegrator] = voltageControlReference - (Kqp * qError);
        m_state[2 + reactiveFilter] =
            (initialQ / inputs[0]) - injection - (Kvp * (voltageControlReference - inputs[0]));
    } else if (QFLAG != 0) {
        m_state[2 + reactiveFilter] =
            (initialQ / inputs[0]) - injection - (Kvp * (initialVref - inputs[0]));
    } else {
        m_state[2 + reactiveFilter] = initialQ / inputs[0];
    }
    heldPowerOrder = initialP;
    heldActiveLimit = activeLimit(inputs, m_state.data() + 2);
    const auto current = commands(inputs, m_state.data() + 2);
    if (std::abs(current[0] - (initialP / inputs[0])) > 1e-8 ||
        std::abs(current[1] + (initialQ / inputs[0])) > 1e-8) {
        throw InvalidParameterValue("REECA1 initial P/Q exceeds current limits");
    }
    m_state[ipCommand] = current[0];
    m_state[iqCommand] = current[1];
    fieldSet = {current[0], current[1]};
}

void REECA1::derivative(const IOdata& inputs,
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

void REECA1::residual(const IOdata& inputs,
                      const StateData& stateData,
                      double resid[],
                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto current = commands(inputs, loc.diffStateLoc);
        loc.destLoc[ipCommand] = current[0] - loc.algStateLoc[ipCommand];
        loc.destLoc[iqCommand] = current[1] - loc.algStateLoc[iqCommand];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void REECA1::algebraicUpdate(const IOdata& inputs,
                             const StateData& stateData,
                             double update[],
                             const SolverMode& sMode,
                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const auto current = commands(inputs, loc.diffStateLoc);
    loc.destLoc[ipCommand] = current[0];
    loc.destLoc[iqCommand] = current[1];
}

void REECA1::jacobianElements(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const index_t alg = loc.algOffset;
    const index_t diff = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg + ipCommand, alg + ipCommand, -1.0);
        matrixData.assign(alg + iqCommand, alg + iqCommand, -1.0);
        constexpr double step = 1e-6;
        if (!isAlgebraicOnly(sMode)) {
            for (index_t column = 0; column < loc.diffSize; ++column) {
                std::array<double, 7> plus{};
                std::copy_n(state, loc.diffSize, plus.begin());
                auto minus = plus;
                plus[column] += step;
                minus[column] -= step;
                const auto upper = commands(inputs, plus.data());
                const auto lower = commands(inputs, minus.data());
                for (index_t index = 0; index < 2; ++index) {
                    matrixData.assign(alg + index,
                                      diff + column,
                                      (upper[index] - lower[index]) / (2 * step));
                }
            }
        }
        const auto inputCount = std::min(inputs.size(), inputLocs.size());
        for (std::size_t input = 0; input < inputCount; ++input) {
            if (inputLocs[input] == kNullLocation || inputs[input] == kNullVal) {
                continue;
            }
            auto upperInputs = inputs;
            auto lowerInputs = inputs;
            upperInputs[input] += step;
            lowerInputs[input] -= step;
            const auto upper = commands(upperInputs, state);
            const auto lower = commands(lowerInputs, state);
            for (index_t output = 0; output < 2; ++output) {
                matrixData.assign(alg + output,
                                  inputLocs[input],
                                  (upper[output] - lower[output]) / (2 * step));
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(diff + voltageFilter, diff + voltageFilter, (-1.0 / Trv) - stateData.cj);
    matrixData.assignCheckCol(diff + voltageFilter, inputLocs[0], 1.0 / Trv);
    const double external = inputs.size() > 3 ? inputs[3] : kNullVal;
    const double speed = generatorSpeed(inputs);
    const double rawPref =
        (external == kNullVal ? initialP : external) + optionalIncrement(inputs, 1);
    const double pref = (rawPref / speed) + activeReferenceAdjustment(inputs);
    const double pRaw = (pref - state[powerFilter]) / Tpfilt;
    const bool pFree = pRaw > dPmin && pRaw < dPmax;
    matrixData.assign(diff + powerFilter,
                      diff + powerFilter,
                      (pFree ? -1.0 / Tpfilt : 0.0) - stateData.cj);
    if (pFree && inputLocs.size() > 1 && inputs.size() > 1 && inputs[1] != kNullVal) {
        matrixData.assignCheckCol(diff + powerFilter, inputLocs[1], 1.0 / (speed * Tpfilt));
    }
    if (pFree && inputLocs.size() > 3 && external != kNullVal) {
        matrixData.assignCheckCol(diff + powerFilter, inputLocs[3], 1.0 / (speed * Tpfilt));
    }
    if (PFLAG == 1 && pFree && std::cmp_greater(inputLocs.size(), speedInputIndex())) {
        matrixData.assignCheckCol(diff + powerFilter,
                                  inputLocs[speedInputIndex()],
                                  -rawPref / (speed * speed * Tpfilt));
    }
    if (pFree) {
        activeReferenceJacobian(inputLocs, matrixData, diff + powerFilter, 1.0 / Tpfilt);
    }
    if (dipMode(inputs[0])) {
        matrixData.assign(diff + powerOrder, diff + powerOrder, -stateData.cj);
        matrixData.assign(diff + reactiveFilter, diff + reactiveFilter, -stateData.cj);
        if (cascadedVoltageControl()) {
            matrixData.assign(diff + reactivePowerIntegrator,
                              diff + reactivePowerIntegrator,
                              -stateData.cj);
        }
    } else {
        matrixData.assign(diff + powerOrder,
                          diff + powerOrder,
                          (Tpord == 0.0 ? 0.0 : -1.0 / Tpord) - stateData.cj);
        const double selectedPower = speed * state[powerFilter];
        if (Tpord != 0.0 && selectedPower > PMIN && selectedPower < PMAX) {
            matrixData.assign(diff + powerOrder, diff + powerFilter, speed / Tpord);
            if (PFLAG == 1 && std::cmp_greater(inputLocs.size(), speedInputIndex())) {
                matrixData.assignCheckCol(diff + powerOrder,
                                          inputLocs[speedInputIndex()],
                                          state[powerFilter] / Tpord);
            }
        }
        if (QFLAG == 0) {
            matrixData.assign(diff + reactiveFilter,
                              diff + reactiveFilter,
                              (-1.0 / Tiq) - stateData.cj);
            const double qRaw = reactivePowerReference(inputs, state);
            const double qref = std::clamp(qRaw, QMin, QMax);
            const double voltage = std::max(inputs[0], 0.01);
            if (inputs[0] > 0.01) {
                matrixData.assignCheckCol(diff + reactiveFilter,
                                          inputLocs[0],
                                          -qref / (voltage * voltage * Tiq));
            }
            if (qRaw > QMin && qRaw < QMax && inputLocs.size() > 2 && inputs.size() > 2 &&
                inputs[2] != kNullVal) {
                matrixData.assignCheckCol(diff + reactiveFilter,
                                          inputLocs[2],
                                          1.0 / (voltage * Tiq));
            }
            if (PFFLAG == 1 && supportsPowerFactorControl() && qRaw > QMin && qRaw < QMax) {
                matrixData.assign(diff + reactiveFilter,
                                  diff + powerFactorFilterStateIndex(),
                                  std::tan(initialPowerFactorAngle) / (voltage * Tiq));
            }
        } else {
            const double qRaw = reactivePowerReference(inputs, state);
            const bool qFree = qRaw > QMin && qRaw < QMax;
            matrixData.assign(diff + reactiveFilter, diff + reactiveFilter, -stateData.cj);
            matrixData.assign(diff + reactiveFilter, diff + voltageFilter, -Kvi);
            if (cascadedVoltageControl()) {
                const double qError = std::clamp(qRaw, QMin, QMax) - reactivePowerFeedback(inputs);
                const double qPiRaw = (Kqp * qError) + state[reactivePowerIntegrator];
                const bool qPiFree = qPiRaw > VMIN && qPiRaw < VMAX;
                matrixData.assign(diff + reactivePowerIntegrator,
                                  diff + reactivePowerIntegrator,
                                  -stateData.cj);
                if (qFree && inputLocs.size() > 2 && inputs.size() > 2 && inputs[2] != kNullVal) {
                    matrixData.assignCheckCol(diff + reactivePowerIntegrator, inputLocs[2], Kqi);
                }
                if (qFree && inputLocs.size() > 4 && inputs.size() > 4 && inputs[4] != kNullVal) {
                    matrixData.assignCheckCol(diff + reactivePowerIntegrator, inputLocs[4], -Kqi);
                }
                if (PFFLAG == 1 && supportsPowerFactorControl() && qFree) {
                    matrixData.assign(diff + reactivePowerIntegrator,
                                      diff + powerFactorFilterStateIndex(),
                                      Kqi * std::tan(initialPowerFactorAngle));
                }
                if (qPiFree) {
                    matrixData.assign(diff + reactiveFilter, diff + reactivePowerIntegrator, Kvi);
                    if (qFree && inputLocs.size() > 2 && inputs.size() > 2 &&
                        inputs[2] != kNullVal) {
                        matrixData.assignCheckCol(diff + reactiveFilter, inputLocs[2], Kvi * Kqp);
                    }
                    if (qFree && inputLocs.size() > 4 && inputs.size() > 4 &&
                        inputs[4] != kNullVal) {
                        matrixData.assignCheckCol(diff + reactiveFilter, inputLocs[4], -Kvi * Kqp);
                    }
                    if (PFFLAG == 1 && supportsPowerFactorControl() && qFree) {
                        matrixData.assign(diff + reactiveFilter,
                                          diff + powerFactorFilterStateIndex(),
                                          Kvi * Kqp * std::tan(initialPowerFactorAngle));
                    }
                }
            } else if (inputLocs.size() > 2 && inputs.size() > 2 && inputs[2] != kNullVal) {
                matrixData.assignCheckCol(diff + reactiveFilter, inputLocs[2], Kvi);
            }
        }
    }
    if (hasStorageSoc()) {
        const auto socIndex = storageSocStateIndex();
        const auto powerIndex = electricalPowerInputIndex();
        matrixData.assign(diff + socIndex, diff + socIndex, -stateData.cj);
        if (std::cmp_greater(inputLocs.size(), powerIndex)) {
            matrixData.assignCheckCol(diff + socIndex,
                                      inputLocs[powerIndex],
                                      -1.0 / storageSocTimeConstant());
        }
    }
    if (PFFLAG == 1 && supportsPowerFactorControl()) {
        const auto filterIndex = powerFactorFilterStateIndex();
        const auto powerIndex = electricalPowerInputIndex();
        matrixData.assign(diff + filterIndex, diff + filterIndex, (-1.0 / Tpfilt) - stateData.cj);
        if (std::cmp_greater(inputLocs.size(), powerIndex)) {
            matrixData.assignCheckCol(diff + filterIndex, inputLocs[powerIndex], 1.0 / Tpfilt);
        }
    }
}

void REECA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime = time - prevTime;
    if (deltaTime < 0) {
        throw InvalidParameterValue("REECA1 timestep precedes current time");
    }
    if (Thld2 > 0.0) {
        const bool dip = voltageDip(inputs[0]);
        if (dip != voltageDipActive) {
            transition(time, inputs, dip);
        }
        if (!dip && activeLimitHeld && time >= activeLimitRelease) {
            activeLimitHeld = false;
        }
    }
    const auto rate = rates(inputs, m_state.data() + 2);
    const auto diffSize = offsets.local().local.diffSize;
    for (index_t index = 0; index < diffSize; ++index) {
        m_state[2 + index] += deltaTime * rate[index];
    }
    const auto current = commands(inputs, m_state.data() + 2);
    m_state[ipCommand] = current[0];
    m_state[iqCommand] = current[1];
    prevTime = time;
}

IOdata REECA1::getOutputs(const IOdata& /*inputs*/,
                          const StateData& stateData,
                          const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    IOdata outputs{loc.algStateLoc[ipCommand],
                   loc.algStateLoc[iqCommand],
                   loc.diffStateLoc[powerOrder]};
    if (Tpord == 0.0) {
        outputs[2] = std::clamp(loc.diffStateLoc[powerFilter], PMIN, PMAX);
        if (voltageDipActive) {
            outputs[2] = heldPowerOrder;
        }
    }
    if (hasStorageSoc()) {
        outputs.push_back(loc.diffStateLoc[storageSocStateIndex()]);
    }
    return outputs;
}

double REECA1::getOutput(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode,
                         index_t outputNum) const
{
    if (outputNum == 2) {
        const auto* state = offsets.getLocations(stateData, sMode, this).diffStateLoc;
        if (Tpord == 0.0) {
            return voltageDipActive ? heldPowerOrder : std::clamp(state[powerFilter], PMIN, PMAX);
        }
        return state[powerOrder];
    }
    if (outputNum == 3 && hasStorageSoc()) {
        return offsets.getLocations(stateData, sMode, this).diffStateLoc[storageSocStateIndex()];
    }
    return RenewableComponent::getOutput(inputs, stateData, sMode, outputNum);
}

index_t REECA1::getOutputLoc(const SolverMode& sMode, index_t outputNum) const
{
    if (outputNum == 2) {
        if (Tpord == 0.0 && voltageDipActive) {
            return kNullLocation;
        }
        return offsets.getDiffOffset(sMode) + (Tpord == 0.0 ? powerFilter : powerOrder);
    }
    if (outputNum == 3 && hasStorageSoc()) {
        return offsets.getDiffOffset(sMode) + storageSocStateIndex();
    }
    return RenewableComponent::getOutputLoc(sMode, outputNum);
}

void REECA1::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const StateData& /*stateDataValue*/,
                                      MatrixData<double>& matrixData,
                                      const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(ipCommand, alg + ipCommand, 1.0);
    matrixData.assign(iqCommand, alg + iqCommand, 1.0);
    if (Tpord != 0.0 || !voltageDipActive) {
        matrixData.assign(2,
                          offsets.getDiffOffset(sMode) + (Tpord == 0.0 ? powerFilter : powerOrder),
                          1.0);
    }
    if (hasStorageSoc()) {
        matrixData.assign(3, offsets.getDiffOffset(sMode) + storageSocStateIndex(), 1.0);
    }
}

void REECA1::transition(CoreTime time, const IOdata& inputs, bool entering)
{
    if (entering) {
        heldPowerOrder = std::clamp(m_state[2 + powerFilter], PMIN, PMAX);
        voltageDipActive = true;
        activeLimitHeld = false;
    } else {
        // The limit follows the reactive current during the dip. Capture its
        // final dip value before clearing the dip branch for post-dip hold.
        heldActiveLimit = activeLimit(inputs, m_state.data() + 2);
        voltageDipActive = false;
        activeLimitHeld = true;
        activeLimitRelease = time + Thld2;
    }
    alert(this, JAC_COUNT_CHANGE);
}

void REECA1::rootTest(const IOdata& inputs,
                      const StateData& stateData,
                      double roots[],
                      const SolverMode& sMode)
{
    if (Thld2 <= 0.0) {
        return;
    }
    constexpr double hysteresis = 1e-7;
    const auto root = offsets.getRootOffset(sMode);
    const double voltage = inputs[0];
    const double distance = std::min(voltage - Vdip, Vup - voltage);
    roots[root] = distance + (voltageDipActive ? -hysteresis : hysteresis);
    roots[root + 1] = activeLimitHeld && !voltageDipActive ?
        static_cast<double>(activeLimitRelease - stateData.time) :
        1.0;
}

void REECA1::rootTrigger(CoreTime time,
                         const IOdata& inputs,
                         const std::vector<int>& rootMask,
                         const SolverMode& sMode)
{
    if (Thld2 <= 0.0) {
        return;
    }
    const auto root = offsets.getRootOffset(sMode);
    if (rootMask[root] != 0) {
        transition(time, inputs, rootMask[root] < 0);
    }
    if (rootMask[root + 1] != 0 && !voltageDipActive && activeLimitHeld &&
        time >= activeLimitRelease - 1e-9) {
        activeLimitHeld = false;
        alert(this, JAC_COUNT_CHANGE);
    }
    const auto current = commands(inputs, m_state.data() + 2);
    m_state[ipCommand] = current[0];
    m_state[iqCommand] = current[1];
}

ChangeCode REECA1::rootCheck(const IOdata& inputs,
                             const StateData& /*stateDataValue*/,
                             const SolverMode& /*sMode*/,
                             CheckLevel /*level*/)
{
    if (Thld2 <= 0.0 || voltageDip(inputs[0]) == voltageDipActive) {
        return ChangeCode::NO_CHANGE;
    }
    // Network events can jump across both voltage thresholds without a
    // continuous root crossing.  setState() has already synchronized m_state.
    transition(prevTime, inputs, voltageDip(inputs[0]));
    const auto current = commands(inputs, m_state.data() + 2);
    m_state[ipCommand] = current[0];
    m_state[iqCommand] = current[1];
    return ChangeCode::JACOBIAN_CHANGE;
}

stringVec REECA1::localStateNames() const
{
    stringVec stateNames;
    if (cascadedVoltageControl()) {
        stateNames = {"Ipcmd", "Iqcmd", "Vf", "Pf", "Pord", "Vpi", "Qpi"};
    } else if (QFLAG != 0) {
        stateNames = {"Ipcmd", "Iqcmd", "Vf", "Pf", "Pord", "Vpi"};
    } else {
        stateNames = {"Ipcmd", "Iqcmd", "Vf", "Pf", "Pord", "Qf"};
    }
    if (hasStorageSoc()) {
        stateNames.emplace_back("SOC");
    }
    if (PFFLAG == 1 && supportsPowerFactorControl()) {
        stateNames.emplace_back("PgenFilter");
    }
    return stateNames;
}
}  // namespace griddyn
