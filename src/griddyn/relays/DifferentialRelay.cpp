/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DifferentialRelay.h"

#include "../GridBus.h"
#include "../Link.h"
#include "../comms/Communicator.h"
#include "../comms/relayMessage.h"
#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../measurement/Condition.h"
#include "core/CoreObjectTemplates.hpp"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
DifferentialRelay::DifferentialRelay(const std::string& objName): Relay(objName)
{
    opFlags.set(continuousFlag);
}

CoreObject* DifferentialRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<DifferentialRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mMaxDifferential = mMaxDifferential;
    nobj->mDelayTime = mDelayTime;
    nobj->m_resetMargin = m_resetMargin;
    nobj->mMinLevel = mMinLevel;
    return nobj;
}

void DifferentialRelay::setFlag(std::string_view flag, bool val)
{
    if (flag == "relative") {
        opFlags.set(relativeDifferentialFlag, val);
    }
    if (flag == "absolute") {
        opFlags.set(relativeDifferentialFlag, !val);
    } else {
        Relay::setFlag(flag, val);
    }
}

bool DifferentialRelay::getFlag(std::string_view flag) const
{
    if (flag == "relative") {
        return opFlags[relativeDifferentialFlag];
    }
    return Relay::getFlag(flag);
}

void DifferentialRelay::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void DifferentialRelay::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const stringVec numericParameterStrings{"delay", "max_difference", "reset_margin", "minlevel"};
    const stringVec stringParameterStrings{};
    getParamString<DifferentialRelay, Relay>(
        this, pstr, numericParameterStrings, stringParameterStrings, {}, pstype);
}

void DifferentialRelay::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "delay") {
        mDelayTime = val;
    } else if ((param == "level") || (param == "max_difference")) {
        mMaxDifferential = val;
    } else if (param == "reset_margin") {
        m_resetMargin = val;
    } else if (param == "minlevel") {
        mMinLevel = val;
    } else {
        Relay::set(param, val, unitType);
    }
}

void DifferentialRelay::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    // if the target object is a link of some kind
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        const double tap = m_sourceObject->get("tap");
        if (opFlags[relativeDifferentialFlag]) {
            if (tap != 1.0) {
                const std::string current1Expression = std::to_string(tap) + "*current1";
                add(std::shared_ptr<Condition>(
                    makeCondition("abs(" + current1Expression + "-current2)/max(abs(" +
                                      current1Expression + "),abs(current2))",
                                  ">",
                                  mMaxDifferential,
                                  m_sourceObject)));
                if (mMinLevel > 0.0) {
                    add(std::shared_ptr<Condition>(
                        makeCondition("max(abs(" + current1Expression + "),abs(current2))",
                                      ">",
                                      mMinLevel,
                                      m_sourceObject)));
                }
            } else {
                add(std::shared_ptr<Condition>(
                    makeCondition("abs(current1-current2)/max(abs(current1),abs(current2))",
                                  ">",
                                  mMaxDifferential,
                                  m_sourceObject)));
                if (mMinLevel > 0.0) {
                    add(std::shared_ptr<Condition>(makeCondition(
                        "max(abs(current1),abs(current2))", ">", mMinLevel, m_sourceObject)));
                }
            }
        } else {
            if (tap != 1.0) {
                add(std::shared_ptr<Condition>(
                    makeCondition("abs(" + std::to_string(tap) + "*current1-current2)",
                                  ">",
                                  mMaxDifferential,
                                  m_sourceObject)));
            } else {
                add(std::shared_ptr<Condition>(makeCondition(
                    "abs(current1-current2)", ">", mMaxDifferential, m_sourceObject)));
            }
        }
        opFlags.set(linkMode);
        opFlags.reset(busMode);
    } else if (dynamic_cast<GridBus*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            makeCondition("abs(load)", "<=", mMaxDifferential, m_sourceObject)));
        opFlags.set(busMode);
        opFlags.reset(linkMode);
    }

    // using make shared here since we need a shared object and it won't get translated
    auto tripEvent = std::make_shared<Event>();
    tripEvent->setTarget(m_sinkObject, "connected");
    tripEvent->setValue(0.0);
    // action 2 to re-enable object

    add(std::move(tripEvent));
    if ((opFlags[relativeDifferentialFlag]) && (opFlags[linkMode]) && (mMinLevel > 0.0)) {
        setActionMultiTrigger(0, {0, 1}, mDelayTime);
    } else {
        setActionTrigger(0, 0, mDelayTime);
    }

    Relay::pFlowObjectInitializeA(time0, flags);
}

void DifferentialRelay::actionTaken(index_t actionNum,
                                    index_t /*conditionNum*/,
                                    ChangeCode /*actionReturn*/,
                                    coreTime /*actionTime*/)
{
    logging::normal(this, "Relay Tripped");

    if (opFlags[useCommLink]) {
        if (actionNum == 0) {
            auto relayEvent = std::make_shared<CommMessage>(CommMessage::BREAKER_TRIP_EVENT);
            cManager.send(relayEvent);
        }
    }
}

void DifferentialRelay::conditionTriggered(index_t /*conditionNum*/, coreTime /*triggerTime*/)
{
    logging::normal(this, "differential condition met");
    if (opFlags.test(useCommLink)) {
        // std::cout << "GridDyn conditionTriggered(), conditionNum = " << conditionNum << '\n';
        auto relayEvent = std::make_shared<CommMessage>(CommMessage::LOCAL_FAULT_EVENT);
        cManager.send(relayEvent);
    }
}

void DifferentialRelay::conditionCleared(index_t /*conditionNum*/, coreTime /*triggerTime*/)
{
    logging::normal(this, "differential condition cleared");

    if (opFlags.test(useCommLink)) {
        auto relayEvent = std::make_shared<CommMessage>(CommMessage::LOCAL_FAULT_CLEARED);
        cManager.send(relayEvent);
    }
}

void DifferentialRelay::receiveMessage(std::uint64_t /*sourceID*/,
                                       std::shared_ptr<CommMessage> message)
{
    switch (message->getMessageType()) {
        case CommMessage::BREAKER_TRIP_COMMAND:
            triggerAction(0);
            break;
        case CommMessage::BREAKER_CLOSE_COMMAND:
            if (m_sinkObject != nullptr) {
                m_sinkObject->set("enable", 1);
            }
            break;
        case CommMessage::BREAKER_OOS_COMMAND:

            setConditionStatus(0, ConditionStatus::disabled);
            break;
        default: {
            assert(false);
        }
    }
}

}  // namespace griddyn::relays
