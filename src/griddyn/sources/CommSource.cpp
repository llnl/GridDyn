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

// NOLINTBEGIN
namespace griddyn::sources {
CommSource::CommSource(const std::string& objName): RampSource(objName)
{
    enableUpdates();
}
CoreObject* CommSource::clone(CoreObject* obj) const
{
    auto cs = cloneBase<CommSource, RampSource>(this, obj);
    if (cs == nullptr) {
        return obj;
    }
    cs->maxRamp = maxRamp;
    return cs;
}

void CommSource::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    rootSim = dynamic_cast<GridSimulation*>(getRoot());
    commLink = cManager.build();

    if (commLink) {
        commLink->initialize();
        commLink->registerReceiveCallback(
            [this](std::uint64_t sourceID, std::shared_ptr<CommMessage> message) {
                receiveMessage(sourceID, message);
            });
    }
    RampSource::pFlowObjectInitializeA(time0, flags);
}

void CommSource::setLevel(double val)
{
    if (opFlags[USE_RAMP]) {
        if (maxRamp > 0) {
            double dt = (val - m_output) / maxRamp;
            if (dt > 0.0001) {
                nextUpdateTime = prevTime + dt;
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

void CommSource::receiveMessage(std::uint64_t sourceID, std::shared_ptr<CommMessage> message)
{
    if (message == nullptr) {
        return;
    }
    auto m = message->getPayload<ControlMessagePayload>();
    if (m == nullptr) {
        return;
    }

    std::shared_ptr<CommMessage> reply;

    switch (message->getMessageType()) {
        case ControlMessagePayload::SET:
            setLevel(m->m_value);

            if (!opFlags[NO_MESSAGE_REPLY])  // unless told not to respond return with the
            {
                reply = std::make_shared<CommMessage>(ControlMessagePayload::SET_SUCCESS);
                auto payload = reply->getPayload<ControlMessagePayload>();
                if (payload != nullptr) {
                    payload->m_actionID = m->m_actionID;
                    commLink->transmit(sourceID, reply);
                }
            }

            break;
        case ControlMessagePayload::GET: {
            reply = std::make_shared<CommMessage>(ControlMessagePayload::GET_RESULT);

            auto rep = reply->getPayload<ControlMessagePayload>();
            if (rep != nullptr) {
                rep->m_field = "level";
                rep->m_value = m_output;
                rep->m_time = prevTime;
                commLink->transmit(sourceID, reply);
            }
        } break;
        case ControlMessagePayload::SET_SUCCESS:
        case ControlMessagePayload::SET_FAIL:
        case ControlMessagePayload::GET_RESULT:
            break;
        case ControlMessagePayload::SET_SCHEDULED:
            if (m->m_time > prevTime) {
                double val = m->m_value;
                auto fea = std::make_shared<FunctionEventAdapter>(
                    [this, val]() {
                        setLevel(val);
                        return ChangeCode::PARAMETER_CHANGE;
                    },
                    m->m_time);
                rootSim->add(fea);
            } else {
                setLevel(m->m_value);

                if (!opFlags[NO_MESSAGE_REPLY])  // unless told not to respond return with the
                {
                    auto gres = std::make_shared<CommMessage>(ControlMessagePayload::SET_SUCCESS);
                    auto payload = gres->getPayload<ControlMessagePayload>();
                    if (payload != nullptr) {
                        payload->m_actionID = m->m_actionID;
                        commLink->transmit(sourceID, gres);
                    }
                }
            }
            break;
        case ControlMessagePayload::GET_SCHEDULED:
        case ControlMessagePayload::CANCEL_FAIL:
        case ControlMessagePayload::CANCEL_SUCCESS:
        case ControlMessagePayload::GET_RESULT_MULTIPLE:
            break;
        case ControlMessagePayload::CANCEL:

            break;
        case ControlMessagePayload::GET_MULTIPLE:
            break;
        case ControlMessagePayload::GET_PERIODIC:
            break;
        default:
            break;
    }
}
}  // namespace griddyn::sources
// NOLINTEND
