/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/events/reversibleEvent.h"
#include <memory>
#include <string>

namespace griddyn::fmi {
class FmiCoordinator;
/** class to manage the inputs for an FMI configuration in GridDyn*/
class FmiEvent: public events::reversibleEvent {
  public:
    /** enumeration of the event types*/
    enum class FmiEventType {
        PARAMETER,  //!< indicator that the event corresponds to a parameter
        STRING_PARAMETER,  //!< indicator that the event is a string parameter
        INPUT,  //!< indicator that the event corresponds to an input
    };

  private:
    FmiCoordinator* mCoordinator = nullptr;  //!< pointer the coordinator
    FmiEventType mEventType = FmiEventType::INPUT;  //!< the type of the event
  public:
    /** constructor taking name and eventType
@param newName the name of the event
@param type the type of event either input or parameter
*/
    FmiEvent(const std::string& newName, FmiEventType type = FmiEventType::INPUT);
    /** default constructor taking optional eventType
     */
    FmiEvent(FmiEventType type = FmiEventType::INPUT);
    /** event constructor taking an eventInfo structure and root object*/
    FmiEvent(const EventInfo& gdEI, CoreObject* rootObject);

    virtual std::unique_ptr<Event> clone() const override;

    virtual void cloneTo(Event* evnt) const override;
    virtual void set(std::string_view param, double val) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void updateEvent(const EventInfo& gdEI, CoreObject* rootObject) override;

    virtual bool setTarget(CoreObject* gdo, std::string_view var = {}) override;

    virtual void updateObject(CoreObject* gco,
                              object_update_mode mode = object_update_mode::direct) override;
    virtual CoreObject* getOwner() const override;
    friend class FmiCoordinator;

  private:
    /** function to find the fmi coordinator so we can connect to that*/
    void findCoordinator();
};

}  // namespace griddyn::fmi

