/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EVENTADAPTERS_H_
#define EVENTADAPTERS_H_
#pragma once

#include "../gridDynDefinitions.hpp"
#include "core/coreObject.h"
#include "core/objectInterpreter.h"
#include "core/objectOperatorInterface.hpp"
#include "eventInterface.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

namespace griddyn {
// template<class T>
// struct is_shared_ptr : std::false_type {};

// template<class T>
// struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};
/** @brief class for managing events of many types
 class is a wrapper around a number of different kinds of discrete events
*/
class eventAdapter {
  public:
    id_type_t eventID;  //!< eventID for searching
    bool m_remove_event = false;  //!< flag to remove the event after execution
    bool two_part_execute = false;  //!< flag if the event has two parts
    bool partB_turn = false;  //!< if we need to execute the second part only
    bool partB_only = false;  //!< flag indicating we only run when partB runs
    coreTime m_period;  //!< the period of the event;
    coreTime m_nextTime;  //!< the next time the event is scheduled to execute
    coreTime partBdelay = timeZero;  //!< the delay between the first and second parts
  private:
    static std::atomic<id_type_t> eventCounter;  //!< counter for generating the unique event ID
  public:
    /** @brief constructor
    @param[in] nextTime the trigger time for the event
    @param[in] period the period of the event  (the event will trigger once per period starting at
    nextTime
    */
    eventAdapter(coreTime nextTime = maxTime, coreTime period = timeZero);

    /** @brief destructor*/
    virtual ~eventAdapter();
    /** do not allow copy construction (or any other automatic defined functions*/
    eventAdapter(const eventAdapter& ev) = delete;
    /** make a copy of the eventAdapter
   @return a unique pointer to the clone
    */
    virtual std::unique_ptr<eventAdapter> clone() const;

    /** clone the eventAdapter onto another object
    @param[in] eA  the pointer to copy the event adapter information to (may be nullptr)
    */
    virtual void cloneTo(eventAdapter* eA) const;

    /** Execute the pre-event portion of the event for two part execution events
    @param[in] cTime the current execution time
    */
    virtual void executeA(coreTime cTime);

    /** Execute the event or partB of the event
    @param[in] cTime the current execution time
    */
    virtual change_code execute(coreTime cTime);

    /** @brief update the next event time*/
    virtual void updateTime();
    /** @brief update the target coreObject
    @param[in] newObject the new object
    @param[in] mode update_mode direct or match
    */
    virtual void updateObject(coreObject* newObject,
                              object_update_mode mode = object_update_mode::direct);
    /** @brief get a list of all referenced objects*/
    virtual void getObjects(std::vector<coreObject*>& objects) const;
    /** initialize the event if needed*/
    virtual void initialize();
    /** get the event code*/
    virtual int eventCode() const;
};

bool compareEventAdapters(const std::shared_ptr<eventAdapter>& e1,
                          const std::shared_ptr<eventAdapter>& e2);

// TODO(pt): try to merge this adapter with the shared_ptr one
template<class Y>
class eventTypeAdapter: public eventAdapter {
    static_assert(std::is_base_of<eventInterface, Y>::value,
                  "classes must be inherited from eventInterface");

  private:
    Y* m_eventObj = nullptr;

  public:
    eventTypeAdapter() = default;
    explicit eventTypeAdapter(Y* ge): m_eventObj(ge)
    {
        m_nextTime = ge->nextTriggerTime();
        switch (ge->executionMode()) {
            case event_execution_mode::normal:
                break;
            case event_execution_mode::two_part_execution:
                two_part_execute = true;
                break;
            case event_execution_mode::delayed:
                two_part_execute = true;
                partB_only = true;
                break;
            default:
                break;
        }
    }

    virtual void cloneTo(eventAdapter* eA) const override
    {
        eventAdapter::cloneTo(eA);
        auto eta = dynamic_cast<eventTypeAdapter*>(eA);
        if (eta == nullptr) {
            return;
        }
        eta->m_eventObj = m_eventObj->clone();
    }

    virtual std::unique_ptr<eventAdapter> clone() const override
    {
        std::unique_ptr<eventAdapter> newAdapter = std::make_unique<eventTypeAdapter>();
        eventTypeAdapter::cloneTo(newAdapter.get());
        return newAdapter;
    }

    virtual void updateObject(coreObject* newObject, object_update_mode mode) override
    {
        m_eventObj->updateObject(newObject, mode);
        updateTime();
    }
    virtual void getObjects(std::vector<coreObject*>& objects) const override
    {
        m_eventObj->getObjects(objects);
    }
    virtual change_code execute(coreTime cTime) override
    {
        change_code retval = change_code::not_triggered;  // EVENT_NOT_TRIGGERED
        int excnt = 0;
        while (m_nextTime <= cTime) {
            auto ret = m_eventObj->trigger(cTime);
            retval = (std::max)(ret, retval);
            if (!(m_eventObj->isArmed())) {
                m_remove_event = true;
                break;
            }
            auto lastTime = m_nextTime;
            updateTime();
            excnt = (lastTime == m_nextTime) ? excnt + 1 : 0;
            if (excnt > 4) {
                std::cerr << "Event time not updating\n";
                m_remove_event = true;
                break;
            }
        }
        return retval;
    }
    virtual void updateTime() override { m_nextTime = m_eventObj->nextTriggerTime(); }

    virtual void initialize() override { m_eventObj->initialize(); }
    virtual int eventCode() const override { return m_eventObj->eventCode(); }
};

template<class Y>
class eventTypeAdapter<std::shared_ptr<Y>>: public eventAdapter {
    static_assert(std::is_base_of<eventInterface, Y>::value,
                  "classes must be inherited from eventInterface");

  private:
    std::shared_ptr<Y> m_eventObj;

  public:
    eventTypeAdapter() = default;
    explicit eventTypeAdapter(std::shared_ptr<Y> ge): m_eventObj(std::move(ge))
    {
        m_nextTime = m_eventObj->nextTriggerTime();
        switch (m_eventObj->executionMode()) {
            case event_execution_mode::normal:
                break;
            case event_execution_mode::two_part_execution:
                two_part_execute = true;
                break;
            case event_execution_mode::delayed:
                two_part_execute = true;
                partB_only = true;
                break;
            default:
                break;
        }
    }

    virtual void cloneTo(eventAdapter* eA) const override
    {
        eventAdapter::cloneTo(eA);
        auto eta = dynamic_cast<eventTypeAdapter*>(eA);
        if (eta == nullptr) {
            return;
        }
        auto evO = m_eventObj->clone();
        eta->m_eventObj = std::shared_ptr<Y>(static_cast<Y*>(evO.release()));
    }

    virtual std::unique_ptr<eventAdapter> clone() const override
    {
        std::unique_ptr<eventAdapter> newAdapter = std::make_unique<eventTypeAdapter>();
        eventTypeAdapter::cloneTo(newAdapter.get());
        return newAdapter;
    }

    virtual void updateObject(coreObject* newObject, object_update_mode mode) override
    {
        m_eventObj->updateObject(newObject, mode);
        updateTime();
    }

    virtual void getObjects(std::vector<coreObject*>& objects) const override
    {
        m_eventObj->getObjects(objects);
    }
    change_code execute(coreTime cTime) override
    {
        change_code retval = change_code::not_triggered;  // EVENT_NOT_TRIGGERED
        int excnt = 0;  //!< counter for protection against an event not behaving properly
        while (m_nextTime <= cTime) {
            auto ret = m_eventObj->trigger(cTime);
            retval = (std::max)(ret, retval);
            if (!(m_eventObj->isArmed())) {
                m_remove_event = true;
                break;
            }
            auto lastTime = m_nextTime;
            updateTime();
            excnt = (lastTime == m_nextTime) ? excnt + 1 : 0;
            if (excnt > 4) {
                std::cerr << "Event time not updating\n";
                m_remove_event = true;
                break;
            }
        }
        return retval;
    }

    virtual void updateTime() override { m_nextTime = m_eventObj->nextTriggerTime(); }
    virtual void initialize() override { m_eventObj->initialize(); }
    virtual int eventCode() const override { return m_eventObj->eventCode(); }
};

class coreObject;

template<>
class eventTypeAdapter<coreObject>: public eventAdapter {
  private:
    coreObject* targetObject = nullptr;
    int evCode_ = 0;

  public:
    eventTypeAdapter() = default;
    explicit eventTypeAdapter(coreObject* gco): targetObject(gco)
    {
        m_nextTime = targetObject->getNextUpdateTime();
        two_part_execute = true;
        evCode_ = gco->getInt("eventcode");
    }

    virtual void updateObject(coreObject* newObject, object_update_mode mode) override
    {
        if (mode == object_update_mode::direct) {
            targetObject = newObject;
        } else if (mode == object_update_mode::match) {
            auto searchRes = findMatchingObject(targetObject, newObject);
            if (searchRes) {
                targetObject = searchRes;
            } else {
                throw(objectUpdateFailException());
            }
        }
        m_nextTime = (targetObject) ? targetObject->getNextUpdateTime() : maxTime;
    }

    virtual void cloneTo(eventAdapter* eA) const override
    {
        eventAdapter::cloneTo(eA);
        auto eca = dynamic_cast<eventTypeAdapter<coreObject>*>(eA);
        if (eca == nullptr) {
            return;
        }
        eca->targetObject = targetObject;
    }

    virtual std::unique_ptr<eventAdapter> clone() const override
    {
        std::unique_ptr<eventAdapter> newAdapter = std::make_unique<eventTypeAdapter<coreObject>>();
        eventTypeAdapter<coreObject>::cloneTo(newAdapter.get());
        return newAdapter;
    }

    virtual void executeA(coreTime cTime) override { targetObject->updateA(cTime); }

    virtual change_code execute(coreTime cTime) override
    {
        targetObject->updateB();
        coreTime time = targetObject->getNextUpdateTime();
        if ((time <= cTime) && (time <= m_nextTime)) {
            m_nextTime = maxTime;
        } else {
            m_nextTime = time;
        }
        return change_code::no_change;
    }

    virtual void updateTime() override { m_nextTime = targetObject->getNextUpdateTime(); }

    virtual int eventCode() const override { return evCode_; }

    coreObject* getTarget() const { return targetObject; }
};
/** eventAdapter with a custom function call
 */
class functionEventAdapter: public eventAdapter {
  public:
    using ccode_function_t = std::function<change_code()>;

  private:
    ccode_function_t fptr;  //!< the function to execute
    int evCode_ = 0;  //!< the event return code
  public:
    /**default constructor*/
    functionEventAdapter() = default;
    /** constructor with explicit function*/
    explicit functionEventAdapter(ccode_function_t fcal);

    /** constructor from function, time and period
    @param[in] fcal the function to execute at the trigger time
    @param triggerTime the time to execute the function
    @param period if greater than 0 the function will execute with the specified period after
    triggerTIme
    */
    functionEventAdapter(ccode_function_t fcal, coreTime triggerTime, coreTime period = 0.0);

    virtual std::unique_ptr<eventAdapter> clone() const override;

    virtual void cloneTo(eventAdapter* ea) const override;

    virtual change_code execute(coreTime cTime) override;

    /** @brief set the function of the event adapter
     *@param[in] nfptr  a std::function which returns a change code and takes 0 arguments*/
    void setfunction(ccode_function_t nfptr);
    /** set the event code*/
    void setEventCode(int evCode) { evCode_ = evCode; }
    virtual int eventCode() const override { return evCode_; }
    /** set the mode of execution of the event
     */
    void setExecutionMode(event_execution_mode newMode);
};

}  // namespace griddyn
#endif
