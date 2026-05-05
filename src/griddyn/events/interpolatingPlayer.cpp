/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "interpolatingPlayer.h"

#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace griddyn::events {
interpolatingPlayer::interpolatingPlayer(const std::string& eventName): Player(eventName) {}

interpolatingPlayer::interpolatingPlayer(coreTime time0, double loopPeriod):
    Player(time0, loopPeriod)
{
}

interpolatingPlayer::interpolatingPlayer(const EventInfo& gdEI, coreObject* rootObject):
    Player(gdEI, rootObject)
{
}

std::unique_ptr<Event> interpolatingPlayer::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<interpolatingPlayer>(getName());
    interpolatingPlayer::cloneTo(upE.get());
    return upE;
}

void interpolatingPlayer::cloneTo(Event* gE) const
{
    Player::cloneTo(gE);
    auto nE = dynamic_cast<interpolatingPlayer*>(gE);
    if (nE == nullptr) {
        return;
    }
    nE->slopeField = slopeField;
    nE->useSlopeField = useSlopeField;
}

void interpolatingPlayer::set(std::string_view param, double val)
{
    if (param == "sampleperiod") {
        samplePeriod = val;
    } else if (param == "samplerate") {
        if (val > 0) {
            samplePeriod = 1.0 / val;
        } else {
            throw(invalidParameterValue(param));
        }
    } else {
        Player::set(param, val);
    }
}

void interpolatingPlayer::set(std::string_view param, std::string_view val)
{
    if (param == "slopefield") {
        slopeField = val;
        useSlopeField = true;
        setNextValue();
    } else {
        Player::set(param, val);
    }
}

void interpolatingPlayer::setFlag(std::string_view flag, bool val)
{
    if (flag == "useslopefield") {
        useSlopeField = val;
        setNextValue();
    } else {
        Player::setFlag(flag, val);
    }
}
void interpolatingPlayer::setNextValue()
{
    if (static_cast<index_t>(ts.size()) <= currIndex) {
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

std::string interpolatingPlayer::to_string() const
{
    // @time1[,time2,time3,... |+ period] >[rootobj::obj:]field(units) = val1,[val2,val3,...]
    std::stringstream ss;
    auto Npts = static_cast<index_t>(ts.size());
    if (eFile.empty()) {
        ss << '@' << triggerTime;
        if (period > timeZero) {
            ss << '+' << period << '|';
        } else if (Npts > 0) {
            for (index_t nn = currIndex + 1; nn < Npts; ++nn) {
                ss << ", " << ts.time(nn);
            }
            ss << "| ";
        } else {
            ss << " | ";
        }
        ss << fullObjectName(m_obj) << ':' << field;
        if (unitType != units::defunit) {
            ss << '(' << units::to_string(unitType) << ')';
        }
        ss << " = " << value;
        if (Npts > 0) {
            for (auto nn = currIndex + 1; nn < Npts; ++nn) {
                ss << ", " << ts.data(nn);
            }
        }
    } else {
        ss << fullObjectName(m_obj) << ':' << field;
        if (unitType != units::defunit) {
            ss << '(' << units::to_string(unitType) << ')';
        }
        ss << " = {" << eFile;
        if (column > 0) {
            ss << '#' << column;
        }
        ss << '}';
        if (period > timeZero) {
            ss << '+' << period;
        }
    }
    return ss.str();
}

change_code interpolatingPlayer::trigger()
{
    try {
        m_obj->set(field, value, unitType);
        return change_code::parameter_change;
    }
    catch (const std::invalid_argument&) {
        return change_code::execution_failure;
    }
}

change_code interpolatingPlayer::trigger(coreTime time)
{
    change_code ret = change_code::not_triggered;
    if (time + kSmallTime >= triggerTime) {
        try {
            if (useSlopeField) {
                m_obj->set(field, value, unitType);
                m_obj->set(slopeField, slope, unitType);
            } else {
                m_obj->set(field, value, unitType);
            }

            ret = change_code::parameter_change;
        }
        catch (const std::invalid_argument&) {
            ret = change_code::execution_failure;
        }
        updateTrigger(time);
    }
    return ret;
}
}  // namespace griddyn::events
