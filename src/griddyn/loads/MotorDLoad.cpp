/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "MotorDLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn::loads {
namespace {
    TypeFactory<MotorDLoad> gMotorDFactory("load",
                                           std::to_array<std::string_view>({"motord", "motor_d"}));

    struct MotorDPowerDerivatives {
        double mPVoltage = 0.0;
        double mQVoltage = 0.0;
        double mPFrequency = 0.0;
        double mQFrequency = 0.0;
    };

    MotorDPowerDerivatives characteristicDerivatives(double activePowerBase,
                                                     double compPF,
                                                     double voltage,
                                                     double frequencyDeviation,
                                                     double stallBreakVoltage,
                                                     double gStall,
                                                     double bStall)
    {
        constexpr double runVoltage = 0.86;
        const double reactivePowerBase = activePowerBase * std::tan(std::acos(compPF));
        const double reactivePowerAtRunVoltage =
            reactivePowerBase - (6.0 * std::pow(1.0 - runVoltage, 2.0));
        MotorDPowerDerivatives derivative;
        double pBase = activePowerBase;
        double qBase = reactivePowerAtRunVoltage + (6.0 * std::pow(voltage - runVoltage, 2.0));
        double dpDv = 0.0;
        double dqDv = 12.0 * (voltage - runVoltage);
        if (voltage <= stallBreakVoltage) {
            derivative.mPVoltage = 2.0 * gStall * voltage;
            derivative.mQVoltage = -2.0 * bStall * voltage;
            return derivative;
        }
        if (voltage <= runVoltage) {
            const double voltageDrop = std::max(0.0, runVoltage - voltage);
            pBase += 12.0 * std::pow(voltageDrop, 3.2);
            qBase = reactivePowerAtRunVoltage + (11.0 * std::pow(voltageDrop, 2.5));
            dpDv = -38.4 * std::pow(voltageDrop, 2.2);
            dqDv = -27.5 * std::pow(voltageDrop, 1.5);
        }
        derivative.mPVoltage = dpDv * (1.0 + frequencyDeviation);
        derivative.mQVoltage = dqDv * (1.0 - (3.3 * frequencyDeviation));
        derivative.mPFrequency = pBase;
        derivative.mQFrequency = -3.3 * qBase;
        return derivative;
    }

}  // namespace

const ModelDescription& MotorDLoad::getModelDescription() const
{
    static const auto description = ModelDescription::define<MotorDLoad>(
        "MotorDLoad",
        &GridLoad::getModelDescription(),
        {parameter({"lfm", "LFm"},
                   &MotorDLoad::loadFactor,
                   units::one,
                   "LFm: load MW / motor MVA rating",
                   parameterValidators::positive),
         parameter({"comppf", "CompPF"},
                   &MotorDLoad::compPF,
                   units::one,
                   "Comp pf",
                   [](double value) { return value > 0.0 && value <= 1.0; }),
         parameter({"vstall", "Vstall"},
                   &MotorDLoad::stallVoltage,
                   units::one,
                   "Stall voltage",
                   [](double value) { return value > 0.0 && value < 2.0; }),
         parameter({"rstall", "Rstall"},
                   &MotorDLoad::stallResistance,
                   units::one,
                   "Stall resistance",
                   parameterValidators::positive),
         parameter({"xstall", "Xstall"},
                   &MotorDLoad::stallReactance,
                   units::one,
                   "Stall reactance",
                   parameterValidators::positive),
         parameter({"tstall", "Tstall"}, &MotorDLoad::stallDelay, units::second, "Stall delay"),
         parameter({"vrst", "Vrst"},
                   &MotorDLoad::restartVoltage,
                   units::one,
                   "Restart voltage",
                   [](double value) { return value > 0.0 && value < 2.0; }),
         parameter({"trst", "Trst"},
                   &MotorDLoad::restartDelay,
                   units::second,
                   "Restart delay",
                   parameterValidators::nonnegative),
         parameter({"vtr1", "Vtr1"},
                   &MotorDLoad::tripVoltage1,
                   units::one,
                   "Trip voltage1",
                   parameterValidators::nonnegative),
         parameter({"ttr1", "Ttr1"},
                   &MotorDLoad::tripDelay1,
                   units::second,
                   "Trip delay1",
                   parameterValidators::nonnegative),
         parameter({"vtr2", "Vtr2"},
                   &MotorDLoad::tripVoltage2,
                   units::one,
                   "Trip voltage2",
                   parameterValidators::nonnegative),
         parameter({"ttr2", "Ttr2"},
                   &MotorDLoad::tripDelay2,
                   units::second,
                   "Trip delay2",
                   parameterValidators::nonnegative),
         parameter({"vc1off", "Vc1off"},
                   &MotorDLoad::contactorVoltageOff1,
                   units::one,
                   "Contactor voltage off1",
                   [](double value) { return value >= 0.0 && value < 2.0; }),
         parameter({"vc2off", "Vc2off"},
                   &MotorDLoad::contactorVoltageOff2,
                   units::one,
                   "Contactor voltage off2",
                   [](double value) { return value >= 0.0 && value < 2.0; }),
         parameter({"vc1on", "Vc1on"},
                   &MotorDLoad::contactorVoltageOn1,
                   units::one,
                   "Contactor voltage on1",
                   [](double value) { return value >= 0.0 && value < 2.0; }),
         parameter({"vc2on", "Vc2on"},
                   &MotorDLoad::contactorVoltageOn2,
                   units::one,
                   "Contactor voltage on2",
                   [](double value) { return value >= 0.0 && value < 2.0; }),
         parameter({"tth", "Tth"},
                   &MotorDLoad::thermalTimeConstant,
                   units::second,
                   "Thermal time constant",
                   parameterValidators::nonnegative),
         parameter({"th1t", "Th1t"},
                   &MotorDLoad::thermalTripStart,
                   units::one,
                   "Thermal trip start",
                   parameterValidators::nonnegative),
         parameter({"th2t", "Th2t"},
                   &MotorDLoad::thermalTripComplete,
                   units::one,
                   "Thermal trip complete",
                   parameterValidators::nonnegative),
         parameter({"tv", "Tv"},
                   &MotorDLoad::voltageMeasurementLag,
                   units::second,
                   "Voltage measurement lag",
                   parameterValidators::nonnegative),
         parameter({"frst", "Frst"},
                   &MotorDLoad::restartableFraction,
                   units::one,
                   "Restartable fraction",
                   parameterValidators::unitInterval),
         parameter({"fuvr", "Fuvr"},
                   &MotorDLoad::undervoltageFraction,
                   units::one,
                   "Undervoltage fraction",
                   parameterValidators::unitInterval),
         localState("hasStalled", &MotorDLoad::hasStalled, "Has stalled"),
         localState("restartablePartStalled",
                    &MotorDLoad::restartablePartStalled,
                    "Restartable part stalled"),
         localState("undervoltageTripped",
                    &MotorDLoad::undervoltageTripped,
                    "Undervoltage tripped"),
         localState("stallTimerActive", &MotorDLoad::stallTimerActive, "Stall timer active"),
         localState("restartTimerActive", &MotorDLoad::restartTimerActive, "Restart timer active"),
         localState("tripTimerActive1", &MotorDLoad::tripTimerActive1, "Trip timer active1"),
         localState("tripTimerActive2", &MotorDLoad::tripTimerActive2, "Trip timer active2"),
         localState("stallStart", &MotorDLoad::stallStart, "Stall start"),
         localState("restartStart", &MotorDLoad::restartStart, "Restart start"),
         localState("tripStart1", &MotorDLoad::tripStart1, "Trip start1"),
         localState("tripStart2", &MotorDLoad::tripStart2, "Trip start2"),
         localState("contactorFraction", &MotorDLoad::contactorFraction, "Contactor fraction"),
         localState("previousContactorVoltage",
                    &MotorDLoad::previousContactorVoltage,
                    "Previous contactor voltage"),
         localState("inverseTimeActive", &MotorDLoad::inverseTimeActive, "Inverse time active"),
         localState("inverseTimeStart", &MotorDLoad::inverseTimeStart, "Inverse time start"),
         localState("inverseTimeCycles", &MotorDLoad::inverseTimeCycles, "Inverse time cycles"),
         localState("inverseTimeAccumulation",
                    &MotorDLoad::inverseTimeAccumulation,
                    "Inverse time accumulation"),
         state("voltage_measurement",
               units::one,
               "Solver variable voltage_measurement when active"),
         state("thermal_a", units::one, "Solver variable thermal_a when active"),
         state("thermal_b", units::one, "Solver variable thermal_b when active")});
    return description;
}

MotorDLoad::MotorDLoad(const std::string& objName): GridLoad(objName)
{
    // The Motor D characteristic includes direct active and reactive power
    // response to bus frequency, so the owning bus must expose that signal.
    opFlags.set(USES_BUS_FREQUENCY);
}

CoreObject* MotorDLoad::clone(CoreObject* obj) const
{
    auto* copied = cloneBase<MotorDLoad, GridLoad>(this, obj);
    if (copied != nullptr) {
        copied->loadFactor = loadFactor;
        copied->compPF = compPF;
        copied->stallVoltage = stallVoltage;
        copied->stallResistance = stallResistance;
        copied->stallReactance = stallReactance;
        copied->stallDelay = stallDelay;
        copied->restartableFraction = restartableFraction;
        copied->restartVoltage = restartVoltage;
        copied->restartDelay = restartDelay;
        copied->undervoltageFraction = undervoltageFraction;
        copied->tripVoltage1 = tripVoltage1;
        copied->tripDelay1 = tripDelay1;
        copied->tripVoltage2 = tripVoltage2;
        copied->tripDelay2 = tripDelay2;
        copied->contactorVoltageOff1 = contactorVoltageOff1;
        copied->contactorVoltageOff2 = contactorVoltageOff2;
        copied->contactorVoltageOn1 = contactorVoltageOn1;
        copied->contactorVoltageOn2 = contactorVoltageOn2;
        copied->thermalTimeConstant = thermalTimeConstant;
        copied->thermalTripStart = thermalTripStart;
        copied->thermalTripComplete = thermalTripComplete;
        copied->voltageMeasurementLag = voltageMeasurementLag;
        copied->motorBaseScale = motorBaseScale;
        copied->stallBreak = stallBreak;
        copied->gStall = gStall;
        copied->bStall = bStall;
        copied->hasStalled = hasStalled;
        copied->restartablePartStalled = restartablePartStalled;
        copied->undervoltageTripped = undervoltageTripped;
        copied->stallTimerActive = stallTimerActive;
        copied->restartTimerActive = restartTimerActive;
        copied->tripTimerActive1 = tripTimerActive1;
        copied->tripTimerActive2 = tripTimerActive2;
        copied->stallStart = stallStart;
        copied->restartStart = restartStart;
        copied->tripStart1 = tripStart1;
        copied->tripStart2 = tripStart2;
        copied->contactorFraction = contactorFraction;
        copied->previousContactorVoltage = previousContactorVoltage;
        copied->inverseTimeActive = inverseTimeActive;
        copied->inverseTimeStart = inverseTimeStart;
        copied->inverseTimeCycles = inverseTimeCycles;
        copied->inverseTimeAccumulation = inverseTimeAccumulation;
    }
    return (copied == nullptr) ? obj : copied;
}

void MotorDLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = MotorDLoad::getModelDescription();
    getParamString<MotorDLoad, GridLoad>(this,
                                         pstr,
                                         description.numericNames(),
                                         description.stringNames(),
                                         description.flagNames(),
                                         pstype);
}

void MotorDLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}

void MotorDLoad::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(MotorDLoad::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        return;
    }
    GridLoad::set(param, val, unitType);
}

double MotorDLoad::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = MotorDLoad::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }

    return GridLoad::get(param, unitType);
}

void MotorDLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const bool thermalConfigured =
        (thermalTimeConstant > 0.0) || (thermalTripStart > 0.0) || (thermalTripComplete > 0.0);
    if (thermalConfigured &&
        ((thermalTimeConstant <= 0.0) || (thermalTripComplete <= thermalTripStart))) {
        throw InvalidParameterValue("Motor D thermal protection requires Tth > 0 and Th2t > Th1t");
    }
    if ((contactorVoltageOff1 > 0.0) || (contactorVoltageOff2 > 0.0) ||
        (contactorVoltageOn1 > 0.0) || (contactorVoltageOn2 > 0.0)) {
        if (!contactorProtectionEnabled()) {
            throw InvalidParameterValue(
                "Motor D contactor voltages must satisfy Vc1off > Vc2off and Vc1on > Vc2on");
        }
    }
    motorBaseScale = (getP() > 0.0) ? getP() / loadFactor : 1.0;
    const double impedanceSquared =
        (stallResistance * stallResistance) + (stallReactance * stallReactance);
    gStall = stallResistance / impedanceSquared;
    bStall = -stallReactance / impedanceSquared;
    computeStallBreak();
    GridLoad::pFlowObjectInitializeA(time0, flags);
}

void MotorDLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    GridLoad::dynObjectInitializeA(time0, flags);
    loadStateSizes(cLocalSolverMode);
    loadJacobianSizes(cLocalSolverMode);
    m_state.resize(offsets.getOffsets(cLocalSolverMode).local.diffSize, 0.0);
    m_dstate_dt.resize(m_state.size(), 0.0);
    hasStalled = false;
    restartablePartStalled = false;
    undervoltageTripped = false;
    stallTimerActive = false;
    restartTimerActive = false;
    tripTimerActive1 = false;
    tripTimerActive2 = false;
    const double voltage = bus->getVoltage();
    const double frequency = bus->getFreq();
    if (stallDelay >= 0.0 && voltage < stallVoltage) {
        stallTimerActive = stallDelay > 0.0;
        stallStart = time0;
        if (stallDelay == 0.0) {
            hasStalled = true;
            restartablePartStalled = restartableFraction > 0.0;
        }
    }
    if (undervoltageFraction > 0.0) {
        if (voltage < tripVoltage1 && tripVoltage1 < 2.0) {
            tripTimerActive1 = tripDelay1 > 0.0;
            tripStart1 = time0;
            if (tripDelay1 == 0.0) {
                undervoltageTripped = true;
            }
        }
        if (voltage < tripVoltage2 && tripVoltage2 < 2.0) {
            tripTimerActive2 = tripDelay2 > 0.0;
            tripStart2 = time0;
            if (tripDelay2 == 0.0) {
                undervoltageTripped = true;
            }
        }
    }
    contactorFraction = 1.0;
    previousContactorVoltage = voltage;
    if (contactorProtectionEnabled() && (voltage < contactorVoltageOff1)) {
        contactorFraction = std::clamp((voltage - contactorVoltageOff2) /
                                           (contactorVoltageOff1 - contactorVoltageOff2),
                                       0.0,
                                       1.0);
    }
    inverseTimeActive = false;
    inverseTimeStart = time0;
    inverseTimeAccumulation = 0.0;
    inverseTimeCycles = 0.0;
    const auto voltageIndex = voltageStateIndex();
    if (voltageIndex != kNullLocation) {
        m_state[voltageIndex] = voltage;
    }
    const auto thermalAIndex = thermalAStateIndex();
    const auto thermalBIndex = thermalBStateIndex();
    if (thermalAIndex != kNullLocation) {
        m_state[thermalAIndex] = thermalHeating(voltage, frequency, false);
        m_state[thermalBIndex] = m_state[thermalAIndex];
    }
}

StateSizes MotorDLoad::localStateSizes(const SolverMode& sMode) const
{
    StateSizes sizes;
    if (isDynamic(sMode) && !isAlgebraicOnly(sMode)) {
        sizes.diffSize = static_cast<count_t>((voltageMeasurementLag > 0.0 ? 1 : 0) +
                                              (thermalProtectionEnabled() ? 2 : 0));
    }
    return sizes;
}

count_t MotorDLoad::localJacobianCount(const SolverMode& sMode) const
{
    if (!isDynamic(sMode) || isAlgebraicOnly(sMode)) {
        return 0;
    }
    const count_t stateCount = localStateSizes(sMode).diffSize;
    return stateCount + (voltageMeasurementLag > 0.0 ? 1 : 0) +
        (thermalProtectionEnabled() ? 4 : 0);
}

stringVec MotorDLoad::localStateNames() const
{
    stringVec names;
    if (voltageMeasurementLag > 0.0) {
        names.emplace_back("voltage_measurement");
    }
    if (thermalProtectionEnabled()) {
        names.emplace_back("thermal_a");
        names.emplace_back("thermal_b");
    }
    return names;
}

index_t MotorDLoad::voltageStateIndex() const
{
    return (voltageMeasurementLag > 0.0) ? 0 : kNullLocation;
}

index_t MotorDLoad::thermalAStateIndex() const
{
    if (!thermalProtectionEnabled()) {
        return kNullLocation;
    }
    return (voltageMeasurementLag > 0.0) ? 1 : 0;
}

index_t MotorDLoad::thermalBStateIndex() const
{
    return thermalProtectionEnabled() ? thermalAStateIndex() + 1 : kNullLocation;
}

double MotorDLoad::stateValue(const StateData& stateData,
                              const SolverMode& sMode,
                              index_t stateIndex) const
{
    if ((stateIndex < 0) || (stateIndex == kNullLocation)) {
        return 0.0;
    }
    if ((stateData.state == nullptr) || !hasDifferential(sMode)) {
        return memberStateValue(stateIndex);
    }
    const index_t diffOffset = offsets.getDiffOffset(sMode);
    if ((diffOffset < 0) || (diffOffset == kNullLocation)) {
        return memberStateValue(stateIndex);
    }
    const auto vectorIndex = static_cast<count_t>(diffOffset) + static_cast<count_t>(stateIndex);
    if ((stateData.stateSize > 0) && (vectorIndex >= stateData.stateSize)) {
        return memberStateValue(stateIndex);
    }
    return stateData.state[vectorIndex];
}

double MotorDLoad::memberStateValue(index_t stateIndex) const
{
    if (stateIndex < 0) {
        return 0.0;
    }
    const auto localStateIndex = static_cast<std::size_t>(stateIndex);
    if (localStateIndex >= m_state.size()) {
        return 0.0;
    }
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    const auto localIndex = (diffOffset == kNullLocation || diffOffset < 0) ?
        localStateIndex :
        static_cast<std::size_t>(diffOffset) + localStateIndex;
    return (localIndex < m_state.size()) ? m_state[localIndex] : 0.0;
}

bool MotorDLoad::contactorProtectionEnabled() const
{
    return (contactorVoltageOff1 > contactorVoltageOff2) &&
        (contactorVoltageOn1 > contactorVoltageOn2) && (contactorVoltageOff1 > 0.0) &&
        (contactorVoltageOn1 > 0.0);
}

bool MotorDLoad::thermalProtectionEnabled() const
{
    return (thermalTimeConstant > 0.0) && (thermalTripComplete > thermalTripStart);
}

double MotorDLoad::thermalFraction(double temperature) const
{
    if (!thermalProtectionEnabled()) {
        return 1.0;
    }
    return thermalOnlineFraction(temperature, thermalTripStart, thermalTripComplete);
}

std::pair<count_t, count_t> MotorDLoad::LocalRootCount(const SolverMode& /*sMode*/) const
{
    return {0, 8};
}

void MotorDLoad::rootTest(const IOdata& inputs,
                          const StateData& stateData,
                          double roots[],
                          const SolverMode& sMode)
{
    const auto rootOffset = offsets.getRootOffset(sMode);
    const double voltage = measuredVoltage(inputs, stateData, sMode);
    const bool monitorStall = !restartablePartStalled &&
        (!hasStalled || (restartableFraction > 0.0)) && (stallVoltage < 2.0) && (stallDelay >= 0.0);
    roots[rootOffset] = monitorStall ? voltage - stallVoltage : 1.0;
    roots[rootOffset + 1] = stallTimerActive && (stallDelay >= 0.0) ?
        static_cast<double>(stateData.time - stallStart) - stallDelay :
        1.0;
    roots[rootOffset + 2] =
        (restartablePartStalled && restartableFraction > 0.0) ? voltage - restartVoltage : 1.0;
    roots[rootOffset + 3] = restartTimerActive ?
        static_cast<double>(stateData.time - restartStart) - restartDelay :
        1.0;
    roots[rootOffset + 4] =
        (!undervoltageTripped && (undervoltageFraction > 0.0) && (tripVoltage1 < 2.0)) ?
        voltage - tripVoltage1 :
        1.0;
    roots[rootOffset + 5] =
        tripTimerActive1 ? static_cast<double>(stateData.time - tripStart1) - tripDelay1 : 1.0;
    roots[rootOffset + 6] =
        (!undervoltageTripped && (undervoltageFraction > 0.0) && (tripVoltage2 < 2.0)) ?
        voltage - tripVoltage2 :
        1.0;
    roots[rootOffset + 7] =
        tripTimerActive2 ? static_cast<double>(stateData.time - tripStart2) - tripDelay2 : 1.0;
}

void MotorDLoad::rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode)
{
    const auto rootOffset = offsets.getRootOffset(sMode);
    const double voltage = measuredVoltage(inputs, emptyStateData, sMode);
    const bool wasStalled = hasStalled;
    const bool wasRestartablePartStalled = restartablePartStalled;
    const bool wasUndervoltageTripped = undervoltageTripped;
    const auto isTriggered = [&rootMask, rootOffset](index_t root) {
        if (rootOffset < 0 || root < 0) {
            return false;
        }
        const auto rootIndex =
            static_cast<std::size_t>(rootOffset) + static_cast<std::size_t>(root);
        return (rootIndex < rootMask.size()) && (rootMask[rootIndex] != 0);
    };

    if (stallDelay >= 0.0 && isTriggered(0) && !restartablePartStalled) {
        if (voltage < stallVoltage) {
            stallTimerActive = stallDelay > 0.0;
            stallStart = time;
            if (stallDelay == 0.0) {
                hasStalled = true;
                restartablePartStalled = restartableFraction > 0.0;
            }
        } else {
            stallTimerActive = false;
        }
    }
    if (stallDelay >= 0.0 && isTriggered(1) && stallTimerActive && (voltage < stallVoltage) &&
        (static_cast<double>(time - stallStart) >= stallDelay)) {
        stallTimerActive = false;
        hasStalled = true;
        restartablePartStalled = restartableFraction > 0.0;
    }
    if (isTriggered(2) && restartablePartStalled) {
        if (voltage > restartVoltage) {
            restartTimerActive = restartDelay > 0.0;
            restartStart = time;
            if (restartDelay == 0.0) {
                restartablePartStalled = false;
            }
        } else {
            restartTimerActive = false;
        }
    }
    if (isTriggered(3) && restartTimerActive && restartablePartStalled &&
        (voltage > restartVoltage) && (static_cast<double>(time - restartStart) >= restartDelay)) {
        restartTimerActive = false;
        restartablePartStalled = false;
    }
    if (!undervoltageTripped && (undervoltageFraction > 0.0)) {
        if (isTriggered(4)) {
            if (voltage < tripVoltage1 && tripVoltage1 < 2.0) {
                tripTimerActive1 = tripDelay1 > 0.0;
                tripStart1 = time;
                if (tripDelay1 == 0.0) {
                    undervoltageTripped = true;
                }
            } else {
                tripTimerActive1 = false;
            }
        }
        if (isTriggered(5) && tripTimerActive1 && (voltage < tripVoltage1) &&
            (static_cast<double>(time - tripStart1) >= tripDelay1)) {
            undervoltageTripped = true;
            tripTimerActive1 = false;
            tripTimerActive2 = false;
        }
        if (isTriggered(6)) {
            if (voltage < tripVoltage2 && tripVoltage2 < 2.0) {
                tripTimerActive2 = tripDelay2 > 0.0;
                tripStart2 = time;
                if (tripDelay2 == 0.0) {
                    undervoltageTripped = true;
                }
            } else {
                tripTimerActive2 = false;
            }
        }
        if (isTriggered(7) && tripTimerActive2 && (voltage < tripVoltage2) &&
            (static_cast<double>(time - tripStart2) >= tripDelay2)) {
            undervoltageTripped = true;
            tripTimerActive1 = false;
            tripTimerActive2 = false;
        }
    }
    if ((wasStalled != hasStalled) || (wasRestartablePartStalled != restartablePartStalled) ||
        (wasUndervoltageTripped != undervoltageTripped)) {
        alert(this, POTENTIAL_FAULT_CHANGE);
    }
}

void MotorDLoad::computeStallBreak()
{
    constexpr double lowSearchVoltage = 0.4;
    constexpr double runVoltage = 0.86;
    const double activePowerBase = loadFactor;
    const auto difference = [activePowerBase, this](double voltage) {
        const double compressorPower =
            activePowerBase + (12.0 * std::pow(std::max(0.0, 0.86 - voltage), 3.2));
        return compressorPower - (gStall * voltage * voltage);
    };
    double lower = lowSearchVoltage;
    double upper = std::min(stallVoltage, runVoltage);
    if (upper <= lower) {
        stallBreak = upper;
        return;
    }
    const double lowerDifference = difference(lower);
    const double upperDifference = difference(upper);
    if (lowerDifference <= 0.0) {
        stallBreak = lower;
        return;
    }
    if (upperDifference > 0.0) {
        stallBreak = upper;
        return;
    }
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double middle = 0.5 * (lower + upper);
        if (difference(middle) > 0.0) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    stallBreak = 0.5 * (lower + upper);
}

MotorDPower MotorDLoad::characteristicPower(double activePowerBase,
                                            double compressorPowerFactor,
                                            double voltage,
                                            double frequencyDeviation,
                                            double stallBreakVoltage,
                                            double stallConductance,
                                            double stallSusceptance)
{
    constexpr double runVoltage = 0.86;
    if (voltage <= stallBreakVoltage) {
        return {.p = stallConductance * voltage * voltage,
                .q = -stallSusceptance * voltage * voltage};
    }
    const double reactivePowerBase = activePowerBase * std::tan(std::acos(compressorPowerFactor));
    const double reactivePowerAtRunVoltage =
        reactivePowerBase - (6.0 * std::pow(1.0 - runVoltage, 2.0));
    double pBase = activePowerBase;
    double qBase = reactivePowerAtRunVoltage + (6.0 * std::pow(voltage - runVoltage, 2.0));
    if (voltage <= runVoltage) {
        const double voltageDrop = std::max(0.0, runVoltage - voltage);
        pBase += 12.0 * std::pow(voltageDrop, 3.2);
        qBase = reactivePowerAtRunVoltage + (11.0 * std::pow(voltageDrop, 2.5));
    }
    return {.p = pBase * (1.0 + frequencyDeviation),
            .q = qBase * (1.0 - (3.3 * frequencyDeviation))};
}

double MotorDLoad::inverseStallCycles(double voltage)
{
    if (voltage < 0.45) {
        return 2.0;
    }
    if (voltage < 0.49) {
        return 2.0 + ((voltage - 0.45) * (1.0 / 0.04));
    }
    if (voltage < 0.55) {
        return 3.0 + ((voltage - 0.49) * (9.0 / 0.06));
    }
    if (voltage < 0.565) {
        return 12.0 + ((voltage - 0.55) * (3.0 / 0.015));
    }
    return 1.0e6;
}

double MotorDLoad::thermalOnlineFraction(double temperature, double tripStart, double tripComplete)
{
    if (tripComplete <= tripStart) {
        return 1.0;
    }
    if (temperature <= tripStart) {
        return 1.0;
    }
    if (temperature >= tripComplete) {
        return 0.0;
    }
    return (tripComplete - temperature) / (tripComplete - tripStart);
}

double MotorDLoad::measuredVoltage(const IOdata& inputs,
                                   const StateData& stateData,
                                   const SolverMode& sMode) const
{
    const auto stateIndex = voltageStateIndex();
    return (stateIndex != kNullLocation) ? stateValue(stateData, sMode, stateIndex) :
                                           voltageInput(inputs, stateData, sMode);
}

double MotorDLoad::thermalHeating(double voltage, double frequency, bool stalled) const
{
    if (std::abs(voltage) < 1.0e-8) {
        return 0.0;
    }
    const double activePowerBase = (motorBaseScale > 0.0) ? getP() / motorBaseScale : 0.0;
    const auto run = characteristicPower(
        activePowerBase, compPF, voltage, frequency - 1.0, stallBreak, gStall, bStall);
    const MotorDPower activePower = stalled ?
        MotorDPower{.p = gStall * voltage * voltage, .q = -bStall * voltage * voltage} :
        run;
    return (((activePower.p * activePower.p) + (activePower.q * activePower.q)) /
            (voltage * voltage)) *
        stallResistance;
}

void MotorDLoad::updateContactor(double voltage)
{
    const double previousFraction = contactorFraction;
    if (!contactorProtectionEnabled()) {
        contactorFraction = 1.0;
        previousContactorVoltage = voltage;
        return;
    }
    if (voltage < previousContactorVoltage) {
        const double offFraction = std::clamp((voltage - contactorVoltageOff2) /
                                                  (contactorVoltageOff1 - contactorVoltageOff2),
                                              0.0,
                                              1.0);
        contactorFraction = std::min(contactorFraction, offFraction);
    } else if (voltage > previousContactorVoltage) {
        const double onFraction = std::clamp((voltage - contactorVoltageOn2) /
                                                 (contactorVoltageOn1 - contactorVoltageOn2),
                                             0.0,
                                             1.0);
        contactorFraction = std::max(contactorFraction, onFraction);
    }
    previousContactorVoltage = voltage;
    if (previousFraction != contactorFraction) {
        alert(this, POTENTIAL_FAULT_CHANGE);
    }
}

void MotorDLoad::updateInverseTimeStall(CoreTime time, double voltage)
{
    if (stallDelay >= 0.0 || hasStalled) {
        return;
    }
    if (voltage >= 0.565) {
        inverseTimeAccumulation = 0.0;
        inverseTimeActive = false;
        inverseTimeStart = time;
        inverseTimeCycles = 0.0;
        return;
    }
    const double cycles = inverseStallCycles(voltage);
    if (!inverseTimeActive) {
        inverseTimeActive = true;
        inverseTimeStart = time;
        inverseTimeCycles = cycles;
        return;
    }
    const double elapsed = static_cast<double>(time - inverseTimeStart);
    if (elapsed > 0.0) {
        const double nominalFrequency = systemBaseFrequency / (2.0 * std::numbers::pi);
        inverseTimeAccumulation +=
            elapsed * nominalFrequency * 0.5 * ((1.0 / cycles) + (1.0 / inverseTimeCycles));
        inverseTimeStart = time;
        inverseTimeCycles = cycles;
    }
    if (inverseTimeAccumulation >= 1.0) {
        hasStalled = true;
        restartablePartStalled = restartableFraction > 0.0;
        inverseTimeActive = false;
        alert(this, POTENTIAL_FAULT_CHANGE);
    }
}

MotorDPower MotorDLoad::modelPower(double voltage, double frequency) const
{
    return modelPower(voltage,
                      frequency,
                      thermalFraction(memberStateValue(thermalAStateIndex())),
                      thermalFraction(memberStateValue(thermalBStateIndex())));
}

MotorDPower MotorDLoad::modelPower(double voltage,
                                   double frequency,
                                   double thermalFractionA,
                                   double thermalFractionB) const
{
    const double activePowerBase = (motorBaseScale > 0.0) ? getP() / motorBaseScale : 0.0;
    const auto runningPower = characteristicPower(
        activePowerBase, compPF, voltage, frequency - 1.0, stallBreak, gStall, bStall);
    const MotorDPower lockedRotorPower{.p = gStall * voltage * voltage,
                                       .q = -bStall * voltage * voltage};
    const MotorDPower motorA = hasStalled ? lockedRotorPower : runningPower;
    const MotorDPower motorB =
        (hasStalled && restartablePartStalled) ? lockedRotorPower : runningPower;
    MotorDPower selectedPower{.p = ((1.0 - restartableFraction) * thermalFractionA * motorA.p) +
                                  (restartableFraction * thermalFractionB * motorB.p),
                              .q = ((1.0 - restartableFraction) * thermalFractionA * motorA.q) +
                                  (restartableFraction * thermalFractionB * motorB.q)};
    const double onlineFraction = undervoltageTripped ? 1.0 - undervoltageFraction : 1.0;
    selectedPower.p *= motorBaseScale * onlineFraction * contactorFraction;
    selectedPower.q *= motorBaseScale * onlineFraction * contactorFraction;
    return selectedPower;
}

double MotorDLoad::voltageInput(const IOdata& inputs,
                                const StateData& stateData,
                                const SolverMode& sMode) const
{
    return inputs.empty() ? bus->getVoltage(stateData, sMode) : inputs[VOLTAGE_IN_LOCATION];
}

double MotorDLoad::frequencyInput(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) const
{
    return (inputs.size() > FREQUENCY_IN_LOCATION) ? inputs[FREQUENCY_IN_LOCATION] :
                                                     bus->getFreq(stateData, sMode);
}

void MotorDLoad::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    GridLoad::timestep(time, inputs, sMode);
    const StateData stateData(time, m_state.data());
    const double voltage = measuredVoltage(inputs, stateData, cLocalSolverMode);
    updateContactor(voltage);
    updateInverseTimeStall(time, voltage);
}

void MotorDLoad::residual(const IOdata& inputs,
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode)
{
    if (!isDynamic(sMode) || isAlgebraicOnly(sMode) || (localStateSizes(sMode).diffSize == 0)) {
        return;
    }
    auto loc = offsets.getLocations(stateData, resid, sMode, this);
    derivative(inputs, stateData, resid, sMode);
    for (index_t ii = 0; ii < localStateSizes(sMode).diffSize; ++ii) {
        loc.destDiffLoc[ii] -= loc.dstateLoc[ii];
    }
}

void MotorDLoad::derivative(const IOdata& inputs,
                            const StateData& stateData,
                            double deriv[],
                            const SolverMode& sMode)
{
    const auto stateCount = localStateSizes(sMode).diffSize;
    if (stateCount == 0) {
        return;
    }
    auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    auto* derivativeValues = loc.destDiffLoc;
    const double voltage = voltageInput(inputs, stateData, sMode);
    const double frequency = frequencyInput(inputs, stateData, sMode);
    const auto voltageIndex = voltageStateIndex();
    if (voltageIndex != kNullLocation) {
        derivativeValues[voltageIndex] =
            (voltage - loc.diffStateLoc[voltageIndex]) / voltageMeasurementLag;
    }
    const auto thermalAIndex = thermalAStateIndex();
    if (thermalAIndex != kNullLocation) {
        const double heatingA = thermalHeating(voltage, frequency, hasStalled);
        const double heatingB =
            thermalHeating(voltage, frequency, hasStalled && restartablePartStalled);
        derivativeValues[thermalAIndex] =
            (heatingA - loc.diffStateLoc[thermalAIndex]) / thermalTimeConstant;
        derivativeValues[thermalBStateIndex()] =
            (heatingB - loc.diffStateLoc[thermalBStateIndex()]) / thermalTimeConstant;
    }
}

void MotorDLoad::jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode)
{
    const auto stateCount = localStateSizes(sMode).diffSize;
    if (stateCount == 0) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    const auto frequencyLoc = (inputLocs.size() > FREQUENCY_IN_LOCATION) ?
        inputLocs[FREQUENCY_IN_LOCATION] :
        kNullLocation;
    const double voltage = voltageInput(inputs, stateData, sMode);
    const double frequency = frequencyInput(inputs, stateData, sMode);
    const auto voltageIndex = voltageStateIndex();
    if (voltageIndex != kNullLocation) {
        const auto row = loc.diffOffset + voltageIndex;
        matrixData.assign(row, row, (-1.0 / voltageMeasurementLag) - stateData.cj);
        if (voltageLoc != kNullLocation) {
            matrixData.assign(row, voltageLoc, 1.0 / voltageMeasurementLag);
        }
    }
    const auto thermalAIndex = thermalAStateIndex();
    if (thermalAIndex == kNullLocation) {
        return;
    }

    const auto thermalBIndex = thermalBStateIndex();
    const double voltageStep = std::max(1.0e-6, std::abs(voltage) * 1.0e-6);
    const double frequencyStep = 1.0e-6;
    const auto heatVoltageDerivative = [&](bool stalled) {
        const double upper = thermalHeating(voltage + voltageStep, frequency, stalled);
        const double lower =
            thermalHeating(std::max(0.0, voltage - voltageStep), frequency, stalled);
        const double denominator = (voltage >= voltageStep) ? 2.0 * voltageStep : voltageStep;
        return (upper - lower) / denominator;
    };
    const auto heatFrequencyDerivative = [&](bool stalled) {
        const double upper = thermalHeating(voltage, frequency + frequencyStep, stalled);
        const double lower = thermalHeating(voltage, frequency - frequencyStep, stalled);
        return (upper - lower) / (2.0 * frequencyStep);
    };
    const std::array<bool, 2> isStalled{hasStalled, hasStalled && restartablePartStalled};
    const std::array<index_t, 2> thermalIndices{thermalAIndex, thermalBIndex};
    for (index_t part = 0; part < 2; ++part) {
        const auto row = loc.diffOffset + thermalIndices[part];
        matrixData.assign(row, row, (-1.0 / thermalTimeConstant) - stateData.cj);
        const double dHeatingDv = heatVoltageDerivative(isStalled[part]) / thermalTimeConstant;
        if (voltageLoc != kNullLocation) {
            matrixData.assign(row, voltageLoc, dHeatingDv);
        }
        if (frequencyLoc != kNullLocation) {
            matrixData.assign(row,
                              frequencyLoc,
                              heatFrequencyDerivative(isStalled[part]) / thermalTimeConstant);
        }
    }
}

void MotorDLoad::outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateData,
                                          MatrixData<double>& matrixData,
                                          const SolverMode& sMode)
{
    const auto thermalAIndex = thermalAStateIndex();
    const auto thermalBIndex = thermalBStateIndex();
    if (thermalAIndex == kNullLocation || !isDynamic(sMode) || isAlgebraicOnly(sMode)) {
        return;
    }
    const double voltage = voltageInput(inputs, stateData, sMode);
    const double frequency = frequencyInput(inputs, stateData, sMode);
    const double activePowerBase = (motorBaseScale > 0.0) ? getP() / motorBaseScale : 0.0;
    const auto run = characteristicPower(
        activePowerBase, compPF, voltage, frequency - 1.0, stallBreak, gStall, bStall);
    const MotorDPower locked{.p = gStall * voltage * voltage, .q = -bStall * voltage * voltage};
    const MotorDPower motorA = hasStalled ? locked : run;
    const MotorDPower motorB = (hasStalled && restartablePartStalled) ? locked : run;
    const double uvFraction = undervoltageTripped ? 1.0 - undervoltageFraction : 1.0;
    const double outputScale = motorBaseScale * uvFraction * contactorFraction;
    const double thermalRange = thermalTripComplete - thermalTripStart;
    const auto dThermalA = (thermalFraction(stateValue(stateData, sMode, thermalAIndex)) > 0.0 &&
                            thermalFraction(stateValue(stateData, sMode, thermalAIndex)) < 1.0) ?
        -1.0 / thermalRange :
        0.0;
    const auto dThermalB = (thermalFraction(stateValue(stateData, sMode, thermalBIndex)) > 0.0 &&
                            thermalFraction(stateValue(stateData, sMode, thermalBIndex)) < 1.0) ?
        -1.0 / thermalRange :
        0.0;
    const auto diffOffset = offsets.getDiffOffset(sMode);
    const auto aLoc = diffOffset + thermalAIndex;
    const auto block = diffOffset + thermalBIndex;
    matrixData.assign(POUT_LOCATION,
                      aLoc,
                      outputScale * (1.0 - restartableFraction) * motorA.p * dThermalA);
    matrixData.assign(QOUT_LOCATION,
                      aLoc,
                      outputScale * (1.0 - restartableFraction) * motorA.q * dThermalA);
    matrixData.assign(POUT_LOCATION,
                      block,
                      outputScale * restartableFraction * motorB.p * dThermalB);
    matrixData.assign(QOUT_LOCATION,
                      block,
                      outputScale * restartableFraction * motorB.q * dThermalB);
}

double MotorDLoad::getRealPower(const IOdata& inputs,
                                const StateData& stateData,
                                const SolverMode& sMode) const
{
    if (!isConnected() || !isEnabled()) {
        return 0.0;
    }
    return modelPower(voltageInput(inputs, stateData, sMode),
                      frequencyInput(inputs, stateData, sMode),
                      thermalFraction(stateValue(stateData, sMode, thermalAStateIndex())),
                      thermalFraction(stateValue(stateData, sMode, thermalBStateIndex())))
        .p;
}

double MotorDLoad::getReactivePower(const IOdata& inputs,
                                    const StateData& stateData,
                                    const SolverMode& sMode) const
{
    if (!isConnected() || !isEnabled()) {
        return 0.0;
    }
    return modelPower(voltageInput(inputs, stateData, sMode),
                      frequencyInput(inputs, stateData, sMode),
                      thermalFraction(stateValue(stateData, sMode, thermalAStateIndex())),
                      thermalFraction(stateValue(stateData, sMode, thermalBStateIndex())))
        .q;
}

double MotorDLoad::getRealPower(double voltage) const
{
    if (!isConnected() || !isEnabled()) {
        return 0.0;
    }
    const double frequency = (bus != nullptr) ? bus->getFreq() : 1.0;
    return modelPower(voltage, frequency).p;
}

double MotorDLoad::getReactivePower(double voltage) const
{
    if (!isConnected() || !isEnabled()) {
        return 0.0;
    }
    const double frequency = (bus != nullptr) ? bus->getFreq() : 1.0;
    return modelPower(voltage, frequency).q;
}

double MotorDLoad::getRealPower() const
{
    return (bus != nullptr) ? getRealPower(bus->getVoltage()) : 0.0;
}

double MotorDLoad::getReactivePower() const
{
    return (bus != nullptr) ? getReactivePower(bus->getVoltage()) : 0.0;
}

void MotorDLoad::ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    if (!isConnected() || !isEnabled()) {
        return;
    }
    const double voltage = voltageInput(inputs, stateData, sMode);
    const double frequency = frequencyInput(inputs, stateData, sMode);
    const double activePowerBase = (motorBaseScale > 0.0) ? getP() / motorBaseScale : 0.0;
    const double onlineFraction = undervoltageTripped ? 1.0 - undervoltageFraction : 1.0;
    auto derivatives = characteristicDerivatives(
        activePowerBase, compPF, voltage, frequency - 1.0, stallBreak, gStall, bStall);
    MotorDPowerDerivatives lockedDerivatives;
    lockedDerivatives.mPVoltage = 2.0 * gStall * voltage;
    lockedDerivatives.mQVoltage = -2.0 * bStall * voltage;
    const auto derivativeA = hasStalled ? lockedDerivatives : derivatives;
    const auto derivativeB =
        (hasStalled && restartablePartStalled) ? lockedDerivatives : derivatives;
    const double thermalA = thermalFraction(stateValue(stateData, sMode, thermalAStateIndex()));
    const double thermalB = thermalFraction(stateValue(stateData, sMode, thermalBStateIndex()));
    const double pVoltage = ((1.0 - restartableFraction) * thermalA * derivativeA.mPVoltage) +
        (restartableFraction * thermalB * derivativeB.mPVoltage);
    const double qVoltage = ((1.0 - restartableFraction) * thermalA * derivativeA.mQVoltage) +
        (restartableFraction * thermalB * derivativeB.mQVoltage);
    const double pFrequency = ((1.0 - restartableFraction) * thermalA * derivativeA.mPFrequency) +
        (restartableFraction * thermalB * derivativeB.mPFrequency);
    const double qFrequency = ((1.0 - restartableFraction) * thermalA * derivativeA.mQFrequency) +
        (restartableFraction * thermalB * derivativeB.mQFrequency);
    const double outputScale = motorBaseScale * onlineFraction * contactorFraction;
    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assignCheckCol(POUT_LOCATION,
                                  inputLocs[VOLTAGE_IN_LOCATION],
                                  pVoltage * outputScale);
        matrixData.assignCheckCol(QOUT_LOCATION,
                                  inputLocs[VOLTAGE_IN_LOCATION],
                                  qVoltage * outputScale);
    }
    if (inputLocs[FREQUENCY_IN_LOCATION] != kNullLocation) {
        matrixData.assignCheckCol(POUT_LOCATION,
                                  inputLocs[FREQUENCY_IN_LOCATION],
                                  pFrequency * outputScale);
        matrixData.assignCheckCol(QOUT_LOCATION,
                                  inputLocs[FREQUENCY_IN_LOCATION],
                                  qFrequency * outputScale);
    }
}

count_t MotorDLoad::outputDependencyCount(index_t /*outputNum*/, const SolverMode& sMode) const
{
    if (!isDynamic(sMode) || isAlgebraicOnly(sMode)) {
        return 0;
    }
    // In addition to thermal states, Motor D's output depends directly on the
    // bus frequency input. Count that possible column for sparse bus Jacobian
    // allocation, even when the current bus mode has no frequency state.
    return 1 + ((thermalAStateIndex() != kNullLocation) ? 2U : 0U);
}

}  // namespace griddyn::loads
