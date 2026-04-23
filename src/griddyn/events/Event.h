/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreObject.h"
#include "core/helperObject.h"
#include "core/objectOperatorInterface.hpp"
#include "eventInterface.hpp"
#include "units/units.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
/** helper data class for holding information about an event during construction
 */
class EventInfo {
  public:
    std::string name;  //!< event name
    std::string description;  //!< event description
    std::string type;  //!< event type
    std::string file;  //!< file associated with the event
    coreTime period = timeZero;  //!< event periodicity
    std::vector<coreTime> time;  //!< event trigger times
    std::vector<double> value;  //!< event trigger values
    stringVec fieldList;  //!< list of fields associated with the event
    std::vector<coreObject*> targetObjs;  //!< the event targets
    std::vector<index_t> columns;  //!< file columns associated with an event
    std::vector<units::unit> units;  //!< units associated with an event
  public:
    EventInfo() = default;
    EventInfo(const std::string& eventString, coreObject* rootObj);

    void loadString(const std::string& eventString, coreObject* rootObj);
};

/** basic event class enabling a property change in an object
eventInterface, objectOperatorInterface are pure virtual interfaces
*/
class Event: public helperObject, public eventInterface, public objectOperatorInterface {
  protected:
    std::string field;  //!< event trigger field
    double value = 0.0;  //!< new value
    coreTime triggerTime;  //!< the time the event is scheduled to be triggered
    coreObject* m_obj = nullptr;  //!< the target object of the event
    units::unit unitType = units::defunit;  //!< units of the event
    index_t eventId;  //!< a unique Identifier code for the event
    bool armed = false;  //!< flag indicating if the event is armed or not
    bool resettable = false;  //!< flag indicating if the event can be reset;
    bool reversible = false;  //!< flag indicating if the event can be reversed;
    bool initRequired = false;  //!< flag indicating the event requires initialization
    // NOTE; there is an extra 4 bytes here

  public:
    /** construct with a name to the event*/
    explicit Event(const std::string& eventName);
    /** default constructor will set the event time to the maximum so it will never trigger on its
    own
    @param[in] time0 the time to trigger the event
    */
    explicit Event(coreTime time0 = negTime);
    /** constructor from and EventInfo object and rootObject
    @param[in] gdEI a structure defining the information of an event
    @param[in] rootObject the base object to use for searching for any parameters or other objects*/
    Event(const EventInfo& gdEI, coreObject* rootObject);
    /** duplicate the event
    @return a pointer to the clone of the event
    */
    virtual std::unique_ptr<Event> clone() const;
    /** duplicate the event to a valid event
    @param[in] evnt a pointer to an event object
    */
    virtual void cloneTo(Event* evnt) const;
    /** update the information in an event from an event info
    @param[in] gdEI the event information structure to get all the event information from
    @param[in] rootObject the root object to use in searching for other objects
    */
    virtual void updateEvent(const EventInfo& gdEI, coreObject* rootObject);
    /** trigger the event
    @return a change_code describing the impact associated with an event
    */
    virtual change_code trigger();

    virtual change_code trigger(coreTime time) override;

    virtual coreTime nextTriggerTime() const override { return triggerTime; }
    virtual bool isArmed() const override { return armed; }
    event_execution_mode executionMode() const override { return event_execution_mode::normal; }
    bool initNeeded() const { return initRequired; }
    virtual void set(std::string_view param, double val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void setFlag(std::string_view flag, bool val) override;
    /** set the trigger time of an event
     */
    virtual void setTime(coreTime time);
    /** set the value associated with a parameter change event
    @param[in] val the new value
    @param[in] newUnits the units associated with the value
    */
    virtual void setValue(double val, units::unit newUnits = units::defunit);
    /** generate a string description of the event*/
    virtual std::string to_string() const;
    /** update the event target
    @param[in] gdo the new target object for the event
    @param[in] var the new target field for the event
    @return true if the event is armed
    */
    virtual bool setTarget(coreObject* gdo, const std::string& var = "");

    virtual void updateObject(coreObject* gco,
                              object_update_mode mode = object_update_mode::direct) override;

    virtual coreObject* getObject() const override;
    virtual void getObjects(std::vector<coreObject*>& objects) const override;

  protected:
    /** update the target and field of an event*/
    void loadField(coreObject* searchObj, const std::string& newField);
    /** run a check to see if the event can be armed*/
    // Note: please remove calls to checkArmed in the constructor before making this virtual
    bool checkArmed();
};

/** construct a simple parameter change event
@param[in] field the object field to change
@param[in] val the value associated with the field
@param[in] eventTime the time the event should trigger
@param[in] rootObject the high level object to base any object searches from
@return a unique ptr to the created event
*/
std::unique_ptr<Event>
    make_event(const std::string& field, double val, coreTime eventTime, coreObject* rootObject);
/** construct an event from an event Info structure
@param[in] gdEI the information associated with an event
@param[in] rootObject the high level object to base any object searches from
@return a unique ptr to the created event
*/
std::unique_ptr<Event> make_event(EventInfo& gdEI, coreObject* rootObject);
/** construct an event from a string description
@param[in] eventString the information associated with an event
@param[in] rootObject the high level object to base any object searches from
@return a unique ptr to the created event
*/
std::unique_ptr<Event> make_event(const std::string& eventString, coreObject* rootObject);

/** equality operator*/
inline bool operator==(const Event& event1, const Event& event2)
{
    return event1.to_string() == event2.to_string();
}

}  // namespace griddyn
