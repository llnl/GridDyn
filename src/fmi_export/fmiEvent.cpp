/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiEvent.h"

#include "fmiCoordinator.h"
#include <memory>
#include <string>

namespace griddyn::fmi {
fmiEvent::fmiEvent(const std::string& newName, fmiEventType type):
    reversibleEvent(newName), mEventType(type)
{
}

fmiEvent::fmiEvent(fmiEventType type): mEventType(type) {}

fmiEvent::fmiEvent(const EventInfo& gdEI, coreObject* rootObject): reversibleEvent(gdEI, rootObject)
{
    findCoordinator();
}

std::unique_ptr<Event> fmiEvent::clone() const
{
    std::unique_ptr<Event> evnt = std::make_unique<fmiEvent>();
    cloneTo(evnt.get());
    return evnt;
}

void fmiEvent::cloneTo(Event* evnt) const
{
    reversibleEvent::cloneTo(evnt);
    auto fe = dynamic_cast<fmiEvent*>(evnt);
    if (fe == nullptr) {
        return;
    }
    // gp->valueref = valueref;
}

void fmiEvent::set(std::string_view param, double val)
{
    if ((param == "vr") || (param == "valuereference")) {
        // valueref = static_cast<unsigned int>(val);
    } else {
        reversibleEvent::set(param, val);
    }
}

void fmiEvent::set(std::string_view param, std::string_view val)
{
    if (param == "datatype") {
        if (val == "string") {
            mEventType = fmiEventType::string_parameter;
            stringEvent = true;
        } else if ((val == "real") || (val == "number")) {
            mEventType = fmiEventType::parameter;
            stringEvent = false;
        }
    } else {
        reversibleEvent::set(param, val);
    }
}

void fmiEvent::updateEvent(const EventInfo& gdEI, coreObject* rootObject)
{
    reversibleEvent::updateEvent(gdEI, rootObject);
    findCoordinator();
}

bool fmiEvent::setTarget(coreObject* gdo, std::string_view var)
{
    auto ret = reversibleEvent::setTarget(gdo, var);
    if (ret) {
        findCoordinator();
    }
    return ret;
}

coreObject* fmiEvent::getOwner() const
{
    return mCoordinator;
}

void fmiEvent::updateObject(coreObject* gco, object_update_mode mode)
{
    reversibleEvent::updateObject(gco, mode);
    findCoordinator();
}

void fmiEvent::findCoordinator()
{
    if (m_obj != nullptr) {
        auto rto = m_obj->getRoot();
        if (rto != nullptr) {
            auto fmiCont = rto->find("fmiCoordinator");
            if (dynamic_cast<fmiCoordinator*>(fmiCont) != nullptr) {
                if (!isSameObject(fmiCont, mCoordinator)) {
                    mCoordinator = static_cast<fmiCoordinator*>(fmiCont);
                    if (mEventType == fmiEventType::input) {
                        mCoordinator->registerInput(getName(), this);
                    } else {
                        mCoordinator->registerParameter(getName(), this);
                    }
                }
            }
        }
    }
}

}  // namespace griddyn::fmi
