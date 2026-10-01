/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../comms/SchedulerMessage.h"
#include "AGControl.h"
#include "Scheduler.h"
#include "../GridArea.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace griddyn {
SchedulerReg::SchedulerReg(const std::string& objName):
    SchedulerRamp(objName), regMax(pMax), regMin(pMin), regRampUp(rampUp), regRampDown(rampDown)
{
    rampTime = 600;
}

SchedulerReg::SchedulerReg(double initialValue, const std::string& objName):
    SchedulerRamp(initialValue, objName), regMax(pMax), regMin(pMin), regRampUp(rampUp),
    regRampDown(rampDown)
{
    rampTime = 600;
}

SchedulerReg::SchedulerReg(double initialValue, double initialReg, const std::string& objName):
    SchedulerRamp(initialValue, objName), regMax(pMax), regMin(pMin), regRampUp(rampUp),
    regRampDown(rampDown), regCurrent(initialReg), regTarget(initialReg)
{
    rampTime = 600;
}

CoreObject* SchedulerReg::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<SchedulerReg, SchedulerRamp>(this, obj);
    if (nobj == nullptr) {
        return nobj;
    }

    nobj->regCurrent = regCurrent;
    nobj->regTarget = regTarget;
    nobj->regUpFrac = regUpFrac;
    nobj->regDownFrac = regDownFrac;
    nobj->regMax = regMax;
    nobj->regMin = regMin;
    nobj->regRampUp = regRampUp;
    nobj->regRampDown = regRampDown;
    nobj->regEnabled = regEnabled;
    nobj->rampTime = rampTime;

    nobj->participationRating = participationRating;
    // copy the scheduler object last as it runs an initialize routine
    SchedulerRamp::clone(nobj);

    return nobj;
}

SchedulerReg::~SchedulerReg()
{
    clearSchedule();
    if (agcController != nullptr) {
        agcController->remove(this);
    }
}

void SchedulerReg::setReg(double regLevel)
{
    if (!std::isfinite(regLevel)) {
        throw InvalidParameterValue("scheduler regulation target must be finite");
    }
    participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base / systemBasePower;

    if (regLevel > regUpFrac * participationRating) {
        regTarget = regUpFrac * participationRating;
    } else if (regLevel < -regDownFrac * participationRating) {
        regTarget = -regDownFrac * participationRating;
    } else {
        regTarget = regLevel;
    }
}

void SchedulerReg::validateRegulationBounds(double baseMW,
                                            double minValue,
                                            double maxValue,
                                            double upFraction,
                                            double downFraction,
                                            bool enabled) const
{
    if (!std::isfinite(baseMW) || baseMW <= 0.0 || !std::isfinite(minValue) ||
        !std::isfinite(maxValue) || minValue > maxValue || !std::isfinite(upFraction) ||
        !std::isfinite(downFraction) || upFraction < 0.0 || upFraction > 1.0 ||
        downFraction < 0.0 || downFraction > 1.0) {
        throw InvalidParameterValue("scheduler regulation settings are outside valid ranges");
    }
    const double rating = (baseMW >= kHalfBigNum) ? maxValue : baseMW / systemBasePower;
    const double effectiveMin = minValue + (enabled ? downFraction * rating : 0.0);
    const double effectiveMax = maxValue - (enabled ? upFraction * rating : 0.0);
    if (!std::isfinite(rating) || effectiveMin > effectiveMax - reserveAvail) {
        throw InvalidParameterValue(
            "scheduler regulation limits and reserve exceed the available power range");
    }
}

void SchedulerReg::updateRegulationLimits()
{
    validateRegulationBounds(m_Base, regMin, regMax, regUpFrac, regDownFrac, regEnabled);
    participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base / systemBasePower;
    if (regEnabled) {
        const double upReservation = regUpFrac * participationRating;
        const double downReservation = regDownFrac * participationRating;
        rampUp = std::max(0.0, regRampUp - upReservation / 600.0);
        rampDown = std::max(0.0, regRampDown - downReservation / 600.0);
        pMax = regMax - upReservation;
        pMin = regMin + downReservation;
    } else {
        rampUp = regRampUp;
        rampDown = regRampDown;
        pMax = regMax;
        pMin = regMin;
    }
    pCurr = std::clamp(pCurr, pMin, pMax - reserveAvail);
    updatePTarget();
    if (agcController != nullptr) {
        agcController->regChange();
    }
}

void SchedulerReg::updateA(CoreTime time)
{
    const double deltaTime = (time - prevTime);
    if (deltaTime <= 0.0) {
        return;
    }
    m_output -= regCurrent;
    baseUpdateInProgress = true;
    SchedulerRamp::updateA(time);
    baseUpdateInProgress = false;
    const double baseRamp = dpdt;
    const double regulationRamp =
        std::clamp((regTarget - regCurrent) / deltaTime, -regRampDown, regRampUp);
    regCurrent += regulationRamp * deltaTime;
    m_output += regCurrent;
    dpdt = baseRamp + regulationRamp;
}

double SchedulerReg::predict(CoreTime time)
{
    const double deltaTime = (time - prevTime);
    if (deltaTime <= 0.0) {
        return m_output;
    }
    const double baseOutput = m_output - regCurrent + SchedulerRamp::getRamp() * deltaTime;
    const double regulation = std::clamp(regTarget,
                                          regCurrent - regRampDown * deltaTime,
                                          regCurrent + regRampUp * deltaTime);
    return baseOutput + regulation;
}

void SchedulerReg::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    SchedulerRamp::dynObjectInitializeA(time0, flags);
    participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base / systemBasePower;

    if ((regUpFrac > 0) || (regDownFrac > 0)) {
        if (agcController == nullptr) {
            dispatcherLink();
        }
    }
}

void SchedulerReg::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    SchedulerRamp::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    const double agcLevel = (desiredOutput.size() > 2) ? desiredOutput[2] : 0;
    if (agcLevel > regUpFrac * participationRating) {
        regCurrent = regUpFrac * participationRating;
    } else if (agcLevel < -regDownFrac * participationRating) {
        regCurrent = -regDownFrac * participationRating;
    } else {
        regCurrent = agcLevel;
    }

    m_output = regCurrent + pCurr + reserveAct;
}

double SchedulerReg::getRamp() const
{
    const double baseRamp = SchedulerRamp::getRamp();
    if (baseUpdateInProgress) {
        return baseRamp;
    }
    const double diff = regTarget - regCurrent;
    if (diff > 0.001) {
        return baseRamp + regRampUp;
    } else if (diff < -0.001) {
        return baseRamp - regRampDown;
    }
    return baseRamp;
}

double SchedulerReg::getRampTime() const
{
    const double baseTime = SchedulerRamp::getRampTime();
    const double diff = regTarget - regCurrent;
    if (diff > 0.001 && regRampUp > 0.0) {
        return std::min(baseTime, diff / regRampUp);
    }
    if (diff < -0.001 && regRampDown > 0.0) {
        return std::min(baseTime, -diff / regRampDown);
    }
    return baseTime;
}

double SchedulerReg::getMax(const CoreTime /*time*/) const
{
    return pMax - reserveAvail;
}

double SchedulerReg::getMin(CoreTime /*time*/) const
{
    return pMin;
}

void SchedulerReg::regSettings(bool active, double upFrac, double downFrac)
{
    if (!std::isfinite(upFrac) || !std::isfinite(downFrac)) {
        throw InvalidParameterValue("scheduler regulation fractions must be finite");
    }
    double newUpFraction = regUpFrac;
    double newDownFraction = regDownFrac;
    if (upFrac >= 0.0) {
        newUpFraction = upFrac;
        newDownFraction = downFrac;
    }
    validateRegulationBounds(m_Base, regMin, regMax, newUpFraction, newDownFraction, active);

    if (upFrac < 0) {
        regEnabled = active;
    } else {
        regEnabled = active;
        regUpFrac = newUpFraction;
        regDownFrac = newDownFraction;
    }
    updateRegulationLimits();
    if (!regEnabled && agcController != nullptr) {
        agcController->remove(this);
    } else if (regEnabled && agcController == nullptr) {
        dispatcherLink();
    }
}

void SchedulerReg::set(std::string_view param, std::string_view val)
{
    SchedulerRamp::set(param, val);
}

void SchedulerReg::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("scheduler regulation parameters must be finite");
    }

    double newBaseMW = m_Base;
    double newMin = regMin;
    double newMax = regMax;
    double newUpFraction = regUpFrac;
    double newDownFraction = regDownFrac;
    double newRegRampUp = regRampUp;
    double newRegRampDown = regRampDown;
    bool newRegEnabled = regEnabled;
    bool regulationConfigChanged = false;

    if (param == "max") {
        newMax = units::convert(val, unitType, units::puMW, m_Base);
        regulationConfigChanged = true;
    } else if (param == "min") {
        newMin = units::convert(val, unitType, units::puMW, m_Base);
        regulationConfigChanged = true;
    } else if (param == "rampup") {
        newRegRampUp = units::convert(val, unitType, units::puMW / units::s, m_Base);
        regulationConfigChanged = true;
    } else if (param == "rampdown") {
        newRegRampDown = units::convert(val, unitType, units::puMW / units::s, m_Base);
        regulationConfigChanged = true;
    } else if (param == "ramp") {
        newRegRampUp = units::convert(val, unitType, units::puMW / units::s, m_Base);
        newRegRampDown = newRegRampUp;
        regulationConfigChanged = true;
    } else if ((param == "rating") || (param == "base")) {
        newBaseMW = units::convert(val, unitType, units::MW, systemBasePower);
        regulationConfigChanged = true;
    } else if (param == "regfrac") {
        newUpFraction = val;
        newDownFraction = val;
        regulationConfigChanged = true;
    } else if (param == "regupfrac") {
        newUpFraction = val;
        regulationConfigChanged = true;
    } else if (param == "regdownfrac") {
        newDownFraction = val;
        regulationConfigChanged = true;
    } else if (param == "regenabled") {
        newRegEnabled = val > 0.0;
        regulationConfigChanged = true;
    } else if (param == "reserve") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        const double rating = (m_Base >= kHalfBigNum) ? regMax : m_Base / systemBasePower;
        const double effectiveMin = regMin + (regEnabled ? regDownFrac * rating : 0.0);
        const double effectiveMax = regMax - (regEnabled ? regUpFrac * rating : 0.0);
        if (!std::isfinite(amount) || amount < 0.0 || amount > effectiveMax - effectiveMin) {
            throw InvalidParameterValue("scheduler reserve exceeds the available power range");
        }
        SchedulerRamp::set(param, val, unitType);
        if (regEnabled) {
            updateRegulationLimits();
        }
        return;
    } else {
        SchedulerRamp::set(param, val, unitType);
        if (regEnabled) {
            updateRegulationLimits();
        }
        return;
    }

    if (!std::isfinite(newMin) || !std::isfinite(newMax) ||
        !std::isfinite(newRegRampUp) || !std::isfinite(newRegRampDown) ||
        newRegRampUp < 0.0 || newRegRampDown < 0.0) {
        throw InvalidParameterValue(
            "scheduler regulation limits and ramp rates must be nonnegative and finite");
    }
    validateRegulationBounds(newBaseMW, newMin, newMax, newUpFraction, newDownFraction,
                             newRegEnabled);
    if (regulationConfigChanged) {
        const bool wasEnabled = regEnabled;
        m_Base = newBaseMW;
        regMin = newMin;
        regMax = newMax;
        regUpFrac = newUpFraction;
        regDownFrac = newDownFraction;
        regRampUp = newRegRampUp;
        regRampDown = newRegRampDown;
        regEnabled = newRegEnabled;
        updateRegulationLimits();
        if (!regEnabled && wasEnabled && agcController != nullptr) {
            agcController->remove(this);
        } else if (regEnabled && agcController == nullptr) {
            dispatcherLink();
        }
    } else if (regEnabled && agcController == nullptr) {
        dispatcherLink();
    }
}

void SchedulerReg::dispatcherLink()
{
    auto* object = getParent();
    AGControl* controller = nullptr;
    while (object != nullptr) {
        auto* area = dynamic_cast<GridArea*>(object);
        if (area != nullptr && area->getAGControl() != nullptr) {
            controller = area->getAGControl();
            break;
        }
        auto* parentObject = object->getParent();
        if (parentObject == object) {
            break;
        }
        object = parentObject;
    }
    if (agcController != nullptr && agcController != controller) {
        agcController->remove(this);
    }
    if (controller != nullptr) {
        controller->add(this);
    }
    SchedulerRamp::dispatcherLink();
}

double SchedulerReg::get(std::string_view param, units::unit unitType) const
{
    return SchedulerRamp::get(param, unitType);
}

void SchedulerReg::receiveMessage(std::uint64_t sourceID,
                                  const std::shared_ptr<CommMessage>& message)
{
    using comms::SchedulerMessagePayload;
    // auto sm = std::dynamic_pointer_cast<schedulerMessage> (message);
    switch (message->getMessageType()) {
        case SchedulerMessagePayload::CLEAR_TARGETS:
            clearSchedule();
            break;
        case SchedulerMessagePayload::SHUTDOWN:
        case SchedulerMessagePayload::STARTUP:
        case SchedulerMessagePayload::UPDATE_TARGETS:
            break;
        default:
            SchedulerRamp::receiveMessage(sourceID, message);
            break;
    }
}

}  // namespace griddyn
