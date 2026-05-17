/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "busRelay.h"

#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../measurement/Condition.h"
#include "core/coreObjectTemplates.hpp"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
busRelay::busRelay(const std::string& objName): Relay(objName)
{
    opFlags.set(continuous_flag);
    opFlags.set(power_flow_checks_flag);  // enable power flow checks for busRelay
}

CoreObject* busRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<busRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mCutoutVoltage = mCutoutVoltage;
    nobj->mCutoutFrequency = mCutoutFrequency;
    nobj->mVoltageDelay = mVoltageDelay;
    nobj->mFrequencyDelay = mFrequencyDelay;
    return nobj;
}

void busRelay::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        Relay::setFlag(flag, val);
    }
}
/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void busRelay::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void busRelay::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "cutoutvoltage") || (param == "voltagelimit")) {
        mCutoutVoltage = units::convert(val, unitType, units::puV, systemBasePower, baseVoltage());
        if (opFlags[dyn_initialized]) {
            setConditionLevel(0, mCutoutVoltage);
        }
    } else if ((param == "cutoutfrequency") || (param == "freqlimit")) {
        mCutoutFrequency = units::convert(val, unitType, units::puHz, systemBaseFrequency);
        if (opFlags[dyn_initialized]) {
            setConditionLevel(1, mCutoutFrequency);
        }
    } else if (param == "delay") {
        mVoltageDelay = val;
        mFrequencyDelay = val;
        if (opFlags[dyn_initialized]) {
            setActionTrigger(0, 0, mVoltageDelay);
            setActionTrigger(0, 1, mFrequencyDelay);
        }
    } else if (param == "voltagedelay") {
        mVoltageDelay = val;
        if (opFlags[dyn_initialized]) {
            setActionTrigger(0, 0, mVoltageDelay);
        }
    } else if (param == "frequencydelay") {
        mFrequencyDelay = val;
        if (opFlags[dyn_initialized]) {
            setActionTrigger(0, 1, mFrequencyDelay);
        }
    } else {
        Relay::set(param, val, unitType);
    }
}

void busRelay::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    auto tripEvent = std::make_unique<Event>(0.0);

    tripEvent->setValue(0.0);
    tripEvent->setTarget(m_sinkObject, "status");

    add(std::shared_ptr<Event>(std::move(tripEvent)));

    add(std::shared_ptr<Condition>(make_condition("voltage", "<", mCutoutVoltage, m_sourceObject)));
    setActionTrigger(0, 0, mVoltageDelay);
    if ((mCutoutVoltage > 2.0) || (mCutoutVoltage <= 0)) {
        setConditionStatus(0, condition_status_t::disabled);
    }
    add(std::shared_ptr<Condition>(
        make_condition("frequency", "<", mCutoutFrequency, m_sourceObject)));
    setActionTrigger(0, 1, mFrequencyDelay);
    if ((mCutoutFrequency > 2.0) || (mCutoutFrequency <= 0)) {
        setConditionStatus(1, condition_status_t::disabled);
    }

    Relay::pFlowObjectInitializeA(time0, flags);
}

void busRelay::actionTaken(index_t /*actionNum*/,
                           index_t conditionNum,
                           change_code /*actionReturn*/,
                           coreTime /*actionTime*/)
{
    if (conditionNum == 0) {
        alert(m_sourceObject, BUS_UNDER_VOLTAGE);
    } else if (conditionNum == 1) {
        alert(m_sourceObject, BUS_UNDER_FREQUENCY);
    }
}
}  // namespace griddyn::relays

