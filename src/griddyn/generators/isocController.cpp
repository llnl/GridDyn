/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "isocController.h"

#include "../Generator.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>
#include <string>

namespace griddyn {
isocController::isocController(const std::string& objName): gridSubModel(objName) {}
CoreObject* isocController::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<isocController, gridSubModel>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->db = db;
    nobj->upStep = upStep;
    nobj->downStep = downStep;
    nobj->upPeriod = upPeriod;
    nobj->downPeriod = downPeriod;
    nobj->maxLevel = maxLevel;
    nobj->minLevel = minLevel;
    nobj->integralTrigger = integralTrigger;
    return nobj;
}

void isocController::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    gen = dynamic_cast<Generator*>(getParent());
    updatePeriod = upPeriod;
    integratorLevel = 0;
}

void isocController::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (!inputs.empty()) {
        lastFreq = inputs[0];
        if (lastFreq < -db) {
            updatePeriod = downPeriod;
        }
    }
    if (!desiredOutput.empty()) {
        m_output = desiredOutput[0];
        fieldSet[0] = 0;
    } else {
        fieldSet[0] = m_output;
    }
}

void isocController::setLimits(double minV, double maxV)
{
    minLevel = std::min(maxV, minV);
    maxLevel = std::max(maxV, minV);
    m_output = gmlc::utilities::valLimit(m_output, minLevel, maxLevel);
}

void isocController::updateA(coreTime time)
{
    if (time < nextUpdateTime) {
        assert(false);
        return;
    }
    integratorLevel += lastFreq * updatePeriod;
    if (lastFreq > db) {
        m_output += upStep;
        updatePeriod = upPeriod;
    } else if (lastFreq < -db) {
        m_output += downStep;
        updatePeriod = downPeriod;
    } else {
        updatePeriod = upPeriod;
        if (integratorLevel > integralTrigger) {
            m_output += upStep;
        } else if (integratorLevel < -integralTrigger) {
            m_output += downStep;
        }
    }
    m_output = gmlc::utilities::valLimit(m_output, minLevel, maxLevel);
    lastUpdateTime = time;
    // printf("t=%f,output=%f\n", time, m_output);
}

void isocController::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    prevTime = time;
    lastFreq = inputs[0];
    while (nextUpdateTime <= time) {
        updateA(time);
        updateB();
    }
}

void isocController::set(std::string_view param, std::string_view val)
{
    gridSubModel::set(param, val);
}
void isocController::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "deadband") || (param == "db")) {
        db = val;
    } else if (param == "upstep") {
        upStep = val;
    } else if (param == "downstep") {
        downStep = val;
    } else if (param == "upperiod") {
        upPeriod = val;
    } else if (param == "downperiod") {
        downPeriod = val;
    } else if ((param == "max") || (param == "maxlevel")) {
        maxLevel = val;
    } else if ((param == "min") || (param == "minLevel")) {
        minLevel = val;
    } else if (param == "m_output") {
        m_output = val;
    } else {
        gridSubModel::set(param, val, unitType);
    }
}

void isocController::setLevel(double newLevel)
{
    m_output = gmlc::utilities::valLimit(newLevel, minLevel, maxLevel);
}
void isocController::setFreq(double freq)
{
    lastFreq = freq;
}
void isocController::deactivate()
{
    m_output = 0;
    nextUpdateTime = maxTime;
}

void isocController::activate(coreTime time)
{
    nextUpdateTime = time + upPeriod;
}
}  // namespace griddyn

