/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "eventAdapters.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace griddyn {
class CoreObject;

/** @brief class implementing a discrete event queue for a continuous time simulation
 the time check on events includes a tolerance to allow for numerical error in the execution of
events the event queue works with event adapters which allow for two part execution of some events
including a potential delay between parts A and B the class also includes a null event which does
nothing but can be called periodically.
*/
class eventQueue {
  private:
    coreTime timeTols = kSmallTime;  //!< the temporal tolerance on events
    std::vector<std::shared_ptr<eventAdapter>> events;  //!< storage location for events
    std::vector<std::shared_ptr<eventAdapter>>
        partB_list;  //!< container for immediate events awaiting part B execution
    std::shared_ptr<eventAdapter>
        nullEvent;  //!< nullEvent operation for scheduling of the null event
    mutable std::mutex
        queuelock_;  //!< a mutex to protect the queue in case of multi-threaded event insertions
  public:
    /** @brief constructor*/
    eventQueue();

    /** @brief virtual destructor*/
    virtual ~eventQueue();

    /** @brief insert an eventAdapter into the queue
    take as an input a shared pointer to an object that implements an event interface and makes an
  eventAdapter out of it
  @arg newEvent  a shared pointer to the eventAdapter object
  @return the event ID of the event adapter
  */
    auto insert(std::shared_ptr<eventAdapter> newEvent)
    {
        auto evID = newEvent->eventID;
        std::lock_guard<std::mutex> lock(queuelock_);
        events.push_back(std::move(newEvent));
        std::sort(events.begin(), events.end(), compareEventAdapters);
        checkDuplicates();
        return evID;
    }

    /** @brief insert an event into the queue
   take as an input some object that implements an event interface and makes an eventAdapter out of
  it
  @tparam X X is a subclass of an eventInterface object
  @arg newEventObject  a pointer to the event object being inserted
  @return the event ID of the newly created event adapter
  */
    template<class X>
    auto insert(X* newEventObject)
    {
        auto ev =
            std::shared_ptr<eventAdapter>(std::make_unique<eventTypeAdapter<X>>(newEventObject));
        return insert(std::move(ev));
    }

    /** @brief insert an event into the queue
     take as an input a shared pointer to an object that implements an event interface and makes an
  eventAdapter out of it
  @tparam X X is a subclass of an eventInterface object
  @arg newEventObject  a pointer to the event object being inserted
  @return the event ID of the newly created event adapter
  */
    template<class X>
    auto insert(std::shared_ptr<X> newEventObject)
    {
        auto ev = std::shared_ptr<eventAdapter>(
            std::make_unique<eventTypeAdapter<std::shared_ptr<X>>>(std::move(newEventObject)));
        return insert(std::move(ev));
    }

    /** @brief get the next event time
        @return the next Event time
        */
    virtual coreTime getNextTime() const;
    /** @brief get the next event time of a specific type of event
  @param[in] eventCode a specific code corresponding to a specific type of event
  @return the next Event time
  */
    virtual coreTime getNextTime(int eventCode) const;

    /** @brief clone the entire queue to a new queue
  @return a unique_ptr to the updated Queue
  */
    virtual std::unique_ptr<eventQueue> clone() const;

    /** @brief clone the entire queue to a different queue
  @param eq the eventQueue to copy the data into
  */
    virtual void cloneTo(eventQueue* eq) const;
    /** @brief map all objects used in the events to a new root object
     */
    virtual void mapObjectsOnto(CoreObject* newRootObject);
    /** @brief Execute the events up to the given time
  @param[in] cTime the current Time
  @return code describing the effect of the executed events
  */
    virtual change_code executeEvents(coreTime cTime);

    /** @brief Execute the first part of the events only
  @param[in] cTime the current Time
  @return code describing the effect of the executed events
  */
    virtual change_code executeEventsAonly(coreTime cTime);

    /** @brief Execute second portion of any events where the A portion (could be skipped) was
  executed by a call to execute A Events only
  @return code describing the effect of the executed events
  */
    virtual change_code executeEventsBonly();

    virtual count_t size() const;
    /** @brief sort the event Queue by time */
    virtual void sort();

    /** @brief remove an event
  @param[in] eventID the id of the event to remove
  */
    virtual void remove(std::int64_t eventID);

    /** @brief recheck all the time of the events for events that may have changed times and resort
     * if required*/
    virtual void recheck();

    /** @brief get a vector of all the objects referenced in the event queue*/
    virtual void getEventObjects(std::vector<CoreObject*>& objV) const;

    /** @brief set parameters for the queue
  @param[in] param a string representing the parameter to set
  @param[in] val the value to set the parameter to
  @throw invalid parameter if the parameter is not recognized
  */
    virtual void set(std::string_view param, double val);

    /** @brief set the null event time
     the null event is an event that does nothing setting this time is a way to mark events that
  can't be described by an event Adapter
  @param[in] time the time for the null event
  @param[in] period the period of the null event
  */
    void nullEventTime(coreTime time, coreTime period = negTime);

    /** @brief get the time for the next Null Event*/
    coreTime getNullEventTime() const;

  private:
    /** @brief check for duplicate events and remove the duplicate
      this is important for removing duplicate CoreObject events so we don't have two of those being
      executed which could cause all sorts of issues with the simulation
      */
    virtual void checkDuplicates();
};

}  // namespace griddyn
