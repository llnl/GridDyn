/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DGProtectionRelay.h"

#include "../GridBus.h"
#include "../events/Event.h"
#include "../generators/RenewableGenerator.h"
#include "../measurement/Condition.h"
#include "../renewables/DistributedConverter.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace griddyn {
const ModelDescription& DGProtectionRelay::getModelDescription() const
{
    static const auto description = ModelDescription::define<DGProtectionRelay>(
        "DGProtectionRelay",
        &Relay::getModelDescription(),
        {command({"external_voltage", "v"}, units::defunit, "External voltage"),
         parameter({"tres"}, &DGProtectionRelay::resetTime, units::second, "Reset time"),
         parameter({"fn"}, &DGProtectionRelay::fn, units::Hz, "Nominal frequency"),
         flag({"fen"}, &DGProtectionRelay::frequencyEnabled, "Enable frequency protection"),
         flag({"ven"}, &DGProtectionRelay::voltageEnabled, "Enable voltage protection"),
         parameter({"fl3"}, &DGProtectionRelay::frequency, 0, units::Hz, "Frequency threshold"),
         parameter({"fl2"}, &DGProtectionRelay::frequency, 1, units::Hz, "Frequency threshold"),
         parameter({"fl1"}, &DGProtectionRelay::frequency, 2, units::Hz, "Frequency threshold"),
         parameter({"fu1"}, &DGProtectionRelay::frequency, 3, units::Hz, "Frequency threshold"),
         parameter({"fu2"}, &DGProtectionRelay::frequency, 4, units::Hz, "Frequency threshold"),
         parameter({"fu3"}, &DGProtectionRelay::frequency, 5, units::Hz, "Frequency threshold"),
         parameter({"vl4"}, &DGProtectionRelay::voltage, 0, units::puV, "Voltage threshold"),
         parameter({"vl3"}, &DGProtectionRelay::voltage, 1, units::puV, "Voltage threshold"),
         parameter({"vl2"}, &DGProtectionRelay::voltage, 2, units::puV, "Voltage threshold"),
         parameter({"vl1"}, &DGProtectionRelay::voltage, 3, units::puV, "Voltage threshold"),
         parameter({"vu1"}, &DGProtectionRelay::voltage, 4, units::puV, "Voltage threshold"),
         parameter({"vu2"}, &DGProtectionRelay::voltage, 5, units::puV, "Voltage threshold"),
         parameter({"vu3"}, &DGProtectionRelay::voltage, 6, units::puV, "Voltage threshold"),
         parameter(
             {"tfl1"}, &DGProtectionRelay::frequencyTime, 0, units::second, "Frequency trip delay"),
         parameter(
             {"tfl2"}, &DGProtectionRelay::frequencyTime, 1, units::second, "Frequency trip delay"),
         parameter(
             {"tfu1"}, &DGProtectionRelay::frequencyTime, 2, units::second, "Frequency trip delay"),
         parameter(
             {"tfu2"}, &DGProtectionRelay::frequencyTime, 3, units::second, "Frequency trip delay"),
         parameter(
             {"tvl1"}, &DGProtectionRelay::voltageTime, 0, units::second, "Voltage trip delay"),
         parameter(
             {"tvl2"}, &DGProtectionRelay::voltageTime, 1, units::second, "Voltage trip delay"),
         parameter(
             {"tvl3"}, &DGProtectionRelay::voltageTime, 2, units::second, "Voltage trip delay"),
         parameter(
             {"tvu1"}, &DGProtectionRelay::voltageTime, 3, units::second, "Voltage trip delay"),
         parameter(
             {"tvu2"}, &DGProtectionRelay::voltageTime, 4, units::second, "Voltage trip delay"),
         localState("locked", &DGProtectionRelay::lock, "Protection lockout latch"),
         localState("clear_deadline", &DGProtectionRelay::clearDeadline, "Earliest release time")});
    return description;
}

void DGProtectionRelay::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = DGProtectionRelay::getModelDescription();
    getParamString<DGProtectionRelay, Relay>(this,
                                             pstr,
                                             description.numericNames(),
                                             description.stringNames(),
                                             description.flagNames(),
                                             pstype);
}

DGProtectionRelay::DGProtectionRelay(bool useExternalVoltage, const std::string& objName):
    Relay(objName), externalVoltage(useExternalVoltage)
{
    opFlags.set(CONTINUOUS_FLAG);
    opFlags.set(RESETTABLE_FLAG);
}

void DGProtectionRelay::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (DGProtectionRelay::getModelDescription().trySetLocal(*this, key, val, unitType)) {
        return;
    }
    if (key == "external_voltage" || key == "v") {
        if (!externalVoltage) {
            throw InvalidParameterValue("DGPRCT1 uses bus voltage");
        }
        externalV = val;
        externalConfigured = true;
        return;
    }
    Relay::set(param, val, unitType);
}

void DGProtectionRelay::setFlag(std::string_view flag, bool val)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{flag});
    if (!DGProtectionRelay::getModelDescription().trySetFlagLocal(*this, key, val)) {
        Relay::setFlag(flag, val);
    }
}

bool DGProtectionRelay::getFlag(std::string_view flag) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{flag});
    if (const auto value = DGProtectionRelay::getModelDescription().tryGetFlagLocal(*this, key)) {
        return *value;
    }
    return Relay::getFlag(flag);
}

double DGProtectionRelay::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (const auto value =
            DGProtectionRelay::getModelDescription().tryGetLocal(*this, key, unitType)) {
        return *value;
    }
    if (key == "external_voltage" || key == "v") {
        return externalV;
    }
    if (key == "lock") {
        return lock ? 1.0 : 0.0;
    }
    return Relay::get(param, unitType);
}

void DGProtectionRelay::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    target = dynamic_cast<DistributedConverter*>(m_sinkObject);
    if (target == nullptr) {
        auto* generator = dynamic_cast<RenewableGenerator*>(m_sinkObject);
        if (generator != nullptr) {
            target = dynamic_cast<DistributedConverter*>(generator->find("electrical"));
        }
    }
    measuredBus = dynamic_cast<GridBus*>(m_sourceObject);
    if (measuredBus == nullptr && target != nullptr) {
        auto* generator = dynamic_cast<RenewableGenerator*>(target->getParent());
        measuredBus =
            generator == nullptr ? nullptr : dynamic_cast<GridBus*>(generator->getParent());
    }
    auto* generator =
        target == nullptr ? nullptr : dynamic_cast<RenewableGenerator*>(target->getParent());
    auto* terminalBus =
        generator == nullptr ? nullptr : dynamic_cast<GridBus*>(generator->getParent());
    if (target == nullptr || measuredBus == nullptr || terminalBus != measuredBus || fn <= 0.0 ||
        resetTime < 0.0 ||
        std::adjacent_find(frequency.begin(), frequency.end(), std::greater_equal<double>{}) !=
            frequency.end() ||
        std::adjacent_find(voltage.begin(), voltage.end(), std::greater_equal<double>{}) !=
            voltage.end() ||
        std::any_of(
            frequencyTime.begin(), frequencyTime.end(), [](double delay) { return delay < 0.0; }) ||
        std::any_of(
            voltageTime.begin(), voltageTime.end(), [](double delay) { return delay < 0.0; })) {
        throw InvalidParameterValue("DG protection target, source, thresholds, or delays");
    }
    if (externalVoltage && !externalConfigured) {
        externalV = measuredBus->getVoltage();
    }
    if (configuredConditions > 0) {
        Relay::pFlowObjectInitializeA(time0, flags);
        return;
    }

    auto trip = std::make_shared<Event>(0.0);
    trip->setTarget(target, "blocked");
    trip->setValue(1.0);
    add(trip);
    const auto addCondition = [&](std::string_view field,
                                  std::string_view comparison,
                                  double threshold,
                                  CoreTime delay,
                                  CoreObject* source) {
        add(std::shared_ptr<Condition>(
            makeCondition(std::string{field}, std::string{comparison}, threshold, source)));
        setActionTrigger(0, configuredConditions, delay);
        ++configuredConditions;
    };
    if (frequencyEnabled) {
        addCondition("frequency", "<", frequency[2] / fn, frequencyTime[0], measuredBus);
        addCondition("frequency", "<", frequency[1] / fn, frequencyTime[1], measuredBus);
        addCondition("frequency", "<", frequency[0] / fn, 0.0, measuredBus);
        addCondition("frequency", ">", frequency[3] / fn, frequencyTime[2], measuredBus);
        addCondition("frequency", ">", frequency[4] / fn, frequencyTime[3], measuredBus);
        addCondition("frequency", ">", frequency[5] / fn, 0.0, measuredBus);
    }
    if (voltageEnabled) {
        auto* source = externalVoltage ? static_cast<CoreObject*>(this) :
                                         static_cast<CoreObject*>(measuredBus);
        const std::string_view field = externalVoltage ? "external_voltage" : "voltage";
        addCondition(field, "<", voltage[3], voltageTime[0], source);
        addCondition(field, "<", voltage[2], voltageTime[1], source);
        addCondition(field, "<", voltage[1], voltageTime[2], source);
        addCondition(field, "<", voltage[0], 0.0, source);
        addCondition(field, ">", voltage[4], voltageTime[3], source);
        addCondition(field, ">", voltage[5], voltageTime[4], source);
        addCondition(field, ">", voltage[6], 0.0, source);
    }
    Relay::pFlowObjectInitializeA(time0, flags);
}

void DGProtectionRelay::updateA(CoreTime time)
{
    Relay::updateA(time);
    if (!lock) {
        return;
    }
    for (index_t condition = 0; condition < configuredConditions; ++condition) {
        if (checkCondition(condition)) {
            clearDeadline = maxTime;
            return;
        }
    }
    if (clearDeadline == maxTime) {
        clearDeadline = time + resetTime;
    }
    if (time >= clearDeadline) {
        target->set("blocked", 0.0);
        lock = false;
        clearDeadline = maxTime;
    } else {
        if (clearDeadline < nextUpdateTime) {
            nextUpdateTime = clearDeadline;
            alert(this, UPDATE_TIME_CHANGE);
        }
    }
}

void DGProtectionRelay::actionTaken(index_t /*actionNum*/,
                                    index_t /*conditionNum*/,
                                    ChangeCode /*actionReturn*/,
                                    CoreTime /*actionTime*/)
{
    lock = true;
    clearDeadline = maxTime;
}

void DGProtectionRelay::conditionCleared(index_t /*conditionNum*/, CoreTime timeCleared)
{
    if (!lock) {
        return;
    }
    for (index_t condition = 0; condition < configuredConditions; ++condition) {
        if (checkCondition(condition)) {
            return;
        }
    }
    clearDeadline = timeCleared + resetTime;
    if (clearDeadline < nextUpdateTime) {
        nextUpdateTime = clearDeadline;
        alert(this, UPDATE_TIME_CHANGE);
    }
}

void DGProtectionRelay::copyParametersTo(DGProtectionRelay* out) const
{
    out->externalConfigured = externalConfigured;
    out->externalV = externalV;
    out->lock = lock;
    out->frequencyEnabled = frequencyEnabled;
    out->voltageEnabled = voltageEnabled;
    out->fn = fn;
    out->resetTime = resetTime;
    out->clearDeadline = clearDeadline;
    out->frequency = frequency;
    out->voltage = voltage;
    out->frequencyTime = frequencyTime;
    out->voltageTime = voltageTime;
}

const ModelDescription& DGPRCT1::getModelDescription() const
{
    static const auto description =
        ModelDescription::define<DGPRCT1>("DGPRCT1", &DGProtectionRelay::getModelDescription(), {});
    return description;
}

DGPRCT1::DGPRCT1(const std::string& objName): DGProtectionRelay(false, objName) {}
CoreObject* DGPRCT1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<DGPRCT1, DGProtectionRelay>(this, obj);
    if (out != nullptr) {
        copyParametersTo(out);
    }
    return out == nullptr ? obj : out;
}
const ModelDescription& DGPRCTExt::getModelDescription() const
{
    static const auto description =
        ModelDescription::define<DGPRCTExt>("DGPRCTExt",
                                            &DGProtectionRelay::getModelDescription(),
                                            {});
    return description;
}

DGPRCTExt::DGPRCTExt(const std::string& objName): DGProtectionRelay(true, objName) {}
CoreObject* DGPRCTExt::clone(CoreObject* obj) const
{
    auto* out = cloneBase<DGPRCTExt, DGProtectionRelay>(this, obj);
    if (out != nullptr) {
        copyParametersTo(out);
    }
    return out == nullptr ? obj : out;
}
}  // namespace griddyn
