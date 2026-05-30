/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ZonalRelay.h"

#include "../comms/Communicator.h"
#include "../comms/relayMessage.h"
#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringConversion.h"
#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
using gmlc::utilities::ensureSizeAtLeast;

ZonalRelay::ZonalRelay(const std::string& objName): Relay(objName)
{
    opFlags.set(continuousFlag);
}

CoreObject* ZonalRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<ZonalRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->mZoneCount = mZoneCount;
    nobj->m_terminal = m_terminal;
    nobj->mZoneLevels = mZoneLevels;
    nobj->mZoneDelays = mZoneDelays;
    nobj->mResetMargin = mResetMargin;
    nobj->mAutoName = mAutoName;
    nobj->mConditionLevel = mConditionLevel;
    return nobj;
}

void ZonalRelay::setFlag(std::string_view flag, bool val)
{
    if (flag == "nondirectional") {
        opFlags.set(nondirectionalFlag, val);
    } else {
        Relay::setFlag(flag, val);
    }
}
/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void ZonalRelay::set(std::string_view param, std::string_view val)
{
    if (param == "levels") {
        auto dvals = gmlc::utilities::str2vector<double>(std::string{val}, kNullVal);
        // check to make sure all the levels are valid
        for (auto level : dvals) {
            if (level < -0.00001) {
                throw(InvalidParameterValue(param));
            }
        }
        Relay::set("zones", static_cast<double>(dvals.size()), units::defunit);
        mZoneLevels = std::move(dvals);
    } else if (param == "delay") {
        auto dvals = gmlc::utilities::str2vector<coreTime>(std::string{val}, negTime);
        if (dvals.size() != mZoneDelays.size()) {
            throw(InvalidParameterValue(param));
        }
        // check to make sure all the values are valid
        for (auto delayValue : dvals) {
            if (delayValue < timeZero) {
                throw(InvalidParameterValue(param));
            }
        }
        mZoneDelays = std::move(dvals);
    } else {
        Relay::set(param, val);
    }
}

void ZonalRelay::set(std::string_view param, double val, units::unit unitType)
{
    auto parseZoneIndex = [](std::string_view parameterName) {
        index_t zoneIndex = 0;
        if ((parameterName.size() == 6) && (std::isdigit(parameterName[5]) != 0)) {
            zoneIndex = static_cast<index_t>(parameterName[5] - '0');
        }
        return zoneIndex;
    };
    auto ensureZoneStorage = [this](count_t requestedZoneCount) {
        const auto zoneLevelSize = static_cast<count_t>(mZoneLevels.size());
        if (requestedZoneCount > zoneLevelSize) {
            for (auto kk = zoneLevelSize; kk < requestedZoneCount; ++kk) {
                if (kk == 0) {
                    mZoneLevels.push_back(0.8);
                    mZoneDelays.push_back(timeZero);
                } else {
                    mZoneLevels.push_back(mZoneLevels[kk - 1] + 0.7);
                    mZoneDelays.push_back(mZoneDelays[kk - 1] + timeOneSecond);
                }
            }
        } else {
            mZoneLevels.resize(requestedZoneCount);
            mZoneDelays.resize(requestedZoneCount);
        }
        mZoneCount = requestedZoneCount;
    };
    if (param == "zones") {
        ensureZoneStorage(static_cast<count_t>(val));
    } else if ((param == "terminal") || (param == "side")) {
        m_terminal = static_cast<index_t>(val);
    } else if ((param == "resetmargin") || (param == "margin")) {
        mResetMargin = val;
    } else if (param == "autoname") {
        mAutoName = static_cast<int>(val);
    } else if (param.starts_with("level")) {
        const index_t zoneIndex = parseZoneIndex(param);
        if (zoneIndex >= mZoneCount) {
            ensureZoneStorage(zoneIndex + 1);
        }
        ensureSizeAtLeast(mZoneLevels, zoneIndex + 1);
        mZoneLevels[zoneIndex] = val;
    } else if (param.starts_with("delay")) {
        const index_t zoneIndex = parseZoneIndex(param);
        if (zoneIndex >= mZoneCount) {
            ensureZoneStorage(zoneIndex + 1);
        }
        ensureSizeAtLeast(mZoneDelays, zoneIndex + 1);
        mZoneDelays[zoneIndex] = val;
    } else {
        Relay::set(param, val, unitType);
    }
}

double ZonalRelay::get(std::string_view param, units::unit unitType) const
{
    double val;
    if (param == "condition") {
        val = kNullVal;
    } else {
        val = Relay::get(param, unitType);
    }
    return val;
}

void ZonalRelay::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    const double baseImpedance = m_sourceObject->get("impedance");
    for (index_t kk = 0; kk < mZoneCount; ++kk) {
        if (opFlags[nondirectionalFlag]) {
            add(std::shared_ptr<Condition>(
                makeCondition("abs(admittance" + std::to_string(m_terminal) + ")",
                              ">=",
                              1.0 / (mZoneLevels[kk] * baseImpedance),
                              m_sourceObject)));
        } else {
            add(std::shared_ptr<Condition>(makeCondition("admittance" + std::to_string(m_terminal),
                                                         ">=",
                                                         1.0 / (mZoneLevels[kk] * baseImpedance),
                                                         m_sourceObject)));
        }
        setResetMargin(kk, mResetMargin * 1.0 / (mZoneLevels[kk] * baseImpedance));
    }

    auto tripEvent = std::make_unique<Event>();
    tripEvent->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
    tripEvent->setValue(1.0);

    add(std::shared_ptr<Event>(std::move(tripEvent)));
    for (index_t kk = 0; kk < mZoneCount; ++kk) {
        setActionTrigger(0, kk, mZoneDelays[kk]);
    }

    if (opFlags[useCommLink]) {
        if (cManager.destName().starts_with("auto")) {
            if (cManager.destName().length() == 6) {
                int code;
                try {
                    code = std::stoi(cManager.destName().substr(5, 1));
                }
                catch (const std::invalid_argument&) {
                    code = 0;
                }

                const std::string newName = generateAutoName(code);
                if (!newName.empty()) {
                    cManager.set("commdestname", newName);
                }
            }
        }
    }
    Relay::dynObjectInitializeA(time0, flags);
}

void ZonalRelay::actionTaken(index_t ActionNum,
                             index_t conditionNum,
                             ChangeCode /*actionReturn*/,
                             coreTime /*actionTime*/)
{
    logging::normal(
        this, "condition {} action {} taken terminal {}", conditionNum, ActionNum, m_terminal);

    if (opFlags[useCommLink]) {
        if (ActionNum == 0) {
            auto relayEvent = std::make_shared<CommMessage>(CommMessage::BREAKER_TRIP_EVENT);
            cManager.send(relayEvent);
        }
    }
    for (index_t kk = conditionNum + 1; kk < mZoneCount; ++kk) {
        setConditionStatus(kk, ConditionStatus::disabled);
    }
    mConditionLevel = std::min(conditionNum, mConditionLevel);
}

void ZonalRelay::conditionTriggered(index_t conditionNum, coreTime /*triggerTime*/)
{
    logging::normal(this, "condition {} triggered terminal {}", conditionNum, m_terminal);
    mConditionLevel = std::min(conditionNum, mConditionLevel);
    if (opFlags[useCommLink]) {
        if (conditionNum > mConditionLevel) {
            return;
        }
        auto relayMessage = std::make_shared<CommMessage>();
        // std::cout << "GridDyn conditionTriggered(), conditionNum = " << conditionNum << '\n';
        if (conditionNum == 0) {
            // std::cout << "GridDyn setting relay message type to LOCAL_FAULT_EVENT" << '\n';
            relayMessage->setMessageType(CommMessage::LOCAL_FAULT_EVENT);
        } else {
            // std::cout << "GridDyn setting relay message type to REMOTE_FAULT_EVENT" << '\n';
            relayMessage->setMessageType(CommMessage::REMOTE_FAULT_EVENT);
        }
        cManager.send(relayMessage);
    }
}

void ZonalRelay::conditionCleared(index_t conditionNum, coreTime /*triggerTime*/)
{
    logging::normal(this, "condition {} cleared terminal {}", conditionNum, m_terminal);
    for (index_t kk = 0; kk < mZoneCount; ++kk) {
        if (getConditionStatus(kk) == ConditionStatus::active) {
            mConditionLevel = kk + 1;
        } else {
            return;
        }
    }
    if (opFlags[useCommLink]) {
        auto relayMessage = std::make_shared<CommMessage>();
        if (conditionNum == 0) {
            relayMessage->setMessageType(CommMessage::LOCAL_FAULT_CLEARED);
        } else {
            relayMessage->setMessageType(CommMessage::REMOTE_FAULT_CLEARED);
        }
        cManager.send(relayMessage);
    }
}

void ZonalRelay::receiveMessage(std::uint64_t /*sourceID*/, std::shared_ptr<CommMessage> message)
{
    switch (message->getMessageType()) {
        case CommMessage::BREAKER_TRIP_COMMAND:
            triggerAction(0);
            break;
        case CommMessage::BREAKER_CLOSE_COMMAND:
            if (m_sinkObject != nullptr) {
                m_sinkObject->set("switch" + std::to_string(m_terminal), 0);
            }
            break;
        case CommMessage::BREAKER_OOS_COMMAND:
            for (index_t kk = 0; kk < mZoneCount; ++kk) {
                setConditionStatus(kk, ConditionStatus::disabled);
            }
            break;
        default: {
            // assert (false);
        }
    }
}

std::string ZonalRelay::generateCommName()
{
    if (mAutoName > 0) {
        std::string newName = generateAutoName(mAutoName);
        if (!newName.empty()) {
            if (newName != getName()) {
                setName(newName);
            }
            return newName;
        }
    }
    return getName();
}

std::string ZonalRelay::generateAutoName(int code)
{
    std::string autoname;
    auto* firstBus = m_sourceObject->getSubObject("bus", 1);
    auto* secondBus = m_sourceObject->getSubObject("bus", 2);

    switch (code) {
        case 1:
            if (m_terminal == 1) {
                autoname = firstBus->getName() + '_' + secondBus->getName();
            } else {
                autoname = secondBus->getName() + '_' + firstBus->getName();
            }
            break;
        case 2:
            if (m_terminal == 1) {
                autoname = std::to_string(firstBus->getUserID()) + '_' +
                    std::to_string(secondBus->getUserID());
            } else {
                autoname = std::to_string(secondBus->getUserID()) + '_' +
                    std::to_string(firstBus->getUserID());
            }
            break;
        default:;
            // do nothing
    }
    // check if there are multiple lines in parallel
    if (!autoname.empty()) {
        auto reverseIter = m_sourceObject->getName().rbegin();
        if (*(reverseIter + 1) == '_') {
            if ((*reverseIter >= 'a') && (*reverseIter <= 'z')) {
                autoname.push_back('_');
                autoname.push_back(*reverseIter);
            }
        }
    }
    return autoname;
}
}  // namespace griddyn::relays
