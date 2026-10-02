/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AGControl.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../generators/DynamicGenerator.h"
#include "../generators/RenewableGenerator.h"
#include "../generators/VariableGenerator.h"
#include "../relays/BusMeasurementSensor.h"
#include "../relays/Sensor.h"
#include "Scheduler.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace griddyn {
static const TypeFactory<AGControl>
    AGC_FACTORY("agc", std::to_array<std::string_view>({"basic", "agc"}), "basic");

AGControl::AGControl(const std::string& objName): GridSubModel(objName)
{
    updatePeriod = 4.0;
}

AGControl::~AGControl()
{
    for (auto* sched : schedList) {
        if (sched != nullptr && sched->agcController == this) {
            sched->agcController = nullptr;
        }
    }
    schedList.clear();
}

CoreObject* AGControl::clone(CoreObject* obj) const
{
    auto* result = cloneBase<AGControl, GridSubModel>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->ki = ki;
    result->kp = kp;
    result->bias = bias;
    result->deadband = deadband;
    result->tf = tf;
    result->tr = tr;
    result->fixedFrequency = fixedFrequency;
    result->targetFrequency = targetFrequency;
    result->frequencySensorName = frequencySensorName;
    result->frequencyOutput = frequencyOutput;
    result->frequencyIsDeviation = frequencyIsDeviation;
    result->frequencyOutputExplicit = frequencyOutputExplicit;
    result->frequencyDeviationExplicit = frequencyDeviationExplicit;
    result->frequencySensor = nullptr;
    result->schedList.clear();
    result->initialized = false;
    return result;
}

void AGControl::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    auto* area = dynamic_cast<GridArea*>(getParent());
    if (area == nullptr) {
        throw InvalidParameterValue("AGC requires an owning area");
    }
    if (!std::isfinite(updatePeriod) || updatePeriod <= 0.0) {
        throw InvalidParameterValue("AGC sample interval must be positive and finite");
    }
    frequencySensor = nullptr;
    if (!frequencySensorName.empty()) {
        frequencySensor = dynamic_cast<Sensor*>(area->find(frequencySensorName));
        if (frequencySensor == nullptr) {
            throw InvalidParameterValue("AGC frequency sensor was not found");
        }
        // The built-in PLL reports deviation in output 1; COI and FreqDiv report pu frequency.
        const bool pllMeasurement = dynamic_cast<PLLSensor*>(frequencySensor) != nullptr;
        if (!frequencyOutputExplicit) {
            frequencyOutput = pllMeasurement ? 1 : 0;
        }
        if (!frequencyDeviationExplicit) {
            frequencyIsDeviation = pllMeasurement;
        }
    }
    addInterchangeSlackParticipant();
    initialized = false;
}

void AGControl::addInterchangeSlackParticipant()
{
    auto* area = static_cast<GridArea*>(getParent());
    if (!schedList.empty()) {
        return;
    }
    // A configured scheduler may not have linked to this AGC yet. Generators
    // initialize after the AGC, so inspect their power sources first.
    for (index_t index = 0;; ++index) {
        auto* generator = area->getGen(index);
        if (generator == nullptr) {
            break;
        }
        if (!generator->isEnabled()) {
            continue;
        }
        const CoreObject* ancestor = generator->getParent();
        bool belongsToThisAGC = true;
        while (ancestor != nullptr && !isSameObject(ancestor, area)) {
            const auto* subarea = dynamic_cast<const GridArea*>(ancestor);
            if (subarea != nullptr && subarea->getAGControl() != nullptr) {
                belongsToThisAGC = false;
                break;
            }
            const auto* parent = ancestor->getParent();
            if (parent == ancestor) {
                break;
            }
            ancestor = parent;
        }
        if (!belongsToThisAGC) {
            continue;
        }
        auto* configured = dynamic_cast<SchedulerReg*>(generator->find("sched"));
        if (configured == nullptr) {
            configured = dynamic_cast<SchedulerReg*>(generator->find("pset"));
        }
        if (configured != nullptr && configured->getRegEnabled()) {
            return;
        }
    }
    auto* slackBus = area->getInterchangeSlackBus();
    if (slackBus == nullptr) {
        return;
    }
    Generator* target = nullptr;
    for (index_t index = 0;; ++index) {
        auto* generator = slackBus->getGen(index);
        if (generator == nullptr) {
            break;
        }
        if (!generator->isEnabled()) {
            continue;
        }
        if (target != nullptr) {
            throw InvalidParameterValue(
                "AGC ISW bus has multiple enabled generators; specify participants explicitly");
        }
        target = generator;
    }
    if (target == nullptr) {
        throw InvalidParameterValue("AGC ISW bus has no enabled generator");
    }
    auto* dynamicGen = dynamic_cast<DynamicGenerator*>(target);
    auto* renewableGen = dynamic_cast<RenewableGenerator*>(target);
    if ((dynamicGen == nullptr &&
         (renewableGen == nullptr || !renewableGen->supportsActivePowerSchedule())) ||
        dynamic_cast<VariableGenerator*>(target) != nullptr) {
        throw InvalidParameterValue(
            "AGC ISW generator does not support a SchedulerReg power setpoint");
    }
    if (target->find("pset") != nullptr) {
        throw InvalidParameterValue("AGC ISW generator already has a power setpoint source");
    }
    const double baseMW = area->get("basepower", units::MW);
    const double ratingMW = target->get("mbase", units::MW);
    const double pset = target->get("pset", units::puMW);
    if (!std::isfinite(baseMW) || baseMW <= 0.0 || !std::isfinite(ratingMW) || ratingMW <= 0.0 ||
        !std::isfinite(pset) || pset <= -kHalfBigNum) {
        throw InvalidParameterValue(
            "AGC ISW generator requires a finite setpoint and positive rating");
    }
    const double rating = ratingMW / baseMW;
    const double pmax = target->get("pmax", units::puMW);
    const double pmin = target->get("pmin", units::puMW);
    const double upFraction = std::clamp((pmax - pset) / rating, 0.0, 1.0);
    const double downFraction = std::clamp((pset - pmin) / rating, 0.0, 1.0);
    auto scheduler = std::make_unique<SchedulerReg>(pset);
    scheduler->set("purpose", "pset");
    scheduler->set("rating", ratingMW);
    scheduler->set("max", pmax);
    scheduler->set("min", pmin);
    const double rampMWPerMinute = target->get("rampagc");
    if (std::isfinite(rampMWPerMinute) && rampMWPerMinute > 0.0) {
        scheduler->set("ramp", rampMWPerMinute / (60.0 * baseMW));
    }
    target->add(scheduler.get());
    auto* attached = dynamic_cast<SchedulerReg*>(target->find("pset"));
    if (scheduler.release() != attached) {
        throw InvalidParameterValue("AGC could not attach its ISW scheduler");
    }
    attached->regSettings(true, upFraction, downFraction);
}

void AGControl::dynObjectInitializeB(const IOdata& /*inputs*/,
                                     const IOdata& /*desiredOutput*/,
                                     IOdata& fieldSet)
{
    if (frequencySensor != nullptr && frequencyOutput >= frequencySensor->numOutputs()) {
        throw InvalidParameterValue("AGC frequency sensor output is unavailable");
    }
    ace = measuredACE();
    filteredAce = ace;
    integralAce = 0.0;
    requestedReg = 0.0;
    reg = 0.0;
    initialized = true;
    fieldSet.resize(1);
    fieldSet[0] = reg;
}

double AGControl::measuredFrequency() const
{
    const double measurement =
        frequencySensor == nullptr ? fixedFrequency : frequencySensor->getOutput(frequencyOutput);
    const double frequency = measurement + (frequencyIsDeviation ? 1.0 : 0.0);
    if (!std::isfinite(frequency) || frequency <= 0.0) {
        throw InvalidParameterValue("AGC frequency measurement must be positive and finite");
    }
    return frequency;
}

double AGControl::measuredACE() const
{
    const auto* area = dynamic_cast<GridArea*>(getParent());
    if (area == nullptr) {
        throw InvalidParameterValue("AGC requires an owning area");
    }
    const double baseMW = area->get("basepower", units::MW);
    const double baseHz = area->get("basefrequency", units::Hz);
    if (!std::isfinite(baseMW) || baseMW <= 0.0 || !std::isfinite(baseHz) || baseHz <= 0.0) {
        throw InvalidParameterValue("AGC requires positive area power and frequency bases");
    }
    double scheduledMW = 0.0;
    if (const auto netSchedule = area->getScheduledNetInterchangeMW(); netSchedule) {
        scheduledMW = *netSchedule;
    } else {
        auto* root = const_cast<GridArea*>(area);
        while (auto* parentArea = dynamic_cast<GridArea*>(root->getParent())) {
            root = parentArea;
        }
        bool found = false;
        for (const auto& transfer : root->getInterAreaTransfers()) {
            if (transfer.fromAreaID == area->getUserID()) {
                scheduledMW += transfer.scheduledMW;
                found = true;
            } else if (transfer.toAreaID == area->getUserID()) {
                scheduledMW -= transfer.scheduledMW;
                found = true;
            }
        }
        if (!found) {
            throw InvalidParameterValue("AGC requires a configured area interchange schedule");
        }
    }
    const double actualMW = area->getBoundaryTieFlowReal() * baseMW;
    const double frequencyErrorHz = (measuredFrequency() - targetFrequency) * baseHz;
    return actualMW - scheduledMW - (10.0 * bias * frequencyErrorHz);
}

void AGControl::updateA(CoreTime time)
{
    if (time + 1e-9 < getNextUpdateTime()) {
        return;
    }
    if (!initialized || !isEnabled()) {
        CoreObject::updateA(time);
        return;
    }
    const double deltaTime = time - prevTime;
    if (deltaTime <= 0.0) {
        CoreObject::updateA(time);
        return;
    }
    for (auto* sched : schedList) {
        if (sched->isEnabled() && sched->getRegEnabled()) {
            sched->updateA(time);
        }
    }
    regChange();
    ace = measuredACE();
    filteredAce += (tf <= 0.0 ? 1.0 : deltaTime / (tf + deltaTime)) *
        (ace - filteredAce);
    const double controlError =
        std::copysign(std::max(0.0, std::abs(filteredAce) - deadband), filteredAce);
    const double candidateIntegral = integralAce + (controlError * deltaTime);
    const auto* area = static_cast<const GridArea*>(getParent());
    const double baseMW = area->get("basepower", units::MW);
    const double candidateReg = -((kp * controlError) + (ki * candidateIntegral)) / baseMW;
    if ((candidateReg <= regUpAvailable || controlError >= 0.0) &&
        (candidateReg >= -regDownAvailable || controlError <= 0.0)) {
        integralAce = candidateIntegral;
    }
    const double rawReg = -((kp * controlError) + (ki * integralAce)) / baseMW;
    requestedReg += (tr <= 0.0 ? 1.0 : deltaTime / (tr + deltaTime)) *
        (rawReg - requestedReg);
    reg = std::clamp(requestedReg, -regDownAvailable, regUpAvailable);
    dispatch();
    prevTime = time;
    CoreObject::updateA(time);
}

CoreTime AGControl::updateB()
{
    return CoreObject::updateB();
}

void AGControl::dispatch()
{
    for (auto* sched : schedList) {
        if (!sched->isEnabled() || !sched->getRegEnabled()) {
            continue;
        }
        const double available = reg >= 0.0 ? regUpAvailable : regDownAvailable;
        const double resource =
            reg >= 0.0 ? sched->getRegUpAvailable() : sched->getRegDownAvailable();
        sched->setReg(available > 0.0 ? reg * resource / available : 0.0);
    }
}

double AGControl::getOutput(const IOdata& /*inputs*/,
                            const StateData& /*sD*/,
                            const SolverMode& /*sMode*/,
                            index_t /*num*/) const
{
    return reg;
}

double AGControl::getOutput(index_t /*num*/) const
{
    return reg;
}

void AGControl::add(CoreObject* obj)
{
    auto* sched = dynamic_cast<SchedulerReg*>(obj);
    if (sched == nullptr) {
        throw UnrecognizedObjectException(this);
    }
    add(sched);
}

void AGControl::add(SchedulerReg* sched)
{
    if (sched == nullptr) {
        return;
    }
    const auto* owner = dynamic_cast<GridArea*>(getParent());
    if (owner != nullptr) {
        const CoreObject* object = sched;
        while (object != nullptr && !isSameObject(object, owner)) {
            const auto* ancestor = object->getParent();
            if (ancestor == object) {
                break;
            }
            object = ancestor;
        }
        if (!isSameObject(object, owner)) {
            throw InvalidParameterValue("AGC participant belongs to another area");
        }
    }
    if (sched->agcController != nullptr && sched->agcController != this) {
        sched->agcController->remove(sched);
    }
    if (std::find(schedList.begin(), schedList.end(), sched) == schedList.end()) {
        schedList.push_back(sched);
        regChange();
    }
    sched->agcController = this;
}

void AGControl::remove(CoreObject* obj)
{
    const auto schedulerPosition = std::find(schedList.begin(), schedList.end(), obj);
    if (schedulerPosition != schedList.end()) {
        if ((*schedulerPosition)->agcController == this) {
            (*schedulerPosition)->agcController = nullptr;
        }
        schedList.erase(schedulerPosition);
        regChange();
    } else if (auto* sched = dynamic_cast<SchedulerReg*>(obj);
               sched != nullptr && sched->agcController == this) {
        sched->agcController = nullptr;
    }
}

void AGControl::set(std::string_view param, std::string_view val)
{
    if (param == "frequencysensor") {
        frequencySensorName = val;
        frequencySensor = nullptr;
    } else {
        CoreObject::set(param, val);
    }
}

void AGControl::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("AGC parameters must be finite");
    }
    if (param == "deadband") {
        if (val < 0.0) {
            throw InvalidParameterValue("AGC deadband must be nonnegative");
        }
        deadband = units::convert(val,
                                  unitType == units::defunit ? units::MW : unitType,
                                  units::MW,
                                  systemBasePower);
    } else if (param == "bias") {
        if (val > 0.0) {
            throw InvalidParameterValue("AGC frequency bias must be nonpositive");
        }
        bias = val;
    } else if (param == "beta") {
        bias = -std::abs(val);
    } else if (param == "ki") {
        if (val < 0.0) {
            throw InvalidParameterValue("AGC integral gain must be nonnegative");
        }
        ki = val;
    } else if (param == "kp") {
        if (val < 0.0) {
            throw InvalidParameterValue("AGC proportional gain must be nonnegative");
        }
        kp = val;
    } else if (param == "tf") {
        if (val < 0.0) {
            throw InvalidParameterValue("AGC filter time must be nonnegative");
        }
        tf = units::convert(val, unitType == units::defunit ? units::s : unitType, units::s);
    } else if (param == "tr") {
        if (val < 0.0) {
            throw InvalidParameterValue("AGC response time must be nonnegative");
        }
        tr = units::convert(val, unitType == units::defunit ? units::s : unitType, units::s);
    } else if (param == "frequency") {
        if (val <= 0.0) {
            throw InvalidParameterValue("AGC frequency must be positive");
        }
        fixedFrequency = val;
    } else if (param == "targetfrequency") {
        if (val <= 0.0) {
            throw InvalidParameterValue("AGC target frequency must be positive");
        }
        targetFrequency = val;
    } else if (param == "frequencyoutput") {
        if (val < 0.0 || std::floor(val) != val ||
            val > static_cast<double>(std::numeric_limits<index_t>::max())) {
            throw InvalidParameterValue("AGC frequency output must be an index");
        }
        frequencyOutput = static_cast<index_t>(val);
        frequencyOutputExplicit = true;
    } else if (param == "frequencyisdeviation") {
        frequencyIsDeviation = val != 0.0;
        frequencyDeviationExplicit = true;
    } else if (param == "period" || param == "updateperiod" || param == "sampleinterval") {
        const double seconds =
            units::convert(val, unitType == units::defunit ? units::s : unitType, units::s);
        if (seconds <= 0.0) {
            throw InvalidParameterValue("AGC sample interval must be positive");
        }
        updatePeriod = seconds;
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

double AGControl::get(std::string_view param, units::unit unitType) const
{
    if (param == "ace") {
        return units::convert(ace,
                              units::MW,
                              unitType == units::defunit ? units::MW : unitType,
                              systemBasePower);
    }
    if (param == "filteredace") {
        return units::convert(filteredAce,
                              units::MW,
                              unitType == units::defunit ? units::MW : unitType,
                              systemBasePower);
    }
    if (param == "regulation") {
        return reg;
    }
    if (param == "bias" || param == "beta") {
        return param == "beta" ? -bias : bias;
    }
    if (param == "sampleinterval") {
        return updatePeriod;
    }
    return GridSubModel::get(param, unitType);
}

void AGControl::regChange()
{
    regUpAvailable = 0.0;
    regDownAvailable = 0.0;
    for (const auto* sched : schedList) {
        if (sched->isEnabled() && sched->getRegEnabled()) {
            regUpAvailable += std::max(0.0, sched->getRegUpAvailable());
            regDownAvailable += std::max(0.0, sched->getRegDownAvailable());
        }
    }
}
}  // namespace griddyn
