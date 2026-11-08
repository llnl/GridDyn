/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LoadRelay.h"

#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
const ModelDescription& LoadRelay::getModelDescription() const
{
    static const auto description = ModelDescription::define<LoadRelay>(
        "LoadRelay",
        &Relay::getModelDescription(),
        {flag({"nondirectional"}, &LoadRelay::opFlags, NONDIRECTIONAL_FLAG, "Nondirectional"),
         command({"cutoutvoltage", "voltagelimit"}, units::defunit, "Cutoutvoltage"),
         command({"cutoutfrequency", "freqlimit"}, units::defunit, "Cutoutfrequency"),
         command({"delay"}, units::defunit, "Delay"),
         parameter({"voltagedelay"},
                   &LoadRelay::mVoltageDelay,
                   units::defunit,
                   "The delay on the voltage trip"),
         parameter({"frequencydelay"},
                   &LoadRelay::mFrequencyDelay,
                   units::defunit,
                   "The delay on the frequency tripping"),
         parameter({"offtime"},
                   &LoadRelay::mOffTime,
                   units::defunit,
                   "The time before the load comes back on line if the trip")});
    return description;
}

LoadRelay::LoadRelay(const std::string& objName): Relay(objName)
{
    // opFlags.set(CONTINUOUS_FLAG);
}

CoreObject* LoadRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<LoadRelay, Relay>(this, obj);
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

/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void LoadRelay::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Relay::set(param, val);
    }
}

void LoadRelay::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(LoadRelay::getModelDescription().trySet(*this, param, val, unitType))) {
        return;
    }
    if ((param == "cutoutvoltage") || (param == "voltagelimit")) {
        mCutoutVoltage = units::convert(val, unitType, units::puV, systemBasePower, baseVoltage());
    } else if ((param == "cutoutfrequency") || (param == "freqlimit")) {
        mCutoutFrequency = units::convert(val, unitType, units::puHz, systemBaseFrequency);
    } else if (param == "delay") {
        mVoltageDelay = val;
        mFrequencyDelay = val;
    } else {
        Relay::set(param, val, unitType);
    }
}

void LoadRelay::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    auto tripEvent = std::make_shared<Event>();

    tripEvent->setTarget(m_sinkObject, "status");
    tripEvent->setValue(0.0);

    add(std::move(tripEvent));
    add(std::shared_ptr<Condition>(makeCondition("voltage", "<", mCutoutVoltage, m_sourceObject)));
    add(std::shared_ptr<Condition>(
        makeCondition("frequency", "<", mCutoutFrequency, m_sourceObject)));
    if (mCutoutVoltage < 2.0) {
        setActionTrigger(0, 0, mVoltageDelay);
    } else {
        setConditionStatus(0, ConditionStatus::DISABLED);
    }
    if (mCutoutFrequency < 2.0) {
        setActionTrigger(0, 1, mFrequencyDelay);
    } else {
        setConditionStatus(1, ConditionStatus::DISABLED);
    }

    Relay::dynObjectInitializeA(time0, flags);
}

void LoadRelay::actionTaken(index_t actionNum,
                            index_t conditionNum,
                            ChangeCode /*actionReturn*/,
                            CoreTime /*actionTime*/)
{
    logging::normal(this, "condition {} action {}", conditionNum, actionNum);
    (void)actionNum;
    (void)conditionNum;
    /*
if (opFlags.test(USE_COMM_LINK))
{
relayMessage P;
if (actionNum == 0)
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
setConditionStatus (kk, ConditionStatus::DISABLED);
}
if (conditionNum < m_condition_level)
{
m_condition_level = conditionNum;
}
*/
}

void LoadRelay::conditionTriggered(index_t conditionNum, CoreTime /*triggerTime*/)
{
    logging::normal(this, "condition {} triggered", conditionNum);
    (void)conditionNum;
    /*
if (conditionNum < m_condition_level)
{
m_condition_level = conditionNum;
}
if (opFlags.test(USE_COMM_LINK))
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

void LoadRelay::conditionCleared(index_t conditionNum, CoreTime /*triggerTime*/)
{
    logging::normal(this, "condition {} cleared", conditionNum);
    (void)conditionNum;
    /*for (size_t kk = 0; kk < m_zones; ++kk)
{
if (cStates[kk] == ConditionStatus::ACTIVE)
{
m_condition_level = kk + 1;
}
else
{
return;
}
}
if (opFlags.test(USE_COMM_LINK))
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
