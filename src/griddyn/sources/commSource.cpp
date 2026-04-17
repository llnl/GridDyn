/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "commSource.h"

#include "../comms/Communicator.h"
#include "../comms/controlMessage.h"
#include "../events/eventQueue.h"
#include "../gridDynSimulation.h"
#include "core/coreObjectTemplates.hpp"
#include <cassert>
#include <memory>
#include <string>

namespace griddyn {
namespace sources {
    commSource::commSource(const std::string& objName): rampSource(objName)
    {
        enable_updates();
    }
    coreObject* commSource::clone(coreObject* obj) const
    {
        auto cs = cloneBase<commSource, rampSource>(this, obj);
        if (cs == nullptr) {
            return obj;
        }
        cs->maxRamp = maxRamp;
        return cs;
    }

    void commSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        rootSim = dynamic_cast<gridSimulation*>(getRoot());
        commLink = cManager.build();

        if (commLink) {
            commLink->initialize();
            commLink->registerReceiveCallback(
                [this](std::uint64_t sourceID, std::shared_ptr<commMessage> message) {
                    receiveMessage(sourceID, message);
                });
        }
        rampSource::pFlowObjectInitializeA(time0, flags);
    }

    void commSource::setLevel(double val)
    {
        if (opFlags[useRamp]) {
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

    void commSource::set(const std::string& param, const std::string& val)
    {
        if (!(cManager.set(param, val))) {
            Source::set(param, val);
        }
    }

    void commSource::set(const std::string& param, double val, units::unit unitType)
    {
        if ((param == "ramp") || (param == "maxramp")) {
            maxRamp = std::abs(val);
        } else {
            if (!(cManager.set(param, val))) {
                Source::set(param, val, unitType);
            }
        }
    }

    void commSource::setFlag(const std::string& flag, bool val)
    {
        if (flag == "ramp") {
            opFlags.set(useRamp, val);
        } else if (flag == "no_reply_message") {
            opFlags.set(no_message_reply, val);
        } else if (flag == "reply_message") {
            opFlags.set(no_message_reply, !val);
        } else {
            if (!(cManager.setFlag(flag, val))) {
                Source::setFlag(flag, val);
            }
        }
    }

    void commSource::updateA(coreTime time)
    {
        if (time > nextUpdateTime) {
            mp_dOdt = 0;
            nextUpdateTime = maxTime;
        }
    }

    using controlMessagePayload = griddyn::comms::controlMessagePayload;

    void commSource::receiveMessage(std::uint64_t sourceID, std::shared_ptr<commMessage> message)
    {
        auto m = message->getPayload<controlMessagePayload>();

        std::shared_ptr<commMessage> reply;

        switch (message->getMessageType()) {
            case controlMessagePayload::SET:
                setLevel(m->m_value);

                if (!opFlags[no_message_reply])  // unless told not to respond return with the
                {
                    reply = std::make_shared<commMessage>(controlMessagePayload::SET_SUCCESS);
                    auto payload = reply->getPayload<controlMessagePayload>();
                    if (payload != nullptr) {
                        payload->m_actionID = m->m_actionID;
                        commLink->transmit(sourceID, reply);
                    }
                }

                break;
            case controlMessagePayload::GET: {
                reply = std::make_shared<commMessage>(controlMessagePayload::GET_RESULT);

                auto rep = reply->getPayload<controlMessagePayload>();
                rep->m_field = "level";
                rep->m_value = m_output;
                rep->m_time = prevTime;
                commLink->transmit(sourceID, reply);
            } break;
            case controlMessagePayload::SET_SUCCESS:
            case controlMessagePayload::SET_FAIL:
            case controlMessagePayload::GET_RESULT:
                break;
            case controlMessagePayload::SET_SCHEDULED:
                if (m->m_time > prevTime) {
                    double val = m->m_value;
                    auto fea = std::make_shared<functionEventAdapter>(
                        [this, val]() {
                            setLevel(val);
                            return change_code::parameter_change;
                        },
                        m->m_time);
                    rootSim->add(fea);
                } else {
                    setLevel(m->m_value);

                    if (!opFlags[no_message_reply])  // unless told not to respond return with the
                    {
                        auto gres =
                            std::make_shared<commMessage>(controlMessagePayload::SET_SUCCESS);
                        auto payload = reply->getPayload<controlMessagePayload>();
                        if (payload != nullptr) {
                            payload->m_actionID = m->m_actionID;
                            commLink->transmit(sourceID, gres);
                        }
                    }
                }
                break;
            case controlMessagePayload::GET_SCHEDULED:
            case controlMessagePayload::CANCEL_FAIL:
            case controlMessagePayload::CANCEL_SUCCESS:
            case controlMessagePayload::GET_RESULT_MULTIPLE:
                break;
            case controlMessagePayload::CANCEL:

                break;
            case controlMessagePayload::GET_MULTIPLE:
                break;
            case controlMessagePayload::GET_PERIODIC:
                break;
            default:
                break;
        }
    }
}  // namespace sources
}  // namespace griddyn
