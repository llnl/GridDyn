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
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace griddyn {
DGProtectionRelay::DGProtectionRelay(bool useExternalVoltage, const std::string& name):
    Relay(name), externalVoltage(useExternalVoltage)
{
    opFlags.set(CONTINUOUS_FLAG);
    opFlags.set(RESETTABLE_FLAG);
}

void DGProtectionRelay::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("DG protection parameter must be finite");
    }
    if (key == "fen") {
        frequencyEnabled = val != 0.0;
        return;
    }
    if (key == "ven") {
        voltageEnabled = val != 0.0;
        return;
    }
    if (key == "fn") {
        fn = val;
        return;
    }
    if (key == "tres") {
        resetTime = val;
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
    constexpr std::array<std::string_view, 6> fNames{{"fl3", "fl2", "fl1", "fu1", "fu2", "fu3"}};
    constexpr std::array<std::string_view, 7> vNames{
        {"vl4", "vl3", "vl2", "vl1", "vu1", "vu2", "vu3"}};
    constexpr std::array<std::string_view, 4> ftNames{{"tfl1", "tfl2", "tfu1", "tfu2"}};
    constexpr std::array<std::string_view, 5> vtNames{{"tvl1", "tvl2", "tvl3", "tvu1", "tvu2"}};
    for (std::size_t i = 0; i < fNames.size(); ++i) {
        if (key == fNames[i]) {
            frequency[i] = val;
            return;
        }
    }
    for (std::size_t i = 0; i < vNames.size(); ++i) {
        if (key == vNames[i]) {
            voltage[i] = val;
            return;
        }
    }
    for (std::size_t i = 0; i < ftNames.size(); ++i) {
        if (key == ftNames[i]) {
            frequencyTime[i] = val;
            return;
        }
    }
    for (std::size_t i = 0; i < vtNames.size(); ++i) {
        if (key == vtNames[i]) {
            voltageTime[i] = val;
            return;
        }
    }
    Relay::set(param, val, unitType);
}

double DGProtectionRelay::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "external_voltage" || key == "v") {
        return externalV;
    }
    if (key == "lock") {
        return lock ? 1.0 : 0.0;
    }
    if (key == "fen") {
        return frequencyEnabled ? 1.0 : 0.0;
    }
    if (key == "ven") {
        return voltageEnabled ? 1.0 : 0.0;
    }
    if (key == "fn") {
        return fn;
    }
    if (key == "tres") {
        return resetTime;
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
    for (index_t i = 0; i < configuredConditions; ++i) {
        if (checkCondition(i)) {
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
    for (index_t i = 0; i < configuredConditions; ++i) {
        if (checkCondition(i)) {
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

DGPRCT1::DGPRCT1(const std::string& name): DGProtectionRelay(false, name) {}
CoreObject* DGPRCT1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<DGPRCT1, DGProtectionRelay>(this, obj);
    if (out != nullptr) {
        copyParametersTo(out);
    }
    return out == nullptr ? obj : out;
}
DGPRCTExt::DGPRCTExt(const std::string& name): DGProtectionRelay(true, name) {}
CoreObject* DGPRCTExt::clone(CoreObject* obj) const
{
    auto* out = cloneBase<DGPRCTExt, DGProtectionRelay>(this, obj);
    if (out != nullptr) {
        copyParametersTo(out);
    }
    return out == nullptr ? obj : out;
}
}  // namespace griddyn
