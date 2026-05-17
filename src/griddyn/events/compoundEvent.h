/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Event.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::events {
/** single event allowing multiple changes in multiple events at a single time point */
class compoundEvent: public Event {
  protected:
    stringVec fields;  //!< the vector of fields to modify
    std::vector<double> values;  //!< the vector of values to change to
    std::vector<units::unit> units;  //!< vector of units corresponding to the changes
    std::vector<CoreObject*> targetObjects;  //!< the set of objects to target
  public:
    explicit compoundEvent(const std::string& eventName);
    explicit compoundEvent(coreTime time0 = 0.0);
    compoundEvent(const EventInfo& gdEI, CoreObject* rootObject);
    virtual std::unique_ptr<Event> clone() const override;

    virtual void cloneTo(Event* evnt) const override;

    // virtual void updateEvent(EventInfo &gdEI, CoreObject *rootObject) override;
    virtual change_code trigger() override;
    virtual change_code trigger(coreTime time) override;

    virtual void set(std::string_view param, double val) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void setValue(double val, units::unit newUnits = units::defunit) override;
    virtual void setValue(const std::vector<double>& val);
    virtual std::string to_string() const override;

    virtual bool setTarget(CoreObject* gdo, std::string_view var = {}) override;
    virtual void updateObject(CoreObject* gco,
                              object_update_mode mode = object_update_mode::direct) override;
    virtual CoreObject* getObject() const override;
    virtual void getObjects(std::vector<CoreObject*>& objects) const override;
};
}  // namespace griddyn::events

