/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../comms/SchedulerMessage.h"
#include "AGControl.h"
#include "Scheduler.h"
#include "core/CoreObjectTemplates.hpp"
#include <cstdio>
#include <memory>
#include <string>

namespace griddyn {
schedulerReg::schedulerReg(const std::string& objName):
    schedulerRamp(objName), regMax(pMax), regMin(pMin), regRampUp(rampUp), regRampDown(rampDown)
{
    rampTime = 600;
}

schedulerReg::schedulerReg(double initialValue, const std::string& objName):
    schedulerRamp(initialValue, objName), regMax(pMax), regMin(pMin), regRampUp(rampUp),
    regRampDown(rampDown)
{
    rampTime = 600;
}

schedulerReg::schedulerReg(double initialValue, double initialReg, const std::string& objName):
    schedulerRamp(initialValue, objName), regMax(pMax), regMin(pMin), regRampUp(rampUp),
    regRampDown(rampDown), regCurrent(initialReg), regTarget(initialReg)
{
    rampTime = 600;
}

CoreObject* schedulerReg::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<schedulerReg, schedulerRamp>(this, obj);
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
    schedulerRamp::clone(nobj);

    return nobj;
}

schedulerReg::~schedulerReg()
{
    clearSchedule();
    if (agcController != nullptr) {
        agcController->remove(this);
    }
}

void schedulerReg::setReg(double regLevel)
{
    participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base;

    if (regLevel > regUpFrac * participationRating) {
        regTarget = regUpFrac * participationRating;
    } else if (regLevel < -regDownFrac * participationRating) {
        regTarget = -regDownFrac * participationRating;
    } else {
        regTarget = regLevel;
    }
}

void schedulerReg::updateA(coreTime time)
{
    const double deltaTime = (time - prevTime);

    if (deltaTime == 0) {
        return;
    }
    const double prevOutput = m_output;
    schedulerRamp::updateA(time);

    double ramp = ((regTarget - regCurrent) / deltaTime) + dpdt;
    if (ramp > regRampUp) {
        ramp = regRampUp;
    } else if (ramp < -regRampDown) {
        ramp = -regRampDown;
    }

    m_output = prevOutput + (ramp * deltaTime);

    dpdt = ramp;
    regCurrent = m_output - pCurr - reserveAct;
}

double schedulerReg::predict(coreTime time)
{
    const double deltaTime = (time - prevTime);
    if (deltaTime == 0) {
        return m_output;
    }
    const double predictedOutput = schedulerRamp::predict(time);

    double ramp =
        ((regTarget - regCurrent) / deltaTime) + ((predictedOutput - m_output) / deltaTime);
    if (ramp > regRampUp) {
        ramp = regRampUp;
    } else if (ramp < -regRampDown) {
        ramp = -regRampDown;
    }

    const double predictedRampOutput = m_output + (ramp * deltaTime);
    return predictedRampOutput;
}

void schedulerReg::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    schedulerRamp::dynObjectInitializeA(time0, flags);
    participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base;

    if ((regUpFrac > 0) || (regDownFrac > 0)) {
        if (agcController == nullptr) {
            dispatcherLink();
        }
    }
}

void schedulerReg::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    schedulerRamp::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
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

double schedulerReg::getRamp() const
{
    double ramp = 0;
    const double diff = regTarget - regCurrent;
    if (diff > 0.001) {
        ramp = regRampUp;
    } else if (diff < 0.001) {
        ramp = regRampDown;
    } else {
        ramp = schedulerRamp::getRamp();
    }
    return ramp;
}

double schedulerReg::getRampTime() const
{
    const double diff = regTarget - regCurrent;
    if (diff > 0.001) {
        return (regTarget - regCurrent) / (regRampUp - pRampCurr);
    }
    if (diff < 0.001) {
        return (regTarget - regCurrent) / (regRampDown - pRampCurr);
    }

    return schedulerRamp::getRampTime();
}

double schedulerReg::getMax(const coreTime /*time*/) const
{
    return pMax;
}

double schedulerReg::getMin(coreTime /*time*/) const
{
    return pMin;
}

void schedulerReg::regSettings(bool active, double upFrac, double downFrac)
{
    if (upFrac < 0) {
        if (regEnabled) {
            if (!active) {
                if (agcController != nullptr) {
                    agcController->remove(this);
                }
                regEnabled = false;
            }
        } else {
            if (active) {
                regEnabled = true;
                if (agcController != nullptr) {
                    agcController->add(this);
                } else {
                    dispatcherLink();
                }
            }
        }
    } else {
        regEnabled = active;
        regUpFrac = upFrac;
        regDownFrac = downFrac;
    }
    if (regEnabled) {
        participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base;
        rampUp = regRampUp - ((regUpFrac * participationRating) / 600);
        rampDown = regRampDown - ((regDownFrac * participationRating) / 600);
        pMax = regMax - (regUpFrac * participationRating);
        pMin = regMin + (regDownFrac * participationRating);
    } else {
        rampUp = regRampUp;
        rampDown = regRampDown;
        pMax = regMax;
        pMin = regMin;
    }
    updatePTarget();
    if (agcController != nullptr) {
        agcController->regChange();
    }
}

void schedulerReg::set(std::string_view param, std::string_view val)
{
    schedulerRamp::set(param, val);
}

void schedulerReg::set(std::string_view param, double val, units::unit unitType)
{
    double temp;
    if (param == "max") {
        regMax = val;
    } else if (param == "min") {
        regMin = val;
    } else if (param == "rampup") {
        regRampUp = val;
    } else if (param == "rampdown") {
        regRampDown = val;
        if (regRampDown < 0) {
            regRampDown = -regRampDown;
        }
    } else if (param == "ramp") {
        regRampUp = val;
        regRampDown = val;
    } else if ((param == "rating") || (param == "base")) {
        m_Base = val;
        if (agcController != nullptr) {
            agcController->regChange();
        }
    } else if (param == "regfrac") {
        temp = val;
        regUpFrac = temp;
        regDownFrac = temp;
        if (agcController != nullptr) {
            agcController->regChange();
        }
    } else if (param == "regupfrac") {
        regUpFrac = val;
        if (agcController != nullptr) {
            agcController->regChange();
        }
    } else if (param == "regdownfrac") {
        regDownFrac = val;

        if (agcController != nullptr) {
            agcController->regChange();
        }
    } else if (param == "regenabled") {
        const bool active = val > 0;
        if (regEnabled) {
            if (!active) {
                if (agcController != nullptr) {
                    agcController->remove(this);
                }
                regEnabled = false;
            }
        } else {
            if (active) {
                regEnabled = true;
                if (agcController != nullptr) {
                    agcController->add(this);
                }
            }
        }
    } else {
        schedulerRamp::set(param, val, unitType);
    }
    if (regEnabled) {
        participationRating = (m_Base >= kHalfBigNum) ? regMax : m_Base;
        rampUp = regRampUp - ((regUpFrac * participationRating) / 600);
        rampDown = regRampDown - ((regDownFrac * participationRating) / 600);
        pMax = regMax - (regUpFrac * participationRating);
        pMin = regMin + (regDownFrac * participationRating);
    } else {
        rampUp = regRampUp;
        rampDown = regRampDown;
        pMax = regMax;
        pMin = regMin;
    }
    updatePTarget();
}

void schedulerReg::dispatcherLink()
{
    agcController = static_cast<AGControl*>(find("agc"));
    if (agcController != nullptr) {
        agcController->add(this);
    }
    schedulerRamp::dispatcherLink();
}

double schedulerReg::get(std::string_view param, units::unit unitType) const
{
    double val;
    if (param == "min") {
        val = pMin;
    } else if (param == "max") {
        val = pMax;
    } else {
        return schedulerRamp::get(param, unitType);
    }
    return val;
}

void schedulerReg::receiveMessage(std::uint64_t sourceID,
                                  const std::shared_ptr<commMessage>& message)
{
    using comms::schedulerMessagePayload;
    // auto sm = std::dynamic_pointer_cast<schedulerMessage> (message);
    switch (message->getMessageType()) {
        case schedulerMessagePayload::CLEAR_TARGETS:
            clearSchedule();
            break;
        case schedulerMessagePayload::SHUTDOWN:
        case schedulerMessagePayload::STARTUP:
        case schedulerMessagePayload::UPDATE_TARGETS:
            break;
        default:
            schedulerRamp::receiveMessage(sourceID, message);
            break;
    }
}

}  // namespace griddyn
