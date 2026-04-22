/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compoundEventPlayer.h"

#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace griddyn::events {
compoundEventPlayer::compoundEventPlayer() = default;

compoundEventPlayer::compoundEventPlayer(const std::string& eventName): compoundEvent(eventName) {}
compoundEventPlayer::compoundEventPlayer(EventInfo& gdEI, coreObject* rootObject):
    compoundEvent(gdEI, rootObject)
{
}

std::unique_ptr<Event> compoundEventPlayer::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<compoundEventPlayer>(getName());
    cloneTo(upE.get());
    return upE;
}

void compoundEventPlayer::cloneTo(Event* gE) const
{
    compoundEvent::cloneTo(gE);
    auto nE = dynamic_cast<compoundEventPlayer*>(gE);
    if (nE == nullptr) {
        return;
    }
}

void compoundEventPlayer::setTime(coreTime time)
{
    triggerTime = time;
}

void compoundEventPlayer::setTimeValue(coreTime time, double val)
{
    triggerTime = time;
    value = val;
}

void compoundEventPlayer::setTimeValue(const std::vector<coreTime>& /* times */,
                                       const std::vector<double>& /* vals */)
{
}

void compoundEventPlayer::set(std::string_view param, double val)
{
    if (param[0] == '#') {
    } else {
        compoundEvent::set(param, val);
    }
}

void compoundEventPlayer::set(std::string_view param, std::string_view val)
{
    if (param[0] == '#') {
    } else {
        compoundEvent::set(param, val);
    }
}
void compoundEventPlayer::updateTrigger(coreTime time)
{
    if (currIndex != kNullLocation)  // we have a file operation
    {
        currIndex++;
        auto Npts = static_cast<index_t>(ts.size());
        if (currIndex >= Npts) {
            if (period > timeZero)  // if we have a period loop the time series
            {
                if (time - ts.time(currIndex) > period) {
                    for (index_t kk = 0; kk < Npts; ++kk) {
                        ts.time(kk) += period + triggerTime;
                    }
                } else {
                    for (index_t kk = 0; kk < Npts; ++kk) {
                        ts.time(kk) += period;
                    }
                }

                currIndex = 0;
                triggerTime = ts.time(currIndex);
            } else {
                armed = false;
            }
        } else {
            // just proceed to the next trigger and Value
            triggerTime = ts.time(currIndex);
        }
    } else {
        // no file so loop if there is a period otherwise disarm
        if (period > timeZero) {
            do {
                triggerTime = triggerTime + period;
            } while (time >= triggerTime);
        } else {
            armed = false;
        }
    }
}

void compoundEventPlayer::initialize() {}

std::string compoundEventPlayer::to_string() const
{
    // @time1[,time2,time3,... |+ period] >[rootobj::obj:]field(units) = val1,[val2,val3,...]
    std::stringstream ss;
    auto Ntargets = static_cast<index_t>(ts.size());
    if (eFile.empty()) {
        ss << '@' << triggerTime;
        if (period > timeZero) {
            ss << '+' << period << '|';
        } else if (Ntargets > 0) {
            for (auto nn = currIndex + 1; nn < Ntargets; ++nn) {
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
        if (Ntargets > 0) {
            for (index_t nn = currIndex + 1; nn < Ntargets; ++nn) {
                // ss << ", " << ts.data[nn];
            }
        }
    } else {
        ss << fullObjectName(m_obj) << ':' << field;
        if (unitType != units::defunit) {
            ss << '(' << units::to_string(unitType) << ')';
        }
        ss << " = <" << eFile;

        ss << '>';
    }
    return ss.str();
}

change_code compoundEventPlayer::trigger()
{
    try {
        m_obj->set(field, value, unitType);
        return change_code::parameter_change;
    }
    catch (const std::invalid_argument&) {
        return change_code::execution_failure;
    }
}

change_code compoundEventPlayer::trigger(coreTime time)
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

bool compoundEventPlayer::setTarget(coreObject* gdo, const std::string& var)
{
    if (!var.empty()) {
        field = var;
    }
    m_obj = gdo;

    if (m_obj != nullptr) {
        setName(m_obj->getName());
    }
    armed = checkArmed();
    return armed;
}

void compoundEventPlayer::loadEventFile(const std::string& fileName)
{
    eFile = fileName;
    ts.loadFile(eFile);

    currIndex = 0;
    if (!ts.empty()) {
        triggerTime = ts.time(0);
    }
}
}  // namespace griddyn::events

