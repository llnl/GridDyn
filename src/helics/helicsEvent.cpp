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
HelicsEvent::HelicsEvent(const std::string& newName): reversibleEvent(newName)
{
    initRequired = true;
}

HelicsEvent::HelicsEvent(HelicsEventType type): eventType(type)
{
    initRequired = true;
}

HelicsEvent::HelicsEvent(const EventInfo& gdEI, CoreObject* rootObject):
    reversibleEvent(gdEI, rootObject)
{
    initRequired = true;
    findCoordinator();
}

std::unique_ptr<Event> HelicsEvent::clone() const
{
    std::unique_ptr<Event> evnt = std::make_unique<HelicsEvent>();
    cloneTo(evnt.get());
    return evnt;
}

void HelicsEvent::cloneTo(Event* evnt) const
{
    reversibleEvent::cloneTo(evnt);
    auto fe = dynamic_cast<HelicsEvent*>(evnt);
    if (fe == nullptr) {
        return;
    }
    fe->minimumDelta = minimumDelta;
    fe->vectorElementIndex = vectorElementIndex;
    fe->subscriptionKey = subscriptionKey;
}

void HelicsEvent::set(std::string_view param, double val)
{
    if ((param == "element") || (param == "vectorelement")) {
        vectorElementIndex = static_cast<int32_t>(val);
    } else if (param == "delta") {
        minimumDelta = std::abs(val);
    } else {
        reversibleEvent::set(param, val);
    }
}

void HelicsEvent::set(std::string_view param, std::string_view val)
{
    if (param == "datatype") {
        if (val == "string") {
            eventType = HelicsEventType::STRING_PARAMETER;
            stringEvent = true;
        } else if ((val == "real") || (val == "number")) {
            eventType = HelicsEventType::PARAMETER;
            stringEvent = false;
        } else if (val == "vector") {
            eventType = HelicsEventType::PARAMETER;
            stringEvent = false;
            vectorElementIndex = 0;
        }
    } else if (param == "key") {
        subscriptionKey = val;
        if (subscriptionId >= 0) {
            coordinator_->updateSubscription(subscriptionId, subscriptionKey, unitType);
        } else if (coordinator_ != nullptr) {
            subscriptionId = coordinator_->addSubscription(subscriptionKey, unitType);
        } else {
            findCoordinator();
        }
    } else if (param == "units") {
        reversibleEvent::set(param, val);
        if (subscriptionId >= 0) {
            coordinator_->updateSubscription(subscriptionId, subscriptionKey, unitType);
        } else if (coordinator_ == nullptr) {
            findCoordinator();
        }
    } else {
        reversibleEvent::set(param, val);
    }
}

void HelicsEvent::updateEvent(const EventInfo& gdEI, CoreObject* rootObject)
{
    reversibleEvent::updateEvent(gdEI, rootObject);
    findCoordinator();
}

bool HelicsEvent::setTarget(CoreObject* gdo, std::string_view var)
{
    auto ret = reversibleEvent::setTarget(gdo, var);
    if (ret) {
        findCoordinator();
    }
    return ret;
}

CoreObject* HelicsEvent::getOwner() const
{
    return coordinator_;
}

void HelicsEvent::initialize()
{
    if (coordinator_ == nullptr) {
        findCoordinator();
        if (coordinator_ == nullptr) {
            return;
        }
    }
    auto sub = coordinator_->getInputPointer(subscriptionId);
    if (sub == nullptr) {
        return;
    }
    if (eventType == HelicsEventType::STRING_PARAMETER) {
        sub->setInputNotificationCallback<std::string>(
            [this](const std::string& update, helics::Time /*tval*/) {
                updateStringValue(update);
                trigger();
            });
    } else {
        if (vectorElementIndex >= 0) {
            sub->setInputNotificationCallback<std::vector<double>>(
                [this](const std::vector<double>& update, helics::Time /*tval*/) {
                    if (vectorElementIndex < static_cast<int32_t>(update.size())) {
                        setValue(update[vectorElementIndex]);
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

void HelicsEvent::updateObject(CoreObject* gco, object_update_mode mode)
{
    reversibleEvent::updateObject(gco, mode);
    findCoordinator();
}

void HelicsEvent::findCoordinator()
{
    if (m_obj) {
        auto rto = m_obj->getRoot();
        if (rto) {
            auto helicsCont = rto->find("helics");
            if (dynamic_cast<HelicsCoordinator*>(helicsCont)) {
                if (!isSameObject(helicsCont, coordinator_)) {
                    coordinator_ = static_cast<HelicsCoordinator*>(helicsCont);
                    if (!subscriptionKey.empty()) {
                        subscriptionId = coordinator_->addSubscription(subscriptionKey, unitType);
                    }
                    coordinator_->addEvent(this);
                }
            }
        }
    }
}

}  // namespace griddyn::helicsLib
