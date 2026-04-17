/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "Event.h"
#include <memory>
#include <string>

namespace griddyn {
class gridGrabber;
namespace events {
    /** an event that allows undoing,  it is a grabber and event rolled into one */
    class reversibleEvent: public Event {
      protected:
        bool canUndo = false;  //!< flag indicating that the event is capable of undoing
        bool hasUndo = false;  //!< flag indicating that the event has an undo value
        bool stringEvent = false;  //!< flag indicating that the event corresponds to a string set
        double undoValue = kNullVal;  //!< the value before the event took place
        std::unique_ptr<gridGrabber> ggrab;  //!< a grabber to get the values
        std::string undoString;  //!< the previous value for a string
        std::string newStringValue;  //!< the new value to set for a string
      public:
        explicit reversibleEvent(const std::string& eventName);
        explicit reversibleEvent(coreTime time0 = 0.0);
        reversibleEvent(const EventInfo& gdEI, coreObject* rootObject);
        virtual void updateEvent(const EventInfo& gdEI, coreObject* rootObject) override;
        virtual ~reversibleEvent();
        virtual std::unique_ptr<Event> clone() const override;

        virtual void cloneTo(Event* evnt) const override;

        // virtual void updateEvent(EventInfo &gdEI, coreObject *rootObject) override;
        virtual change_code trigger() override;
        virtual change_code trigger(coreTime time) override;

        virtual bool setTarget(coreObject* gdo, const std::string& var = "") override;

        virtual void updateStringValue(const std::string& newStr);

        virtual void updateObject(coreObject* gco,
                                  object_update_mode mode = object_update_mode::direct) override;
        virtual change_code undo();
        virtual double query();
    };
}  // namespace events
}  // namespace griddyn
