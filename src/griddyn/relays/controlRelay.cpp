/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "controlRelay.h"

#include "core/coreExceptions.h"
// #include "utilities/TimeSeries.hpp"
#include "../comms/Communicator.h"
#include "../comms/controlMessage.h"
#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../measurement/gridGrabbers.h"
#include "../simulation/gridSimulation.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
namespace relays {
    controlRelay::controlRelay(const std::string& objName): Relay(objName) {}

    coreObject* controlRelay::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<controlRelay, Relay>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }

        nobj->autoName = autoName;
        nobj->actionDelay = actionDelay;
        nobj->measureDelay = measureDelay;
        nobj->m_terminal = m_terminal;
        return nobj;
    }

    void controlRelay::setFlag(const std::string& flag, bool val)
    {
        if (flag == "noreply") {
            opFlags.set(no_message_reply, val);
        } else {
            Relay::setFlag(flag, val);
        }
    }

    void controlRelay::addMeasurement(const std::string& measure)
    {
        auto vals = makeGrabbers(measure,
                                 (m_sourceObject != nullptr)   ? m_sourceObject :
                                     (m_sinkObject != nullptr) ? m_sinkObject :
                                                                 getParent());

        for (auto& ggb : vals) {
            pointNames_.emplace(gmlc::utilities::convertToLowerCase(ggb->getDesc()),
                                static_cast<index_t>(measurement_points_.size()));
            measurement_points_.push_back(std::move(ggb));
        }
    }

    double controlRelay::getMeasurement(index_t num) const
    {
        if (isValidIndex(num, measurement_points_)) {
            return measurement_points_[num]->grabData();
        }
        return kNullVal;
    }

    double controlRelay::getMeasurement(const std::string& pointName) const
    {
        auto ind = findMeasurement(pointName);
        return (ind != kNullLocation) ? measurement_points_[ind]->grabData() : kNullVal;
    }

    index_t controlRelay::findMeasurement(const std::string& pointName) const
    {
        auto fnd = pointNames_.find(pointName);
        return (fnd != pointNames_.end()) ? fnd->second : kNullLocation;
    }
    /*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
    void controlRelay::set(const std::string& param, const std::string& val)
    {
        if (param == "measurement") {
            addMeasurement(val);
        } else {
            Relay::set(param, val);
        }
    }

    void controlRelay::set(const std::string& param, double val, units::unit unitType)
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

    void controlRelay::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        rootSim = dynamic_cast<gridSimulation*>(getRoot());

        Relay::dynObjectInitializeA(time0, flags);
        if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
            opFlags.set(link_type_source);
        }
        if (dynamic_cast<Link*>(m_sinkObject) != nullptr) {
            opFlags.set(link_type_sink);
        }
    }

    void controlRelay::actionTaken(index_t ActionNum,
                                   index_t conditionNum,
                                   change_code /*actionReturn*/,
                                   coreTime /*actionTime*/)
    {
        LOG_NORMAL(std::format("condition {}-> action {} taken", conditionNum, ActionNum));
        (void)(ActionNum);
        (void)(conditionNum);
    }

    using cm = griddyn::comms::controlMessagePayload;

    void controlRelay::receiveMessage(std::uint64_t sourceID, std::shared_ptr<commMessage> message)
    {
        auto m = message->getPayload<cm>();
        index_t actnum;
        ++instructionCounter;

        switch (message->getMessageType()) {
            case cm::SET:
                if (m->m_time <= prevTime + kSmallTime) {
                    if (actionDelay <= kSmallTime) {
                        auto fea = generateSetEvent(prevTime, sourceID, m);
                        fea->execute(prevTime);  // just execute the event immediately
                    } else {
                        auto fea = generateSetEvent(prevTime + actionDelay, sourceID, m);
                        rootSim->add(std::shared_ptr<eventAdapter>(std::move(fea)));
                    }
                } else {
                    auto gres = std::make_shared<commMessage>(cm::SET_SCHEDULED);
                    gres->getPayload<cm>()->m_actionID =
                        (m->m_actionID > 0) ? m->m_actionID : instructionCounter;

                    commLink->transmit(sourceID, std::move(gres));
                    // make the event
                    auto fea = generateSetEvent(m->m_time, sourceID, m);
                    rootSim->add(std::shared_ptr<eventAdapter>(std::move(fea)));
                }
                break;
            case cm::GET:
                if (m->m_time <= prevTime + kSmallTime) {
                    if (measureDelay <= kSmallTime) {
                        // just generate the action and execute it
                        auto fea = generateGetEvent(prevTime, sourceID, m);
                        fea->execute(prevTime);  // just execute the event immediately
                    } else {
                        auto fea = generateGetEvent(m->m_time + measureDelay, sourceID, m);
                        rootSim->add(std::shared_ptr<eventAdapter>(std::move(fea)));
                    }
                } else {
                    auto gres = std::make_shared<commMessage>(cm::SET_SCHEDULED);
                    gres->getPayload<cm>()->m_actionID =
                        (m->m_actionID > 0) ? m->m_actionID : instructionCounter;
                    commLink->transmit(sourceID, std::move(gres));
                    auto fea = generateGetEvent(m->m_time, sourceID, m);
                    rootSim->add(std::shared_ptr<eventAdapter>(std::move(fea)));
                }
                break;
            case cm::GET_MULTIPLE:
                if (m->m_time <= prevTime + kSmallTime) {
                    if (measureDelay <= kSmallTime) {
                    } else {
                    }
                } else {
                }
                break;
            case cm::GET_PERIODIC:
                break;
            case cm::GET_RESULT_MULTIPLE:
            case cm::SET_SUCCESS:
            case cm::SET_FAIL:
            case cm::GET_RESULT:
            case cm::SET_SCHEDULED:
            case cm::GET_SCHEDULED:
            case cm::CANCEL_FAIL:
            case cm::CANCEL_SUCCESS:
                break;
            case cm::CANCEL:
                actnum = findAction(m->m_actionID);

                if (actnum != kNullLocation) {
                    if ((!actions[actnum].executed) &&
                        (actions[actnum].triggerTime >
                         actionDelay)) {  // can only cancel actions that have not executed and are
                                          // not closer than the actionDelay
                        actions[actnum].executed = true;
                        auto gres = std::make_shared<commMessage>(cm::CANCEL_SUCCESS);
                        gres->getPayload<cm>()->m_actionID = m->m_actionID;
                        commLink->transmit(sourceID, std::move(gres));
                    } else {
                        auto gres = std::make_shared<commMessage>(cm::CANCEL_FAIL);
                        gres->getPayload<cm>()->m_actionID = m->m_actionID;
                        commLink->transmit(sourceID, std::move(gres));
                    }
                } else {
                    auto gres = std::make_shared<commMessage>(cm::CANCEL_FAIL);
                    gres->getPayload<cm>()->m_actionID = m->m_actionID;
                    commLink->transmit(sourceID, std::move(gres));
                }
                break;
        }
    }

    std::string controlRelay::generateCommName()
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

    std::string controlRelay::generateAutoName(int code)
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

    change_code controlRelay::executeAction(index_t actionNum)
    {
        if (!isValidIndex(actionNum, actions)) {
            return change_code::not_triggered;
        }
        auto cact = actions[actionNum];
        if (!cact.executed) {
            cact.executed = true;

            if (cact.measureAction) {
                auto findLoc = findMeasurement(cact.field);
                double val;
                if (findLoc != kNullLocation) {
                    val = getMeasurement(findLoc);
                } else {
                    if (opFlags[link_type_source]) {
                        val = m_sourceObject->get(cact.field + m_terminal_key, cact.unitType);
                        if (val == kNullVal) {
                            val = m_sourceObject->get(cact.field, cact.unitType);
                        }
                    } else {
                        val = m_sourceObject->get(cact.field, cact.unitType);
                    }
                }
                auto gres = std::make_shared<commMessage>(cm::GET_RESULT);
                auto ptr = gres->getPayload<cm>();
                ptr->m_field = cact.field;
                ptr->m_value = val;
                ptr->m_time = prevTime;
                commLink->transmit(cact.sourceID, std::shared_ptr<commMessage>(std::move(gres)));
                return change_code::no_change;
            }

            try {
                std::string field;

                if (opFlags[link_type_sink]) {
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

                if (!opFlags[no_message_reply])  // unless told not to respond return with the
                {
                    auto gres = std::make_shared<commMessage>(cm::SET_SUCCESS);
                    gres->getPayload<cm>()->m_actionID = cact.actionID;
                    commLink->transmit(cact.sourceID, std::move(gres));
                }
                return change_code::parameter_change;
            }
            catch (const std::invalid_argument&) {
                if (!opFlags[no_message_reply])  // unless told not to respond return with the
                {
                    auto gres = std::make_shared<commMessage>(cm::SET_FAIL);
                    gres->getPayload<cm>()->m_actionID = cact.actionID;
                    commLink->transmit(cact.sourceID,
                                       std::shared_ptr<commMessage>(std::move(gres)));
                }
                return change_code::execution_failure;
            }
        }
        return change_code::not_triggered;
    }

    void controlRelay::updateObject(coreObject* obj, object_update_mode mode)
    {
        Relay::updateObject(obj, mode);
        if (opFlags[dyn_initialized]) {
            rootSim = dynamic_cast<gridSimulation*>(getRoot());

            if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
                opFlags.set(link_type_source);
            }
            if (dynamic_cast<Link*>(m_sinkObject) != nullptr) {
                opFlags.set(link_type_sink);
            }
        }
    }

    std::unique_ptr<functionEventAdapter>
        controlRelay::generateGetEvent(coreTime eventTime, std::uint64_t sourceID, cm* message)
    {
        auto act = getFreeAction();
        actions[act].actionID =
            (message->m_actionID > 0) ? message->m_actionID : instructionCounter;
        actions[act].executed = false;
        actions[act].measureAction = true;
        actions[act].sourceID = sourceID;
        actions[act].triggerTime = eventTime;
        gmlc::utilities::makeLowerCase(message->m_field);
        actions[act].field = message->m_field;
        if (!(message->m_units.empty())) {
            actions[act].unitType = units::unit_cast_from_string(message->m_units);
        }
        auto fea =
            std::make_unique<functionEventAdapter>([act, this]() { return executeAction(act); },
                                                   eventTime);
        /** this is so the get event triggers last*/
        fea->setExecutionMode(event_execution_mode::delayed);
        return fea;
    }

    std::unique_ptr<functionEventAdapter>
        controlRelay::generateSetEvent(coreTime eventTime, std::uint64_t sourceID, cm* message)
    {
        auto act = getFreeAction();
        actions[act].actionID =
            (message->m_actionID > 0) ? message->m_actionID : instructionCounter;
        actions[act].executed = false;
        actions[act].measureAction = false;
        actions[act].sourceID = sourceID;
        actions[act].triggerTime = eventTime;
        gmlc::utilities::makeLowerCase(message->m_field);
        actions[act].field = message->m_field;
        actions[act].val = message->m_value;

        if (!message->m_units.empty()) {
            actions[act].unitType = units::unit_cast_from_string(message->m_units);
        }

        auto fea =
            std::make_unique<functionEventAdapter>([act, this]() { return executeAction(act); },
                                                   eventTime);
        return fea;
    }

    index_t controlRelay::findAction(std::uint64_t actionID)
    {
        auto res = std::find_if(actions.begin(), actions.end(), [actionID](const auto& act) {
            return (act.actionID == actionID);
        });
        return (res != actions.end()) ? static_cast<index_t>(res - actions.begin()) : kNullLocation;
    }

    index_t controlRelay::getFreeAction()
    {
        auto asize = static_cast<index_t>(actions.size());
        for (index_t act = 0; act < asize; ++act) {
            if (!actions[act].executed) {
                return act;
            }
        }
        // if we didn't find an open one,  make the actions vector longer and return the new index

        actions.resize((static_cast<size_t>(asize) + 1) * 2);  // double the size
        return asize;
    }
}  // namespace relays
}  // namespace griddyn
