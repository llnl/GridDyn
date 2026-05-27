/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#ifndef GRIDDYN_INTERPOLATING_PLAYER_H_
#    define GRIDDYN_INTERPOLATING_PLAYER_H_

// headers
// #include "GridDynSimulation.h"

#    include "Player.h"
#    include <memory>
#    include <string>
namespace griddyn::events {
/** event player allowing a timeSeries of events to occur over numerous time points on a single
 * object and field*/
class InterpolatingPlayer: public Player {
  protected:
    std::string slopeField;  //!< the object field to trigger for a slope input
    double samplePeriod = kBigNum;  //!< the sampling period to update the interpolated value
    double slope = 0.0;  //!< the actual slope to use
    bool useSlopeField =
        false;  //!< flag indicating that the event is actually using the slopefield
  public:
    /** construct with a name*/
    explicit InterpolatingPlayer(const std::string& eventName);
    /** construct with a time and looping period*/
    InterpolatingPlayer(coreTime time0 = 0.0, double loopPeriod = 0.0);
    /** construct from an event Info structure and root object*/
    InterpolatingPlayer(const EventInfo& gdEI, CoreObject* rootObject);
    virtual std::unique_ptr<Event> clone() const override;

    virtual void cloneTo(Event* evnt) const override;

    // virtual void updateEvent(EventInfo &gdEI, CoreObject *rootObject) override;
    virtual ChangeCode trigger() override;
    virtual ChangeCode trigger(coreTime time) override;

    virtual void set(std::string_view param, double val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void setFlag(std::string_view flag, bool val) override;
    virtual std::string to_string() const override;

    // friendly helper functions for sorting
  protected:
    virtual void setNextValue() override;
};
}  // namespace griddyn::events
#endif
