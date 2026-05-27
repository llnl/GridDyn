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
FmiEvent::FmiEvent(const std::string& newName, FmiEventType type):
    ReversibleEvent(newName), mEventType(type)
{
}

FmiEvent::FmiEvent(FmiEventType type): mEventType(type) {}

FmiEvent::FmiEvent(const EventInfo& gdEI, CoreObject* rootObject): ReversibleEvent(gdEI, rootObject)
{
    findCoordinator();
}

std::unique_ptr<Event> FmiEvent::clone() const
{
    std::unique_ptr<Event> evnt = std::make_unique<FmiEvent>();
    cloneTo(evnt.get());
    return evnt;
}

void FmiEvent::cloneTo(Event* evnt) const
{
    ReversibleEvent::cloneTo(evnt);
    auto* fmiEventClone = dynamic_cast<FmiEvent*>(evnt);
    if (fmiEventClone == nullptr) {
        return;
    }
    // gp->valueref = valueref;
}

void FmiEvent::set(std::string_view param, double val)
{
    if ((param == "vr") || (param == "valuereference")) {
        // valueref = static_cast<unsigned int>(val);
    } else {
        ReversibleEvent::set(param, val);
    }
}

void FmiEvent::set(std::string_view param, std::string_view val)
{
    if (param == "datatype") {
        if (val == "string") {
            mEventType = FmiEventType::STRING_PARAMETER;
            stringEvent = true;
        } else if ((val == "real") || (val == "number")) {
            mEventType = FmiEventType::PARAMETER;
            stringEvent = false;
        }
    } else {
        ReversibleEvent::set(param, val);
    }
}

void FmiEvent::updateEvent(const EventInfo& gdEI, CoreObject* rootObject)
{
    ReversibleEvent::updateEvent(gdEI, rootObject);
    findCoordinator();
}

bool FmiEvent::setTarget(CoreObject* gdo, std::string_view var)
{
    auto ret = ReversibleEvent::setTarget(gdo, var);
    if (ret) {
        findCoordinator();
    }
    return ret;
}

CoreObject* FmiEvent::getOwner() const
{
    return mCoordinator;
}

void FmiEvent::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    ReversibleEvent::updateObject(gco, mode);
    findCoordinator();
}

void FmiEvent::findCoordinator()
{
    if (m_obj != nullptr) {
        auto* rootObject = m_obj->getRoot();
        if (rootObject != nullptr) {
            auto* fmiCoordinatorObject = rootObject->find("fmiCoordinator");
            if (dynamic_cast<FmiCoordinator*>(fmiCoordinatorObject) != nullptr) {
                if (!isSameObject(fmiCoordinatorObject, mCoordinator)) {
                    mCoordinator = static_cast<FmiCoordinator*>(fmiCoordinatorObject);
                    if (mEventType == FmiEventType::INPUT) {
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
