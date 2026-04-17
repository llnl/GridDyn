/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GRIDDYN_COMPOUND_EVENT_H_
#define GRIDDYN_COMPOUND_EVENT_H_
#pragma once

#include "Event.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
namespace events {
    /** single event allowing multiple changes in multiple events at a single time point */
    class compoundEvent: public Event {
      protected:
        stringVec fields;  //!< the vector of fields to modify
        std::vector<double> values;  //!< the vector of values to change to
        std::vector<units::unit> units;  //!< vector of units corresponding to the changes
        std::vector<coreObject*> targetObjects;  //!< the set of objects to target
      public:
        explicit compoundEvent(const std::string& eventName);
        explicit compoundEvent(coreTime time0 = 0.0);
        compoundEvent(const EventInfo& gdEI, coreObject* rootObject);
        virtual std::unique_ptr<Event> clone() const override;

        virtual void cloneTo(Event* evnt) const override;

        // virtual void updateEvent(EventInfo &gdEI, coreObject *rootObject) override;
        virtual change_code trigger() override;
        virtual change_code trigger(coreTime time) override;

        virtual void set(const std::string& param, double val) override;
        virtual void set(const std::string& param, const std::string& val) override;

        virtual void setValue(double val, units::unit newUnits = units::defunit) override;
        virtual void setValue(const std::vector<double>& val);
        virtual std::string to_string() const override;

        virtual bool setTarget(coreObject* gdo, const std::string& var = "") override;
        virtual void updateObject(coreObject* gco,
                                  object_update_mode mode = object_update_mode::direct) override;
        virtual coreObject* getObject() const override;
        virtual void getObjects(std::vector<coreObject*>& objects) const override;
    };
}  // namespace events
}  // namespace griddyn
#endif
