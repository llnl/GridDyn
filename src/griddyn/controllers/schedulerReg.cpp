/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../comms/schedulerMessage.h"
#include "AGControl.h"
#include "core/coreObjectTemplates.hpp"
#include "scheduler.h"
#include <cstdio>
#include <memory>
#include <string>

namespace griddyn {
schedulerReg::schedulerReg(const std::string& objName):
    schedulerRamp(objName), regMax(Pmax), regMin(Pmin), regRampUp(rampUp), regRampDown(rampDown)
{
    rampTime = 600;
}

schedulerReg::schedulerReg(double initialValue, const std::string& objName):
    schedulerRamp(initialValue, objName), regMax(Pmax), regMin(Pmin), regRampUp(rampUp),
    regRampDown(rampDown)
{
    rampTime = 600;
}

schedulerReg::schedulerReg(double initialValue, double initialReg, const std::string& objName):
    schedulerRamp(initialValue, objName), regMax(Pmax), regMin(Pmin), regRampUp(rampUp),
    regRampDown(rampDown), regCurr(initialReg), regTarget(initialReg)
{
    rampTime = 600;
}

CoreObject* schedulerReg::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<schedulerReg, schedulerRamp>(this, obj);
    if (nobj == nullptr) {
        return nobj;
    }

    nobj->regCurr = regCurr;
    nobj->regTarget = regTarget;
    nobj->regUpFrac = regUpFrac;
    nobj->regDownFrac = regDownFrac;
    nobj->regMax = regMax;
    nobj->regMin = regMin;
    nobj->regRampUp = regRampUp;
    nobj->regRampDown = regRampDown;
    nobj->regEnabled = regEnabled;
    nobj->rampTime = rampTime;

    nobj->pr = pr;
    // copy the scheduler object last as it runs an initialize routine
    schedulerRamp::clone(nobj);

    return nobj;
}

schedulerReg::~schedulerReg()
{
    clearSchedule();
    if (agc != nullptr) {
        agc->remove(this);
    }
}

void schedulerReg::setReg(double regLevel)
{
    pr = (m_Base >= kHalfBigNum) ? regMax : m_Base;

    if (regLevel > regUpFrac * pr) {
        regTarget = regUpFrac * pr;
    } else if (regLevel < -regDownFrac * pr) {
        regTarget = -regDownFrac * pr;
    } else {
        regTarget = regLevel;
    }
}

void schedulerReg::updateA(coreTime time)
{
    double dt = (time - prevTime);

    if (dt == 0) {
        return;
    }
    double prevOutput = m_output;
    schedulerRamp::updateA(time);

    double ramp = (regTarget - regCurr) / dt + dPdt;
    if (ramp > regRampUp) {
        ramp = regRampUp;
    } else if (ramp < -regRampDown) {
        ramp = -regRampDown;
    }

    m_output = prevOutput + ramp * dt;

    dPdt = ramp;
    regCurr = m_output - PCurr - reserveAct;
}

double schedulerReg::predict(coreTime time)
{
    double dt = (time - prevTime);
    if (dt == 0) {
        return m_output;
    }
    double toutput = schedulerRamp::predict(time);

    double ramp = (regTarget - regCurr) / dt + (toutput - m_output) / dt;
    if (ramp > regRampUp) {
        ramp = regRampUp;
    } else if (ramp < -regRampDown) {
        ramp = -regRampDown;
    }

    double retout = m_output + ramp * dt;
    return retout;
}

void schedulerReg::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    schedulerRamp::dynObjectInitializeA(time0, flags);
    pr = (m_Base >= kHalfBigNum) ? regMax : m_Base;

    if ((regUpFrac > 0) || (regDownFrac > 0)) {
        if (agc == nullptr) {
            dispatcherLink();
        }
    }
}

void schedulerReg::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    schedulerRamp::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    double AGClevel = (desiredOutput.size() > 2) ? desiredOutput[2] : 0;
    if (AGClevel > regUpFrac * pr) {
        regCurr = regUpFrac * pr;
    } else if (AGClevel < -regDownFrac * pr) {
        regCurr = -regDownFrac * pr;
    } else {
        regCurr = AGClevel;
    }

    m_output = regCurr + PCurr + reserveAct;
}

double schedulerReg::getRamp() const
{
    double ramp = 0;
    double diff = regTarget - regCurr;
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
    double diff = regTarget - regCurr;
    if (diff > 0.001) {
        return (regTarget - regCurr) / (regRampUp - PRampCurr);
    }
    if (diff < 0.001) {
        return (regTarget - regCurr) / (regRampDown - PRampCurr);
    }

    return schedulerRamp::getRampTime();
}

double schedulerReg::getMax(const coreTime /*time*/) const
{
    return Pmax;
}

double schedulerReg::getMin(coreTime /*time*/) const
{
    return Pmin;
}

void schedulerReg::regSettings(bool active, double upFrac, double downFrac)
{
    if (upFrac < 0) {
        if (regEnabled) {
            if (!active) {
                if (agc != nullptr) {
                    agc->remove(this);
                }
                regEnabled = false;
            }
        } else {
            if (active) {
                regEnabled = true;
                if (agc != nullptr) {
                    agc->add(this);
                } else {
                    dispatcherLink();
                }
            }
        }
    } else {
        regEnabled = active;
        if (downFrac < 0) {
            regUpFrac = upFrac;
            regDownFrac = downFrac;
        } else {
            regUpFrac = upFrac;
            regDownFrac = downFrac;
        }
    }
    if (regEnabled) {
        pr = (m_Base >= kHalfBigNum) ? regMax : m_Base;
        rampUp = regRampUp - regUpFrac * pr / 600;
        rampDown = regRampDown - regDownFrac * pr / 600;
        Pmax = regMax - regUpFrac * pr;
        Pmin = regMin + regDownFrac * pr;
    } else {
        rampUp = regRampUp;
        rampDown = regRampDown;
        Pmax = regMax;
        Pmin = regMin;
    }
    updatePTarget();
    if (agc != nullptr) {
        agc->regChange();
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
        if (agc != nullptr) {
            agc->regChange();
        }
    } else if (param == "regfrac") {
        temp = val;
        regUpFrac = temp;
        regDownFrac = temp;
        if (agc != nullptr) {
            agc->regChange();
        }
    } else if (param == "regupfrac") {
        regUpFrac = val;
        if (agc != nullptr) {
            agc->regChange();
        }
    } else if (param == "regdownfrac") {
        regDownFrac = val;

        if (agc != nullptr) {
            agc->regChange();
        }
    } else if (param == "regenabled") {
        bool active = val > 0;
        if (regEnabled) {
            if (!active) {
                if (agc != nullptr) {
                    agc->remove(this);
                }
                regEnabled = false;
            }
        } else {
            if (active) {
                regEnabled = true;
                if (agc != nullptr) {
                    agc->add(this);
                }
            }
        }
    } else {
        schedulerRamp::set(param, val, unitType);
    }
    if (regEnabled) {
        pr = (m_Base >= kHalfBigNum) ? regMax : m_Base;
        rampUp = regRampUp - regUpFrac * pr / 600;
        rampDown = regRampDown - regDownFrac * pr / 600;
        Pmax = regMax - regUpFrac * pr;
        Pmin = regMin + regDownFrac * pr;
    } else {
        rampUp = regRampUp;
        rampDown = regRampDown;
        Pmax = regMax;
        Pmin = regMin;
    }
    updatePTarget();
}

void schedulerReg::dispatcherLink()
{
    agc = static_cast<AGControl*>(find("agc"));
    if (agc != nullptr) {
        agc->add(this);
    }
    schedulerRamp::dispatcherLink();
}

double schedulerReg::get(std::string_view param, units::unit unitType) const
{
    double val;
    if (param == "min") {
        val = Pmin;
    } else if (param == "max") {
        val = Pmax;
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
            break;
        case schedulerMessagePayload::STARTUP:
            break;
        case schedulerMessagePayload::UPDATE_TARGETS:
            break;
        default:
            schedulerRamp::receiveMessage(sourceID, message);
            break;
    }
}

}  // namespace griddyn

