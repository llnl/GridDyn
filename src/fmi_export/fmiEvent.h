/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "griddyn/events/reversibleEvent.h"

#include <memory>
#include <string>

namespace griddyn {
namespace fmi {
    class fmiCoordinator;
    /** class to manage the inputs for an FMI configuration in GridDyn*/
    class fmiEvent: public events::reversibleEvent {
      public:
        /** enumeration of the event types*/
        enum class fmiEventType {
            parameter,  //!< indicator that the event corresponds to a parameter
            string_parameter,  //!< indicator that the event is a string parameter
            input,  //!< indicator that the event corresponds to an input
        };

      private:
        fmiCoordinator* coord = nullptr;  //!< pointer the coordinator
        fmiEventType eventType = fmiEventType::input;  //!< the type of the event
      public:
        /** constructor taking name and eventType
    @param name the name of the event
    @param type the type of event either input or parameter
    */
        fmiEvent(const std::string& newName, fmiEventType type = fmiEventType::input);
        /** default constructor taking optional eventType
         */
        fmiEvent(fmiEventType type = fmiEventType::input);
        /** event constructor taking an eventInfo structure and root object*/
        fmiEvent(const EventInfo& gdEI, coreObject* rootObject);

        virtual std::unique_ptr<Event> clone() const override;

        virtual void cloneTo(Event* evnt) const override;
        virtual void set(const std::string& param, double val) override;
        virtual void set(const std::string& param, const std::string& val) override;

        virtual void updateEvent(const EventInfo& gdEI, coreObject* rootObject) override;

        virtual bool setTarget(coreObject* gdo, const std::string& var = "") override;

        virtual void updateObject(coreObject* gco,
                                  object_update_mode mode = object_update_mode::direct) override;
        virtual coreObject* getOwner() const override;
        friend class fmiCoordinator;

      private:
        /** function to find the fmi coordinator so we can connect to that*/
        void findCoordinator();
    };

}  // namespace fmi
}  // namespace griddyn
