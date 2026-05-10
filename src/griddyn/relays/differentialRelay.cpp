/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "differentialRelay.h"

#include "../Link.h"
#include "../comms/Communicator.h"
#include "../comms/relayMessage.h"
#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../gridBus.h"
#include "../measurement/Condition.h"
#include "core/coreObjectTemplates.hpp"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
differentialRelay::differentialRelay(const std::string& objName): Relay(objName)
{
    opFlags.set(continuous_flag);
}

coreObject* differentialRelay::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<differentialRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mMaxDifferential = mMaxDifferential;
    nobj->mDelayTime = mDelayTime;
    nobj->m_resetMargin = m_resetMargin;
    nobj->mMinLevel = mMinLevel;
    return nobj;
}

void differentialRelay::setFlag(std::string_view flag, bool val)
{
    if (flag == "relative") {
        opFlags.set(relative_differential_flag, val);
    }
    if (flag == "absolute") {
        opFlags.set(relative_differential_flag, !val);
    } else {
        Relay::setFlag(flag, val);
    }
}

bool differentialRelay::getFlag(std::string_view flag) const
{
    if (flag == "relative") {
        return opFlags[relative_differential_flag];
    }
    return Relay::getFlag(flag);
}

void differentialRelay::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void differentialRelay::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    const stringVec numericParameterStrings{"delay", "max_difference", "reset_margin", "minlevel"};
    const stringVec stringParameterStrings{};
    getParamString<differentialRelay, Relay>(
        this, pstr, numericParameterStrings, stringParameterStrings, {}, pstype);
}

void differentialRelay::set(std::string_view param, double val, units::unit unitType)
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

void differentialRelay::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    // if the target object is a link of some kind
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        const double tap = m_sourceObject->get("tap");
        if (opFlags[relative_differential_flag]) {
            if (tap != 1.0) {
                const std::string current1Expression = std::to_string(tap) + "*current1";
                add(std::shared_ptr<Condition>(
                    make_condition("abs(" + current1Expression + "-current2)/max(abs(" +
                                       current1Expression + "),abs(current2))",
                                   ">",
                                   mMaxDifferential,
                                   m_sourceObject)));
                if (mMinLevel > 0.0) {
                    add(std::shared_ptr<Condition>(
                        make_condition("max(abs(" + current1Expression + "),abs(current2))",
                                       ">",
                                       mMinLevel,
                                       m_sourceObject)));
                }
            } else {
                add(std::shared_ptr<Condition>(
                    make_condition("abs(current1-current2)/max(abs(current1),abs(current2))",
                                   ">",
                                   mMaxDifferential,
                                   m_sourceObject)));
                if (mMinLevel > 0.0) {
                    add(std::shared_ptr<Condition>(make_condition(
                        "max(abs(current1),abs(current2))", ">", mMinLevel, m_sourceObject)));
                }
            }
        } else {
            if (tap != 1.0) {
                add(std::shared_ptr<Condition>(
                    make_condition("abs(" + std::to_string(tap) + "*current1-current2)",
                                   ">",
                                   mMaxDifferential,
                                   m_sourceObject)));
            } else {
                add(std::shared_ptr<Condition>(make_condition(
                    "abs(current1-current2)", ">", mMaxDifferential, m_sourceObject)));
            }
        }
        opFlags.set(link_mode);
        opFlags.reset(bus_mode);
    } else if (dynamic_cast<gridBus*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            make_condition("abs(load)", "<=", mMaxDifferential, m_sourceObject)));
        opFlags.set(bus_mode);
        opFlags.reset(link_mode);
    }

    // using make shared here since we need a shared object and it won't get translated
    auto tripEvent = std::make_shared<Event>();
    tripEvent->setTarget(m_sinkObject, "connected");
    tripEvent->setValue(0.0);
    // action 2 to re-enable object

    add(std::move(tripEvent));
    if ((opFlags[relative_differential_flag]) && (opFlags[link_mode]) && (mMinLevel > 0.0)) {
        setActionMultiTrigger(0, {0, 1}, mDelayTime);
    } else {
        setActionTrigger(0, 0, mDelayTime);
    }

    Relay::pFlowObjectInitializeA(time0, flags);
}

void differentialRelay::actionTaken(index_t ActionNum,
                                    index_t /*conditionNum*/,
                                    change_code /*actionReturn*/,
                                    coreTime /*actionTime*/)
{
    logging::normal(this, "Relay Tripped");

    if (opFlags[use_commLink]) {
        if (ActionNum == 0) {
            auto relayEvent =
                std::make_shared<comms::relayMessage>(comms::relayMessage::BREAKER_TRIP_EVENT);
            cManager.send(std::move(relayEvent));
        }
    }
}

void differentialRelay::conditionTriggered(index_t /*conditionNum*/, coreTime /*triggerTime*/)
{
    logging::normal(this, "differential condition met");
    if (opFlags.test(use_commLink)) {
        // std::cout << "GridDyn conditionTriggered(), conditionNum = " << conditionNum << '\n';
        auto relayEvent =
            std::make_shared<comms::relayMessage>(comms::relayMessage::LOCAL_FAULT_EVENT);
        cManager.send(relayEvent);
    }
}

void differentialRelay::conditionCleared(index_t /*conditionNum*/, coreTime /*triggerTime*/)
{
    logging::normal(this, "differential condition cleared");

    if (opFlags.test(use_commLink)) {
        auto relayEvent =
            std::make_shared<comms::relayMessage>(comms::relayMessage::LOCAL_FAULT_CLEARED);
        cManager.send(relayEvent);
    }
}

void differentialRelay::receiveMessage(std::uint64_t /*sourceID*/,
                                       std::shared_ptr<commMessage> message)
{
    switch (message->getMessageType()) {
        case comms::relayMessage::BREAKER_TRIP_COMMAND:
            triggerAction(0);
            break;
        case comms::relayMessage::BREAKER_CLOSE_COMMAND:
            if (m_sinkObject != nullptr) {
                m_sinkObject->set("enable", 1);
            }
            break;
        case comms::relayMessage::BREAKER_OOS_COMMAND:

            setConditionStatus(0, condition_status_t::disabled);
            break;
        default: {
            assert(false);
        }
    }
}

}  // namespace griddyn::relays
