/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Player.h"

#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace griddyn::events {
Player::Player(const std::string& eventName): Event(eventName) {}

Player::Player(coreTime time0, double loopPeriod): Event(time0), period(loopPeriod) {}

Player::Player(const EventInfo& gdEI, coreObject* rootObject):
    Event(gdEI, rootObject), period(gdEI.period)
{
    if (gdEI.file.empty()) {
        setTimeValue(gdEI.time, gdEI.value);
    } else {
        if (!gdEI.columns.empty()) {
            column = gdEI.columns[0];
        }
        loadEventFile(gdEI.file);
    }
}

void Player::updateEvent(const EventInfo& gdEI, coreObject* rootObject)
{
    if (gdEI.file.empty()) {
        setTimeValue(gdEI.time, gdEI.value);
    } else {
        if (!gdEI.columns.empty()) {
            column = gdEI.columns[0];
        }
        loadEventFile(gdEI.file);
    }
    Event::updateEvent(gdEI, rootObject);
}

std::unique_ptr<Event> Player::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<Player>(getName());
    cloneTo(upE.get());
    return upE;
}

void Player::cloneTo(Event* gE) const
{
    Event::cloneTo(gE);
    auto nE = dynamic_cast<Player*>(gE);
    if (nE == nullptr) {
        return;
    }
    nE->period = period;
    nE->eFile = eFile;
    nE->ts = ts;
    nE->column = column;
    nE->currIndex = currIndex;
    nE->timeOffset = timeOffset;
}

void Player::set(std::string_view param, double val)
{
    if (param == "period") {
        value = val;
    } else if (param == "column") {
        column = static_cast<index_t>(val);
    } else if (param == "timeoffset") {
        timeOffset = val;
    } else {
        Event::set(param, val);
    }
}

void Player::set(std::string_view param, std::string_view val)
{
    if (param == "file") {
        loadEventFile(std::string{val});
    } else {
        Event::set(param, val);
    }
}

void Player::setTime(coreTime time)
{
    triggerTime = time;
}

void Player::setTimeValue(coreTime time, double val)
{
    triggerTime = time;
    value = val;
}

void Player::setTimeValue(const std::vector<coreTime>& time, const std::vector<double>& val)
{
    ts.reserve(static_cast<gmlc::utilities::fsize_t>(time.size()));

    ts.addData(time, val);

    currIndex = 0;
    setNextValue();
}

void Player::setNextValue()
{
    if (static_cast<size_t>(currIndex) < ts.size()) {
        triggerTime = ts.time(currIndex) + timeOffset;
        value = ts.data(currIndex);
    }
}

void Player::updateTrigger(coreTime time)
{
    if (time >= triggerTime) {
        if (time >= ts.time(currIndex) + timeOffset) {
            ++currIndex;
        }
    }
    if (static_cast<size_t>(currIndex) >= ts.size()) {
        if (period > timeZero)  // if we have a period loop the time series
        {
            if (time - ts.lastTime() > period) {
                timeOffset = period + triggerTime;
            } else {
                timeOffset += period;
            }

            currIndex = 0;
            setNextValue();
        } else {
            armed = false;
        }
    } else {  // just proceed to the next trigger and Value
        setNextValue();
    }
}

std::string Player::to_string() const
{
    // @time1[,time2,time3,... |+ period] >[rootobj::obj:]field(units) = val1,[val2,val3,...]
    std::stringstream ss;
    if (eFile.empty()) {
        ss << '@' << triggerTime;
        if (period > timeZero) {
            ss << '+' << period << '|';
        } else if (!ts.empty()) {
            for (auto nn = currIndex + 1; nn < static_cast<index_t>(ts.size()); ++nn) {
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
        if (!ts.empty()) {
            for (index_t nn = currIndex + 1; nn < static_cast<index_t>(ts.size()); ++nn) {
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

change_code Player::trigger()
{
    try {
        m_obj->set(field, value, unitType);
        return change_code::parameter_change;
    }
    catch (const std::invalid_argument&) {
        return change_code::execution_failure;
    }
}

change_code Player::trigger(coreTime time)
{
    change_code ret = change_code::not_triggered;
    if (time + kSmallTime >= triggerTime) {
        try {
            m_obj->set(field, value, unitType);
            ret = change_code::parameter_change;
        }
        catch (const std::invalid_argument&) {
            ret = change_code::execution_failure;
        }
        updateTrigger(time);
    }
    return ret;
}

void Player::initialize() {}

bool Player::setTarget(coreObject* gdo, const std::string& var)
{
    if (!var.empty()) {
        field = var;
    }
    m_obj = gdo;

    if (m_obj != nullptr) {
        setName(m_obj->getName());
        armed = true;
    }
    return armed;
}

void Player::loadEventFile(const std::string& fileName)
{
    if (!fileName.empty()) {
        eFile = fileName;
    }

    ts.loadFile(eFile, column);

    currIndex = 0;
    setNextValue();
}
}  // namespace griddyn::events
