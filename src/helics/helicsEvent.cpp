/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsEvent.h"

#include "helics/application_api/Subscriptions.hpp"
#include "helicsCoordinator.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::helicsLib {
helicsEvent::helicsEvent(const std::string& newName): reversibleEvent(newName)
{
    initRequired = true;
}

helicsEvent::helicsEvent(helicsEventType type): eventType(type)
{
    initRequired = true;
}

helicsEvent::helicsEvent(const EventInfo& gdEI, coreObject* rootObject):
    reversibleEvent(gdEI, rootObject)
{
    initRequired = true;
    findCoordinator();
}

std::unique_ptr<Event> helicsEvent::clone() const
{
    std::unique_ptr<Event> evnt = std::make_unique<helicsEvent>();
    cloneTo(evnt.get());
    return evnt;
}

void helicsEvent::cloneTo(Event* evnt) const
{
    reversibleEvent::cloneTo(evnt);
    auto fe = dynamic_cast<helicsEvent*>(evnt);
    if (fe == nullptr) {
        return;
    }
    fe->minDelta = minDelta;
    fe->vectorElement = vectorElement;
    fe->key = key;
}

void helicsEvent::set(std::string_view param, double val)
{
    if ((param == "element") || (param == "vectorelement")) {
        vectorElement = static_cast<int32_t>(val);
    } else if (param == "delta") {
        minDelta = std::abs(val);
    } else {
        reversibleEvent::set(param, val);
    }
}

void helicsEvent::set(std::string_view param, std::string_view val)
{
    if (param == "datatype") {
        if (val == "string") {
            eventType = helicsEventType::string_parameter;
            stringEvent = true;
        } else if ((val == "real") || (val == "number")) {
            eventType = helicsEventType::parameter;
            stringEvent = false;
        } else if (val == "vector") {
            eventType = helicsEventType::parameter;
            stringEvent = false;
            vectorElement = 0;
        }
    } else if (param == "key") {
        key = val;
        if (subid >= 0) {
            coord->updateSubscription(subid, key, unitType);
        } else if (coord != nullptr) {
            subid = coord->addSubscription(key, unitType);
        } else {
            findCoordinator();
        }
    } else if (param == "units") {
        reversibleEvent::set(param, val);
        if (subid >= 0) {
            coord->updateSubscription(subid, key, unitType);
        } else if (coord == nullptr) {
            findCoordinator();
        }
    } else {
        reversibleEvent::set(param, val);
    }
}

void helicsEvent::updateEvent(const EventInfo& gdEI, coreObject* rootObject)
{
    reversibleEvent::updateEvent(gdEI, rootObject);
    findCoordinator();
}

bool helicsEvent::setTarget(coreObject* gdo, const std::string& var)
{
    auto ret = reversibleEvent::setTarget(gdo, var);
    if (ret) {
        findCoordinator();
    }
    return ret;
}

coreObject* helicsEvent::getOwner() const
{
    return coord;
}

void helicsEvent::initialize()
{
    if (coord == nullptr) {
        findCoordinator();
        if (coord == nullptr) {
            return;
        }
    }
    auto sub = coord->getInputPointer(subid);
    if (sub == nullptr) {
        return;
    }
    if (eventType == helicsEventType::string_parameter) {
        sub->setInputNotificationCallback<std::string>(
            [this](const std::string& update, helics::Time /*tval*/) {
                updateStringValue(update);
                trigger();
            });
    } else {
        if (vectorElement >= 0) {
            sub->setInputNotificationCallback<std::vector<double>>(
                [this](const std::vector<double>& update, helics::Time /*tval*/) {
                    if (vectorElement < static_cast<int32_t>(update.size())) {
                        setValue(update[vectorElement]);
                        trigger();
                    }
                });
        } else {
            sub->setInputNotificationCallback<double>([this](double update, helics::Time /*tval*/) {
                setValue(update);
                trigger();
            });
        }
    }
    initRequired = false;
}

void helicsEvent::updateObject(coreObject* gco, object_update_mode mode)
{
    reversibleEvent::updateObject(gco, mode);
    findCoordinator();
}

void helicsEvent::findCoordinator()
{
    if (m_obj) {
        auto rto = m_obj->getRoot();
        if (rto) {
            auto helicsCont = rto->find("helics");
            if (dynamic_cast<helicsCoordinator*>(helicsCont)) {
                if (!isSameObject(helicsCont, coord)) {
                    coord = static_cast<helicsCoordinator*>(helicsCont);
                    if (!key.empty()) {
                        subid = coord->addSubscription(key, unitType);
                    }
                    coord->addEvent(this);
                }
            }
        }
    }
}

}  // namespace griddyn::helicsLib
