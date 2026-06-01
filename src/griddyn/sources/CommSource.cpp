/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CommSource.h"

#include "../GridDynSimulation.h"
#include "../comms/Communicator.h"
#include "../comms/ControlMessage.h"
#include "../events/EventQueue.h"
#include "core/CoreObjectTemplates.hpp"
#include <cassert>
#include <memory>
#include <string>

namespace griddyn::sources {
CommSource::CommSource(const std::string& objName): RampSource(objName)
{
    enableUpdates();
}
CoreObject* CommSource::clone(CoreObject* obj) const
{
    auto* commSourceClone = cloneBase<CommSource, RampSource>(this, obj);
    if (commSourceClone == nullptr) {
        return obj;
    }
    commSourceClone->maxRamp = maxRamp;
    return commSourceClone;
}

void CommSource::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    rootSim = dynamic_cast<GridSimulation*>(getRoot());
    commLink = cManager.build();

    if (commLink) {
        commLink->initialize();
        commLink->registerReceiveCallback(
            [this](std::uint64_t sourceID, const std::shared_ptr<CommMessage>& message) {
                receiveMessage(sourceID, message);
            });
    }
    RampSource::pFlowObjectInitializeA(time0, flags);
}

void CommSource::setLevel(double val)
{
    if (opFlags[USE_RAMP]) {
        if (maxRamp > 0) {
            const double deltaTime = (val - m_output) / maxRamp;
            if (deltaTime > 0.0001) {
                nextUpdateTime = prevTime + deltaTime;
                alert(this, UPDATE_TIME_CHANGE);
                mp_dOdt = (val > m_output) ? maxRamp : -maxRamp;
            } else {
                m_output = val;
                mp_dOdt = 0.0;
            }
        }
    } else {
        m_output = val;
    }
}

void CommSource::set(std::string_view param, std::string_view val)
{
    if (!(cManager.set(param, val))) {
        Source::set(param, val);
    }
}

void CommSource::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "ramp") || (param == "maxramp")) {
        maxRamp = std::abs(val);
    } else {
        if (!(cManager.set(param, val))) {
            Source::set(param, val, unitType);
        }
    }
}

void CommSource::setFlag(std::string_view flag, bool val)
{
    if (flag == "ramp") {
        opFlags.set(USE_RAMP, val);
    } else if (flag == "no_reply_message") {
        opFlags.set(NO_MESSAGE_REPLY, val);
    } else if (flag == "reply_message") {
        opFlags.set(NO_MESSAGE_REPLY, !val);
    } else {
        if (!(cManager.setFlag(flag, val))) {
            Source::setFlag(flag, val);
        }
    }
}

void CommSource::updateA(CoreTime time)
{
    if (time > nextUpdateTime) {
        mp_dOdt = 0;
        nextUpdateTime = maxTime;
    }
}

using ControlMessagePayload = griddyn::comms::ControlMessagePayload;

void CommSource::receiveMessage(std::uint64_t sourceID, const std::shared_ptr<CommMessage>& message)
{
    if (message == nullptr) {
        return;
    }
    auto* controlMessage = message->getPayload<ControlMessagePayload>();
    if (controlMessage == nullptr) {
        return;
    }

    std::shared_ptr<CommMessage> reply;

    switch (message->getMessageType()) {
        case ControlMessagePayload::SET:
            setLevel(controlMessage->m_value);

            if (!opFlags[NO_MESSAGE_REPLY])  // unless told not to respond return with the
            {
                reply = std::make_shared<CommMessage>(ControlMessagePayload::SET_SUCCESS);
                auto* replyPayload = reply->getPayload<ControlMessagePayload>();
                if (replyPayload != nullptr) {
                    replyPayload->m_actionID = controlMessage->m_actionID;
                    commLink->transmit(sourceID, reply);
                }
            }

            break;
        case ControlMessagePayload::GET: {
            reply = std::make_shared<CommMessage>(ControlMessagePayload::GET_RESULT);

            auto* replyPayload = reply->getPayload<ControlMessagePayload>();
            if (replyPayload != nullptr) {
                replyPayload->m_field = "level";
                replyPayload->m_value = m_output;
                replyPayload->m_time = prevTime;
                commLink->transmit(sourceID, reply);
            }
        } break;
        case ControlMessagePayload::SET_SUCCESS:
        case ControlMessagePayload::SET_FAIL:
        case ControlMessagePayload::GET_RESULT:
            break;
        case ControlMessagePayload::SET_SCHEDULED:
            if (controlMessage->m_time > prevTime) {
                const double scheduledValue = controlMessage->m_value;
                auto fea = std::make_shared<FunctionEventAdapter>(
                    [this, scheduledValue]() {
                        setLevel(scheduledValue);
                        return ChangeCode::PARAMETER_CHANGE;
                    },
                    controlMessage->m_time);
                rootSim->add(fea);
            } else {
                setLevel(controlMessage->m_value);

                if (!opFlags[NO_MESSAGE_REPLY])  // unless told not to respond return with the
                {
                    auto gres = std::make_shared<CommMessage>(ControlMessagePayload::SET_SUCCESS);
                    auto* replyPayload = gres->getPayload<ControlMessagePayload>();
                    if (replyPayload != nullptr) {
                        replyPayload->m_actionID = controlMessage->m_actionID;
                        commLink->transmit(sourceID, gres);
                    }
                }
            }
            break;
        case ControlMessagePayload::GET_SCHEDULED:
        case ControlMessagePayload::CANCEL_FAIL:
        case ControlMessagePayload::CANCEL_SUCCESS:
        case ControlMessagePayload::GET_RESULT_MULTIPLE:
        case ControlMessagePayload::CANCEL:
        case ControlMessagePayload::GET_MULTIPLE:
        case ControlMessagePayload::GET_PERIODIC:
            break;
        default:
            break;
    }
}
}  // namespace griddyn::sources
