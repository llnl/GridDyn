/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "InterpolatingPlayer.h"

#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace griddyn::events {
InterpolatingPlayer::InterpolatingPlayer(const std::string& eventName): Player(eventName) {}

InterpolatingPlayer::InterpolatingPlayer(CoreTime time0, double loopPeriod):
    Player(time0, loopPeriod)
{
}

InterpolatingPlayer::InterpolatingPlayer(const EventInfo& gdEI, CoreObject* rootObject):
    Player(gdEI, rootObject)
{
}

std::unique_ptr<Event> InterpolatingPlayer::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<InterpolatingPlayer>(getName());
    InterpolatingPlayer::cloneTo(upE.get());
    return upE;
}

void InterpolatingPlayer::cloneTo(Event* event) const
{
    Player::cloneTo(event);
    auto* interpolatingPlayer = dynamic_cast<InterpolatingPlayer*>(event);
    if (interpolatingPlayer == nullptr) {
        return;
    }
    interpolatingPlayer->slopeField = slopeField;
    interpolatingPlayer->samplePeriod = samplePeriod;
    interpolatingPlayer->slope = slope;
    interpolatingPlayer->useSlopeField = useSlopeField;
}

void InterpolatingPlayer::set(std::string_view param, double val)
{
    if (param == "sampleperiod") {
        samplePeriod = val;
    } else if (param == "samplerate") {
        if (val > 0) {
            samplePeriod = 1.0 / val;
        } else {
            throw(InvalidParameterValue(param));
        }
    } else {
        Player::set(param, val);
    }
}

void InterpolatingPlayer::set(std::string_view param, std::string_view val)
{
    if (param == "slopefield") {
        slopeField = val;
        useSlopeField = true;
        setNextValue();
    } else {
        Player::set(param, val);
    }
}

void InterpolatingPlayer::setFlag(std::string_view flag, bool val)
{
    if (flag == "useslopefield") {
        useSlopeField = val;
        setNextValue();
    } else {
        Player::setFlag(flag, val);
    }
}
void InterpolatingPlayer::setNextValue()
{
    if (std::cmp_less_equal(ts.size(), currIndex)) {
        return;
    }
    if (currIndex >= static_cast<index_t>(ts.size()) - 1) {
        if (period > timeZero) {
            slope = (ts.data(0) - ts.lastData()) / (ts.time(0) - ts.lastTime() - period);
        } else {
            slope = 0;
        }
    } else {
        slope = (ts.data(currIndex + 1) - ts.data(currIndex)) /
            (ts.time(currIndex + 1) - ts.time(currIndex));
    }

    if (useSlopeField) {
        value = ts.data(currIndex);
        triggerTime = ts.time(currIndex);
    } else {
        triggerTime = std::min(ts.time(currIndex), triggerTime + samplePeriod);
        if (currIndex == 0) {
            if (period > timeZero) {
                value = ts.lastData() + slope * (triggerTime - ts.lastTime());
            } else {
                value = ts.data(currIndex);
            }
        } else {
            value = ts.data(currIndex - 1) + slope * (triggerTime - ts.time(currIndex - 1));
        }
    }
}

std::string InterpolatingPlayer::to_string() const
{
    // @time1[,time2,time3,... |+ period] >[rootobj::obj:]field(units) = val1,[val2,val3,...]
    std::stringstream output;
    auto pointCount = static_cast<index_t>(ts.size());
    if (eFile.empty()) {
        output << '@' << triggerTime;
        if (period > timeZero) {
            output << '+' << period << '|';
        } else if (pointCount > 0) {
            for (index_t nn = currIndex + 1; nn < pointCount; ++nn) {
                output << ", " << ts.time(nn);
            }
            output << "| ";
        } else {
            output << " | ";
        }
        output << fullObjectName(m_obj) << ':' << field;
        if (unitType != units::defunit) {
            output << '(' << units::to_string(unitType) << ')';
        }
        output << " = " << value;
        if (pointCount > 0) {
            for (auto nn = currIndex + 1; nn < pointCount; ++nn) {
                output << ", " << ts.data(nn);
            }
        }
    } else {
        output << fullObjectName(m_obj) << ':' << field;
        if (unitType != units::defunit) {
            output << '(' << units::to_string(unitType) << ')';
        }
        output << " = {" << eFile;
        if (column > 0) {
            output << '#' << column;
        }
        output << '}';
        if (period > timeZero) {
            output << '+' << period;
        }
    }
    return output.str();
}

ChangeCode InterpolatingPlayer::trigger()
{
    try {
        m_obj->set(field, value, unitType);
        return ChangeCode::PARAMETER_CHANGE;
    }
    catch (const std::invalid_argument&) {
        return ChangeCode::EXECUTION_FAILURE;
    }
}

ChangeCode InterpolatingPlayer::trigger(CoreTime time)
{
    ChangeCode ret = ChangeCode::NOT_TRIGGERED;
    if (time + kSmallTime >= triggerTime) {
        try {
            if (useSlopeField) {
                m_obj->set(field, value, unitType);
                m_obj->set(slopeField, slope, unitType);
            } else {
                m_obj->set(field, value, unitType);
            }

            ret = ChangeCode::PARAMETER_CHANGE;
        }
        catch (const std::invalid_argument&) {
            ret = ChangeCode::EXECUTION_FAILURE;
        }
        updateTrigger(time);
    }
    return ret;
}
}  // namespace griddyn::events
