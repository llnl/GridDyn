/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/events/reversibleEvent.h"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::helicsLib {
class HelicsCoordinator;

class HelicsEvent: public events::reversibleEvent {
  public:
    /** enumeration of the event types*/
    enum class HelicsEventType {
        PARAMETER,  //!< indicator that the event corresponds to a parameter
        STRING_PARAMETER,  //!< indicator that the event is a string parameter
    };

  private:
    HelicsCoordinator* coordinator_ = nullptr;  //!< pointer the coordinator
    HelicsEventType eventType = HelicsEventType::PARAMETER;  //!< the type of the event
    std::string subscriptionKey;  //!< helics subscription key
    int32_t subscriptionId = -1;  //!< index of the subscription
    int32_t vectorElementIndex = -1;  // element of a vector to use as the event parameter
    double minimumDelta = 0.0;  //!< set the minimum delta for the event to trigger
  public:
    HelicsEvent(const EventInfo& gdEI, CoreObject* rootObject);
    explicit HelicsEvent(const std::string& name);
    explicit HelicsEvent(HelicsEventType type = HelicsEventType::PARAMETER);

    virtual std::unique_ptr<Event> clone() const override;
    virtual void cloneTo(Event* col) const override;
    virtual void set(std::string_view param, double val) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void updateEvent(const EventInfo& gdEI, CoreObject* rootObject) override;

    virtual bool setTarget(CoreObject* gdo, std::string_view var = {}) override;

    virtual void updateObject(CoreObject* gco,
                              object_update_mode mode = object_update_mode::direct) override;
    virtual CoreObject* getOwner() const override;

    virtual void initialize() override;
    friend class fmiCoordinator;

  private:
    /** function to find the fmi coordinator so we can connect to that*/
    void findCoordinator();
};

}  // namespace griddyn::helicsLib
