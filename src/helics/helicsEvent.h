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
    class helicsCoordinator;

    class helicsEvent: public events::reversibleEvent {
      public:
        /** enumeration of the event types*/
        enum class helicsEventType {
            parameter,  //!< indicator that the event corresponds to a parameter
            string_parameter,  //!< indicator that the event is a string parameter
        };

      private:
        helicsCoordinator* coord = nullptr;  //!< pointer the coordinator
        helicsEventType eventType = helicsEventType::parameter;  //!< the type of the event
        std::string key;  //!< helics subscription key
        int32_t subid = -1;  //!< index of the subscription
        int32_t vectorElement = -1;  // element of a vector to use as the event parameter
        double minDelta = 0.0;  //!< set the minimum delta for the event to trigger
      public:
        helicsEvent(const EventInfo& gdEI, coreObject* rootObject);
        explicit helicsEvent(const std::string& name);
        explicit helicsEvent(helicsEventType type = helicsEventType::parameter);

        virtual std::unique_ptr<Event> clone() const override;
        virtual void cloneTo(Event* col) const override;
        virtual void set(const std::string& param, double val) override;
        virtual void set(const std::string& param, const std::string& val) override;

        virtual void updateEvent(const EventInfo& gdEI, coreObject* rootObject) override;

        virtual bool setTarget(coreObject* gdo, const std::string& var = "") override;

        virtual void updateObject(coreObject* gco,
                                  object_update_mode mode = object_update_mode::direct) override;
        virtual coreObject* getOwner() const override;

        virtual void initialize() override;
        friend class fmiCoordinator;

      private:
        /** function to find the fmi coordinator so we can connect to that*/
        void findCoordinator();
    };

}  // namespace griddyn::helicsLib
