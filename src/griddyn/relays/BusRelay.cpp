/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "BusRelay.h"

#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../measurement/Condition.h"
#include "core/CoreObjectTemplates.hpp"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
BusRelay::BusRelay(const std::string& objName): Relay(objName)
{
    opFlags.set(CONTINUOUS_FLAG);
    opFlags.set(POWER_FLOW_CHECKS_FLAG);  // enable power flow checks for BusRelay
}

CoreObject* BusRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<BusRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mCutoutVoltage = mCutoutVoltage;
    nobj->mCutoutFrequency = mCutoutFrequency;
    nobj->mVoltageDelay = mVoltageDelay;
    nobj->mFrequencyDelay = mFrequencyDelay;
    return nobj;
}

void BusRelay::setFlag(std::string_view flag, bool val)
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
void BusRelay::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void BusRelay::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "cutoutvoltage") || (param == "voltagelimit")) {
        mCutoutVoltage = units::convert(val, unitType, units::puV, systemBasePower, baseVoltage());
        if (opFlags[DYN_INITIALIZED]) {
            setConditionLevel(0, mCutoutVoltage);
        }
    } else if ((param == "cutoutfrequency") || (param == "freqlimit")) {
        mCutoutFrequency = units::convert(val, unitType, units::puHz, systemBaseFrequency);
        if (opFlags[DYN_INITIALIZED]) {
            setConditionLevel(1, mCutoutFrequency);
        }
    } else if (param == "delay") {
        mVoltageDelay = val;
        mFrequencyDelay = val;
        if (opFlags[DYN_INITIALIZED]) {
            setActionTrigger(0, 0, mVoltageDelay);
            setActionTrigger(0, 1, mFrequencyDelay);
        }
    } else if (param == "voltagedelay") {
        mVoltageDelay = val;
        if (opFlags[DYN_INITIALIZED]) {
            setActionTrigger(0, 0, mVoltageDelay);
        }
    } else if (param == "frequencydelay") {
        mFrequencyDelay = val;
        if (opFlags[DYN_INITIALIZED]) {
            setActionTrigger(0, 1, mFrequencyDelay);
        }
    } else {
        Relay::set(param, val, unitType);
    }
}

void BusRelay::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    auto tripEvent = std::make_unique<Event>(0.0);

    tripEvent->setValue(0.0);
    tripEvent->setTarget(m_sinkObject, "status");

    add(std::shared_ptr<Event>(std::move(tripEvent)));

    add(std::shared_ptr<Condition>(makeCondition("voltage", "<", mCutoutVoltage, m_sourceObject)));
    setActionTrigger(0, 0, mVoltageDelay);
    if ((mCutoutVoltage > 2.0) || (mCutoutVoltage <= 0)) {
        setConditionStatus(0, ConditionStatus::DISABLED);
    }
    add(std::shared_ptr<Condition>(
        makeCondition("frequency", "<", mCutoutFrequency, m_sourceObject)));
    setActionTrigger(0, 1, mFrequencyDelay);
    if ((mCutoutFrequency > 2.0) || (mCutoutFrequency <= 0)) {
        setConditionStatus(1, ConditionStatus::DISABLED);
    }

    Relay::pFlowObjectInitializeA(time0, flags);
}

void BusRelay::actionTaken(index_t /*actionNum*/,
                           index_t conditionNum,
                           ChangeCode /*actionReturn*/,
                           CoreTime /*actionTime*/)
{
    if (conditionNum == 0) {
        alert(m_sourceObject, BUS_UNDER_VOLTAGE);
    } else if (conditionNum == 1) {
        alert(m_sourceObject, BUS_UNDER_FREQUENCY);
    }
}
}  // namespace griddyn::relays
