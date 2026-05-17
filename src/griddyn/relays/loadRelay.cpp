/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "loadRelay.h"

#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "core/coreObjectTemplates.hpp"
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
loadRelay::loadRelay(const std::string& objName): Relay(objName)
{
    // opFlags.set(continuous_flag);
}

CoreObject* loadRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<loadRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->mCutoutVoltage = mCutoutVoltage;
    nobj->mCutoutFrequency = mCutoutFrequency;
    nobj->mVoltageDelay = mVoltageDelay;
    nobj->mFrequencyDelay = mFrequencyDelay;
    nobj->mOffTime = mOffTime;
    return nobj;
}

void loadRelay::setFlag(std::string_view flag, bool val)
{
    if (flag == "nondirectional") {
        opFlags.set(NONDIRECTIONAL_FLAG, val);
    } else {
        Relay::setFlag(flag, val);
    }
}
/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void loadRelay::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Relay::set(param, val);
    }
}

void loadRelay::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "cutoutvoltage") || (param == "voltagelimit")) {
        mCutoutVoltage = units::convert(val, unitType, units::puV, systemBasePower, baseVoltage());
    } else if ((param == "cutoutfrequency") || (param == "freqlimit")) {
        mCutoutFrequency = units::convert(val, unitType, units::puHz, systemBaseFrequency);
    } else if (param == "delay") {
        mVoltageDelay = val;
        mFrequencyDelay = val;
    } else if (param == "voltagedelay") {
        mVoltageDelay = val;
    } else if (param == "frequencydelay") {
        mFrequencyDelay = val;
    } else if (param == "offtime") {
        mOffTime = val;
    } else {
        Relay::set(param, val, unitType);
    }
}

void loadRelay::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    auto tripEvent = std::make_shared<Event>();

    tripEvent->setTarget(m_sinkObject, "status");
    tripEvent->setValue(0.0);

    add(std::move(tripEvent));
    add(std::shared_ptr<Condition>(make_condition("voltage", "<", mCutoutVoltage, m_sourceObject)));
    add(std::shared_ptr<Condition>(
        make_condition("frequency", "<", mCutoutFrequency, m_sourceObject)));
    if (mCutoutVoltage < 2.0) {
        setActionTrigger(0, 0, mVoltageDelay);
    } else {
        setConditionStatus(0, condition_status_t::disabled);
    }
    if (mCutoutFrequency < 2.0) {
        setActionTrigger(0, 1, mFrequencyDelay);
    } else {
        setConditionStatus(1, condition_status_t::disabled);
    }

    Relay::dynObjectInitializeA(time0, flags);
}

void loadRelay::actionTaken(index_t ActionNum,
                            index_t conditionNum,
                            change_code /*actionReturn*/,
                            coreTime /*actionTime*/)
{
    logging::normal(this, "condition {} action {}", conditionNum, ActionNum);
    (void)ActionNum;
    (void)conditionNum;
    /*
if (opFlags.test (use_commLink))
{
relayMessage P;
if (ActionNum == 0)
{
P.setMessageType (relayMessage::MESSAGE_TYPE::BREAKER_TRIP_EVENT);
if (commDestName.empty ())
{
auto b = P.buffer ();
commLink->transmit (commDestId, static_cast<int> (P.GetMessageType ()), P.size (), b);
}
else
{
auto b = P.buffer ();
commLink->transmit (commDestName, static_cast<int> (P.GetMessageType ()), P.size (), b);
}
}
}
for (size_t kk = conditionNum + 1; kk < m_zones; ++kk)
{
setConditionStatus (kk, condition_status_t::disabled);
}
if (conditionNum < m_condition_level)
{
m_condition_level = conditionNum;
}
*/
}

void loadRelay::conditionTriggered(index_t conditionNum, coreTime /*triggerTime*/)
{
    logging::normal(this, "condition {} triggered", conditionNum);
    (void)conditionNum;
    /*
if (conditionNum < m_condition_level)
{
m_condition_level = conditionNum;
}
if (opFlags.test (use_commLink))
{
if (conditionNum > m_condition_level)
{
return;
}
relayMessage P;
//std::cout << "GridDyn conditionTriggered(), conditionNum = " << conditionNum << '\n';
if (conditionNum == 0)
{
//std::cout << "GridDyn setting relay message type to LOCAL_FAULT_EVENT" << '\n';
P.setMessageType (relayMessage::MESSAGE_TYPE::LOCAL_FAULT_EVENT);
}
else
{
//std::cout << "GridDyn setting relay message type to REMOTE_FAULT_EVENT" << '\n';
P.setMessageType (relayMessage::MESSAGE_TYPE::REMOTE_FAULT_EVENT);
}
if (commDestName.empty ())
{
auto b = P.buffer ();
commLink->transmit (commDestId, static_cast<int> (P.GetMessageType ()), P.size (), b);
}
else
{
auto b = P.buffer ();
commLink->transmit (commDestName, static_cast<int> (P.GetMessageType ()), P.size (), b);
}
}
*/
}

void loadRelay::conditionCleared(index_t conditionNum, coreTime /*triggerTime*/)
{
    logging::normal(this, "condition {} cleared", conditionNum);
    (void)conditionNum;
    /*for (size_t kk = 0; kk < m_zones; ++kk)
{
if (cStates[kk] == condition_status_t::active)
{
m_condition_level = kk + 1;
}
else
{
return;
}
}
if (opFlags.test (use_commLink))
{
relayMessage P;
if (conditionNum == 0)
{
P.setMessageType (relayMessage::MESSAGE_TYPE::LOCAL_FAULT_CLEARED);
}
else
{
P.setMessageType (relayMessage::MESSAGE_TYPE::REMOTE_FAULT_CLEARED);
}
if (commDestName.empty ())
{
auto b = P.buffer ();
commLink->transmit (commDestId, static_cast<int> (P.GetMessageType ()), P.size (), b);
}
else
{
auto b = P.buffer ();
commLink->transmit (commDestName, static_cast<int> (P.GetMessageType ()), P.size (), b);
}
}
*/
}
}  // namespace griddyn::relays
