/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ControlRelay.h"

#include "core/CoreExceptions.h"
// #include "utilities/TimeSeries.hpp"
#include "../comms/Communicator.h"
#include "../comms/ControlMessage.h"
#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../measurement/GridGrabbers.h"
#include "../simulation/GridSimulation.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
ControlRelay::ControlRelay(const std::string& objName): Relay(objName) {}

CoreObject* ControlRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<ControlRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->autoName = autoName;
    nobj->actionDelay = actionDelay;
    nobj->measureDelay = measureDelay;
    nobj->m_terminal = m_terminal;
    return nobj;
}

void ControlRelay::setFlag(std::string_view flag, bool val)
{
    if (flag == "noreply") {
        opFlags.set(NO_MESSAGE_REPLY, val);
    } else {
        Relay::setFlag(flag, val);
    }
}

void ControlRelay::addMeasurement(std::string_view measure)
{
    CoreObject* targetObject = getParent();
    if (m_sourceObject != nullptr) {
        targetObject = m_sourceObject;
    } else if (m_sinkObject != nullptr) {
        targetObject = m_sinkObject;
    }
    auto vals = makeGrabbers(measure, targetObject);

    for (auto& ggb : vals) {
        pointNames_.emplace(gmlc::utilities::convertToLowerCase(ggb->getDesc()),
                            static_cast<index_t>(measurement_points_.size()));
        measurement_points_.push_back(std::move(ggb));
    }
}

double ControlRelay::getMeasurement(index_t num) const
{
    if (isValidIndex(num, measurement_points_)) {
        return measurement_points_[num]->grabData();
    }
    return kNullVal;
}

double ControlRelay::getMeasurement(std::string_view pointName) const
{
    auto ind = findMeasurement(pointName);
    return (ind != kNullLocation) ? measurement_points_[ind]->grabData() : kNullVal;
}

index_t ControlRelay::findMeasurement(std::string_view pointName) const
{
    auto fnd = pointNames_.find(pointName);
    return (fnd != pointNames_.end()) ? fnd->second : kNullLocation;
}
/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void ControlRelay::set(std::string_view param, std::string_view val)
{
    if (param == "measurement") {
        addMeasurement(std::string{val});
    } else {
        Relay::set(param, val);
    }
}

void ControlRelay::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "autoname") {
        autoName = static_cast<int>(val);
    } else if (param == "delay") {
        actionDelay = val;
    } else if (param == "terminal") {
        m_terminal = static_cast<index_t>(val);
        m_terminal_key = std::to_string(m_terminal);
    } else {
        Relay::set(param, val, unitType);
    }
}

void ControlRelay::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    rootSim = dynamic_cast<GridSimulation*>(getRoot());

    Relay::dynObjectInitializeA(time0, flags);
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        opFlags.set(LINK_TYPE_SOURCE);
    }
    if (dynamic_cast<Link*>(m_sinkObject) != nullptr) {
        opFlags.set(LINK_TYPE_SINK);
    }
}

void ControlRelay::actionTaken(index_t actionNum,
                               index_t conditionNum,
                               ChangeCode /*actionReturn*/,
                               CoreTime /*actionTime*/)
{
    logging::normal(this, "condition {}-> action {} taken", conditionNum, actionNum);
}

using cm = griddyn::comms::ControlMessagePayload;

void ControlRelay::receiveMessage(std::uint64_t sourceID, std::shared_ptr<CommMessage> message)
{
    auto* messagePayload = message->getPayload<cm>();
    index_t actnum;
    ++instructionCounter;

    switch (message->getMessageType()) {
        case cm::SET:
            if (messagePayload->m_time <= prevTime + kSmallTime) {
                if (actionDelay <= kSmallTime) {
                    auto fea = generateSetEvent(prevTime, sourceID, messagePayload);
                    fea->execute(prevTime);  // just execute the event immediately
                } else {
                    auto fea = generateSetEvent(prevTime + actionDelay, sourceID, messagePayload);
                    rootSim->add(std::shared_ptr<EventAdapter>(std::move(fea)));
                }
            } else {
                auto gres = std::make_shared<CommMessage>(cm::SET_SCHEDULED);
                gres->getPayload<cm>()->m_actionID = (messagePayload->m_actionID > 0) ?
                    messagePayload->m_actionID :
                    instructionCounter;

                commLink->transmit(sourceID, gres);
                // make the event
                auto fea = generateSetEvent(messagePayload->m_time, sourceID, messagePayload);
                rootSim->add(std::shared_ptr<EventAdapter>(std::move(fea)));
            }
            break;
        case cm::GET:
            if (messagePayload->m_time <= prevTime + kSmallTime) {
                if (measureDelay <= kSmallTime) {
                    // just generate the action and execute it
                    auto fea = generateGetEvent(prevTime, sourceID, messagePayload);
                    fea->execute(prevTime);  // just execute the event immediately
                } else {
                    auto fea = generateGetEvent(messagePayload->m_time + measureDelay,
                                                sourceID,
                                                messagePayload);
                    rootSim->add(std::shared_ptr<EventAdapter>(std::move(fea)));
                }
            } else {
                auto gres = std::make_shared<CommMessage>(cm::SET_SCHEDULED);
                gres->getPayload<cm>()->m_actionID = (messagePayload->m_actionID > 0) ?
                    messagePayload->m_actionID :
                    instructionCounter;
                commLink->transmit(sourceID, gres);
                auto fea = generateGetEvent(messagePayload->m_time, sourceID, messagePayload);
                rootSim->add(std::shared_ptr<EventAdapter>(std::move(fea)));
            }
            break;
        case cm::GET_MULTIPLE:
        case cm::GET_RESULT_MULTIPLE:
        case cm::SET_SUCCESS:
        case cm::SET_FAIL:
        case cm::GET_RESULT:
        case cm::SET_SCHEDULED:
        case cm::GET_SCHEDULED:
        case cm::GET_PERIODIC:
        case cm::CANCEL_FAIL:
        case cm::CANCEL_SUCCESS:
            break;
        case cm::CANCEL:
            actnum = findAction(messagePayload->m_actionID);

            if (actnum != kNullLocation) {
                if ((!actions[actnum].executed) &&
                    (actions[actnum].triggerTime >
                     actionDelay)) {  // can only cancel actions that have not executed and are
                                      // not closer than the actionDelay
                    actions[actnum].executed = true;
                    auto gres = std::make_shared<CommMessage>(cm::CANCEL_SUCCESS);
                    gres->getPayload<cm>()->m_actionID = messagePayload->m_actionID;
                    commLink->transmit(sourceID, gres);
                } else {
                    auto gres = std::make_shared<CommMessage>(cm::CANCEL_FAIL);
                    gres->getPayload<cm>()->m_actionID = messagePayload->m_actionID;
                    commLink->transmit(sourceID, gres);
                }
            } else {
                auto gres = std::make_shared<CommMessage>(cm::CANCEL_FAIL);
                gres->getPayload<cm>()->m_actionID = messagePayload->m_actionID;
                commLink->transmit(sourceID, gres);
            }
            break;
        default:
            break;
    }
}

std::string ControlRelay::generateCommName()
{
    if (autoName > 0) {
        auto aname = generateAutoName(autoName);
        if (aname != getName()) {
            setName(aname);
        }
        return aname;
    }
    return Relay::generateCommName();
}

std::string ControlRelay::generateAutoName(int code)
{
    switch (code) {
        case 1:
            return m_sinkObject->getName();
        case 2:
            return m_sourceObject->getName();
        default:
            return getName();
            // do nothing
    }
}

ChangeCode ControlRelay::executeAction(index_t actionNum)
{
    if (!isValidIndex(actionNum, actions)) {
        return ChangeCode::NOT_TRIGGERED;
    }
    auto& cact = actions[actionNum];
    if (!cact.executed) {
        cact.executed = true;
        cact.executionTime = prevTime;

        if (cact.measureAction) {
            auto findLoc = findMeasurement(cact.field);
            double val;
            if (findLoc != kNullLocation) {
                val = getMeasurement(findLoc);
            } else {
                if (opFlags[LINK_TYPE_SOURCE]) {
                    val = m_sourceObject->get(cact.field + m_terminal_key, cact.unitType);
                    if (val == kNullVal) {
                        val = m_sourceObject->get(cact.field, cact.unitType);
                    }
                } else {
                    val = m_sourceObject->get(cact.field, cact.unitType);
                }
            }
            auto gres = std::make_shared<CommMessage>(cm::GET_RESULT);
            auto* payload = gres->getPayload<cm>();
            payload->m_field = cact.field;
            payload->m_value = val;
            payload->m_time = prevTime;
            commLink->transmit(cact.sourceID, gres);
            return ChangeCode::NO_CHANGE;
        }

        try {
            std::string field;

            if (opFlags[LINK_TYPE_SINK]) {
                if ((cact.field == "breaker") || (cact.field == "switch") ||
                    (cact.field == "breaker_open")) {
                    field = cact.field + m_terminal_key;
                } else {
                    field = cact.field;
                }
            } else {
                field = cact.field;
            }
            m_sinkObject->set(field, cact.val, cact.unitType);

            if (!opFlags[NO_MESSAGE_REPLY])  // unless told not to respond return with the
            {
                auto gres = std::make_shared<CommMessage>(cm::SET_SUCCESS);
                gres->getPayload<cm>()->m_actionID = cact.actionID;
                commLink->transmit(cact.sourceID, gres);
            }
            return ChangeCode::PARAMETER_CHANGE;
        }
        catch (const std::invalid_argument&) {
            if (!opFlags[NO_MESSAGE_REPLY])  // unless told not to respond return with the
            {
                auto gres = std::make_shared<CommMessage>(cm::SET_FAIL);
                gres->getPayload<cm>()->m_actionID = cact.actionID;
                commLink->transmit(cact.sourceID, gres);
            }
            return ChangeCode::EXECUTION_FAILURE;
        }
    }
    return ChangeCode::NOT_TRIGGERED;
}

void ControlRelay::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    Relay::updateObject(obj, mode);
    if (opFlags[DYN_INITIALIZED]) {
        rootSim = dynamic_cast<GridSimulation*>(getRoot());

        if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
            opFlags.set(LINK_TYPE_SOURCE);
        }
        if (dynamic_cast<Link*>(m_sinkObject) != nullptr) {
            opFlags.set(LINK_TYPE_SINK);
        }
    }
}

std::unique_ptr<FunctionEventAdapter>
    ControlRelay::generateGetEvent(CoreTime eventTime, std::uint64_t sourceID, cm* message)
{
    auto act = getFreeAction();
    auto& action = actions[act];
    action = {};
    action.actionID = (message->m_actionID > 0) ? message->m_actionID : instructionCounter;
    action.executed = false;
    action.measureAction = true;
    action.sourceID = sourceID;
    action.triggerTime = eventTime;
    gmlc::utilities::makeLowerCase(message->m_field);
    action.field = message->m_field;
    if (!(message->m_units.empty())) {
        action.unitType = units::unit_cast_from_string(message->m_units);
    }
    auto fea = std::make_unique<FunctionEventAdapter>([act, this]() { return executeAction(act); },
                                                      eventTime);
    /** this is so the get event triggers last*/
    fea->setExecutionMode(EventExecutionMode::DELAYED);
    return fea;
}

std::unique_ptr<FunctionEventAdapter>
    ControlRelay::generateSetEvent(CoreTime eventTime, std::uint64_t sourceID, cm* message)
{
    auto act = getFreeAction();
    auto& action = actions[act];
    action = {};
    action.actionID = (message->m_actionID > 0) ? message->m_actionID : instructionCounter;
    action.executed = false;
    action.measureAction = false;
    action.sourceID = sourceID;
    action.triggerTime = eventTime;
    gmlc::utilities::makeLowerCase(message->m_field);
    action.field = message->m_field;
    action.val = message->m_value;

    if (!message->m_units.empty()) {
        action.unitType = units::unit_cast_from_string(message->m_units);
    }

    auto fea = std::make_unique<FunctionEventAdapter>([act, this]() { return executeAction(act); },
                                                      eventTime);
    return fea;
}

index_t ControlRelay::findAction(std::uint64_t actionID)
{
    auto res = std::find_if(actions.begin(), actions.end(), [actionID](const auto& act) {
        return (act.actionID == actionID);
    });
    return (res != actions.end()) ? static_cast<index_t>(res - actions.begin()) : kNullLocation;
}

index_t ControlRelay::getFreeAction()
{
    auto asize = static_cast<index_t>(actions.size());
    for (index_t act = 0; act < asize; ++act) {
        if (actions[act].executed) {
            return act;
        }
    }
    // if we didn't find an open one,  make the actions vector longer and return the new index

    actions.resize((static_cast<size_t>(asize) + 1) * 2);  // double the size
    return asize;
}
}  // namespace griddyn::relays
