/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "IsocController.h"

#include "../Generator.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>
#include <string>

namespace griddyn {
IsocController::IsocController(const std::string& objName): GridSubModel(objName) {}
CoreObject* IsocController::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<IsocController, GridSubModel>(this, obj);
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

void IsocController::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    gen = dynamic_cast<Generator*>(getParent());
    updatePeriod = upPeriod;
    integratorLevel = 0;
}

void IsocController::dynObjectInitializeB(const IOdata& inputs,
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

void IsocController::setLimits(double minV, double maxV)
{
    minLevel = std::min(maxV, minV);
    maxLevel = std::max(maxV, minV);
    m_output = gmlc::utilities::valLimit(m_output, minLevel, maxLevel);
}

void IsocController::updateA(coreTime time)
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

void IsocController::timestep(coreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    prevTime = time;
    lastFreq = inputs[0];
    while (nextUpdateTime <= time) {
        updateA(time);
        updateB();
    }
}

void IsocController::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}
void IsocController::set(std::string_view param, double val, units::unit unitType)
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
        GridSubModel::set(param, val, unitType);
    }
}

void IsocController::setLevel(double newLevel)
{
    m_output = gmlc::utilities::valLimit(newLevel, minLevel, maxLevel);
}
void IsocController::setFreq(double freq)
{
    lastFreq = freq;
}
void IsocController::deactivate()
{
    m_output = 0;
    nextUpdateTime = maxTime;
}

void IsocController::activate(coreTime time)
{
    nextUpdateTime = time + upPeriod;
}
}  // namespace griddyn
