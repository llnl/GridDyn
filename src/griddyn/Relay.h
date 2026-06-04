/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GridPrimary.h"
#include "comms/CommManager.h"
#include "core/ObjectOperatorInterface.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class StateGrabber;
class GridGrabber;
class Condition;
class Communicator;
class EventAdapter;
class Event;
class CommMessage;
class PropertyBuffer;

enum class ChangeCode;  // forward declare ChangeCode enumeration

/**
*@brief relay class:
 relay's are sensors and actuators.  They can read data from griddyn and then take actions on other
* objects on a regular schedule or on a functional basis.
**/
class Relay: public GridPrimary, ObjectOperatorInterface {
  public:
    static std::atomic<count_t>
        relayCount;  //!< static counter for the number of relays to generate an id number
    /** @brief enumeration of the relay condition states*/
    enum class ConditionStatus {
        ACTIVE,  //!< the relay condition is ACTIVE
        TRIGGERED,  //!< the relay condition is TRIGGERED and waiting a timeout
        DISABLED,  //!< the relay condition is DISABLED and not scanning
    };

  protected:
    /** flags for the relayFlags data*/
    enum RelayFlags {
        RELAY_FLAG0 = 0,
        RELAY_FLAG1 = 1,
        RELAY_FLAG2 = 2,
        RELAY_FLAG3 = 3,
        RELAY_FLAG4 = 4,
        RELAY_FLAG5 = 5,
        RELAY_FLAG6 = 6,
        RELAY_FLAG7 = 7,
        RELAY_FLAG8 = 8,
        RELAY_FLAG9 = 9,
        RELAY_FLAG10 = 10,
        RELAY_FLAG11 = 11,
        RELAY_FLAG12 = 12,
        RELAY_FLAG13 = 13,
        RELAY_FLAG14 = 14,
        RELAY_FLAG15 = 15,
        RELAY_FLAG16 = 16,
        RELAY_FLAG17 = 17,
        RELAY_FLAG18 = 18,
        RELAY_FLAG19 = 19,
        RELAY_FLAG20 = 20,
        RELAY_FLAG21 = 21,
        RELAY_FLAG22 = 22,
        RELAY_FLAG23 = 23,
        RELAY_FLAG24 = 24,
        RELAY_FLAG25 = 25,
        RELAY_FLAG26 = 26,
        RELAY_FLAG27 = 27,
        RELAY_FLAG28 = 28,
        RELAY_FLAG29 = 29,
        RELAY_FLAG30 = 30,
        RELAY_FLAG31 = 31,

        CONTINUOUS_FLAG = OBJECT_FLAG1,  //!< flag indicating the relay has some continuous checks
        RESETTABLE_FLAG = OBJECT_FLAG2,  //!< flag indicating that the conditions can be reset
        USE_COMM_LINK = OBJECT_FLAG3,  //!< flag indicating that the relay uses communications
        POWER_FLOW_CHECKS_FLAG = OBJECT_FLAG4,  //!< flag indicating that the relay should be in
                                                //!< operation during power flow
        EXTRA_RELAY_FLAG =
            OBJECT_FLAG5,  //!< just defining an extra name for additional relay flags

    };
    CoreTime triggerTime = maxTime;  //!< the next time execute
    CoreObject* m_sourceObject = nullptr;  //!< the default object where the data comes from
    CoreObject* m_sinkObject = nullptr;  //!< the default object where the actions occur
    std::uint16_t triggerCount = 0;  //!< count of the number of triggers
    std::uint16_t actionsTakenCount = 0;  //!< count of the number of actions taken
    std::bitset<32> relayFlags =
        0;  //!< a set of extra relays flags that derived classes can use beyond the opFlags
    // comm fields
    comms::CommManager
        cManager;  //!< structure object to store and manage the communicator information

    std::shared_ptr<Communicator> commLink;  //!< communicator link

    CoreTime m_nextSampleTime = maxTime;  //!< the next time to sample the conditions

  public:
    explicit Relay(const std::string& objName = "relay_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void add(CoreObject* obj) override;

    /**
     *@brief add a Event to the relay
     **/
    virtual void add(std::shared_ptr<Event> eventObject);
    /**
     *@brief add an EventAdapter to the relay
     **/
    virtual void add(std::shared_ptr<EventAdapter> eventAdapter);
    /**
     * @brief add a condition to the relay
     **/
    virtual void add(std::shared_ptr<Condition> conditionObject);

    /**
    *@brief update a specific action
    @param[in] eventObject a gridEvent associated with the action
    @param[in] actionNumber the index of the action to update
    **/
    virtual void updateAction(std::shared_ptr<Event> eventObject, index_t actionNumber);
    /**
    *@brief update a specific action
    @param[in] eventAdapter an event Adapter to associate with an action
    @param[in] actionNumber the index of the action to update
    **/
    virtual void updateAction(std::shared_ptr<EventAdapter> eventAdapter, index_t actionNumber);
    /**
    *@brief update a specific condition
    @param[in] conditionObject a condition object to associate with a relay condition
    @param[in] conditionNumber the index of the condition to update with the new condition object
    **/
    virtual void updateCondition(std::shared_ptr<Condition> conditionObject,
                                 index_t conditionNumber);

    /**
     *@brief reset the relay
     **/
    void resetRelay();
    /**
    * @brief set the relay source object
    @param[in] obj the object to act as the default source for measurements
    */
    void setSource(CoreObject* obj);
    /**
    * @brief set the relay sink object
    @param[in] obj the object to act as the default sink for an object
    */
    void setSink(CoreObject* obj);
    /**
    @brief set the status indicator for a particular condition
    @param[in] conditionNumber the index of the condition in question
    @param[in] newStatus the updated status of the condition active, triggered, disabled
    */
    void setConditionStatus(index_t conditionNumber,
                            ConditionStatus newStatus = ConditionStatus::ACTIVE);
    /**
    @brief remove an action from service
    @param[in] actionNumber the index of the action to remove from service
    */
    void removeAction(index_t actionNumber);

    /** @brief get a condition object from the relay
    @param[in] conditionNumber the index of the condition retrieve
    @return a shared pointer to the condition object
    */
    std::shared_ptr<Condition> getCondition(index_t conditionNumber);
    /** retrieve and EventAdapter associated with a particular action
    @param[in] actionNumber the index of the action to retrieve
    @return a shared pointer associated with particular action*/
    std::shared_ptr<EventAdapter> getAction(index_t actionNumber);
    /**
    @brief get the status of one of the relays conditions
    @param[in] conditionNumber the index of the condition
    @return an enumeration of the condition status (active, triggered, or disabled)
    */
    ConditionStatus getConditionStatus(index_t conditionNumber);
    /**
    @brief get the value associated with a condition
    @param[in] conditionNumber the index of the condition
    @return the value used in determining the status of a condition
    */
    double getConditionValue(index_t conditionNumber) const;
    /**
    @brief get the value associated with a condition from state data
    @param[in] conditionNumber the index of the condition
    @return the value used in determining the status of a condition
    */
    double getConditionValue(index_t conditionNumber,
                             const StateData& stateDataValue,
                             const SolverMode& sMode) const;
    /** check if a particular condition is true
    @param[in] conditionNumber the index of the condition to check
    @return true if the condition is activated
    */
    bool checkCondition(index_t conditionNumber) const;
    /** set a threshold associated with particular condition
    @param[in] conditionNumber the index of the condition to reference
    @param[in] levelVal the new threshold
    */
    void setConditionLevel(index_t conditionNumber, double levelVal);
    /** set the condition that will trigger a particular action
    @details this can be called multiple times with different values
    actions can be associated with multiple conditions and conditions can trigger multiple actions
    therefore this function can be called multiple times.  If called with the same actionNumber and
    conditionNumber only the delay is updated
    @param[in] actionNumber the condition index which is the trigger for an action
    @param[in] conditionNumber the action to associate with a particular condition
    @param[in] delayTime the time between the trigger and when the associated action is triggered
    */
    virtual void setActionTrigger(index_t actionNumber,
                                  index_t conditionNumber,
                                  CoreTime delayTime = timeZero);
    /** manually trigger a particular action
    @param[in] actionNumber the index of the action to manually trigger
    @return a ChangeCode associated with the action describing the level of change to the system
    */
    virtual ChangeCode triggerAction(index_t actionNumber);
    /** define a set of conditions which all must be true for certain period of time before the
    action is triggered
    @param[in] multiConditions the set of condition indices which must all be true before an action
    is taken
    @param[in] actionNumber the index of the action to take once all conditions are true for
    delayTime
    @param[in] delayTime the period of time which all conditions must be true before triggering the
    action
    */
    virtual void setActionMultiTrigger(index_t actionNumber,
                                       const IOlocs& multiConditions,
                                       CoreTime delayTime = timeZero);

    /** define the margin by which a resettable condition must be on the other side of reset level
    to actually reset
    @param[in] conditionNumber the index of the condition to alter
    @param[in] margin the numerical value by which a value must be on opposite side of the reset
    level to actually reset
    */
    void setResetMargin(index_t conditionNumber, double margin);
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void updateA(CoreTime time) override;
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual ChangeCode
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, CheckLevel level) override;
    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;
    /** message processing function for use with communicators
    @param[in] sourceID  the source of the comm message
    @param[in] message the actual message to process
    */
    virtual void receiveMessage(std::uint64_t sourceID, std::shared_ptr<CommMessage> message);
    /** send and alarm message
    @param[in] code the identifier to put in the alarm message
    @throw  if no commlink is present
    */
    void sendAlarm(std::uint32_t code);
    /** generate an alarm event
    @param[in] val a string defining the alarm
    */
    std::unique_ptr<EventAdapter> make_alarm(const std::string& val);
    // Object operator interface functions

    virtual void updateObjectLinkages(CoreObject* newRoot) override;
    virtual void updateObject(CoreObject* obj,
                              ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override;
    virtual CoreObject* getObject() const override;
    virtual void getObjects(std::vector<CoreObject*>& objects) const override;

    virtual CoreObject* find(std::string_view objName) const override;

  protected:
    /** update the number of root finding functions used in the relay
    @param[in] alertChange true if the function should send alerts to its parent object if the
    number of roots changes
    */
    virtual void updateRootCount(bool alertChange = true);
    /** do something when an action is taken
    @param actionNum  the index of the action that was executed
    @param conditionNum the index of the condition that triggered the action
    @param actionReturn  the return code of the action execution
    @param actionTime the time at which the action was taken
    */
    virtual void actionTaken(index_t actionNum,
                             index_t conditionNum,
                             ChangeCode actionReturn,
                             CoreTime actionTime);
    /** do something when an condition is triggered
    @param conditionNum the index of the condition that triggered the action
    @param timeTriggered the time at which the condition was triggered
    */
    virtual void conditionTriggered(index_t conditionNum, CoreTime timeTriggered);
    /** do something when an condition is cleared
    @param conditionNum the index of the condition that triggered the action
    @param timeCleared the time at which the condition was cleared
    */
    virtual void conditionCleared(index_t conditionNum, CoreTime timeCleared);

    /** generate the commlink name*/
    virtual std::string generateCommName();

  private:
    /** @brief subclass  data container for helping with condition time checks*/
    class condCheckTime {
      public:
        index_t conditionNum;  //!< the condition Number
        index_t actionNum;  //!< the action number
        CoreTime testTime;  //!< the time the test should be performed
        bool multiCondition = false;  //!< flag if the condition is part of a multiCondition

        /** @brief constructor with all the data
        @param[in] cNum the conditionNumber
        @param[in] aNum the actionNumber
        @param[in] time the time to conduct a test
        @param[in] mcond  the value of the multiCondition flag
        */
        condCheckTime(index_t cNum = 0,
                      index_t aNum = 0,
                      CoreTime time = maxTime,
                      bool mcond = false):
            conditionNum(cNum), actionNum(aNum), testTime(time), multiCondition(mcond)
        {
        }
    };
    /** @brief data type declaration for a multiCondition trigger*/
    class mcondTrig {
      public:
        index_t actionNum = kInvalidLocation;  //!< the related ActionNumber
        IOlocs multiConditions;  //!< identification of all the conditions involved
        CoreTime delayTime =
            timeZero;  //!< the delay time all conditions must be true before the action is taken
        //!< TODO:PT account for this delay
        mcondTrig() = default;
        mcondTrig(index_t actNum, const IOlocs& conds, CoreTime delTime = timeZero):
            actionNum(actNum), multiConditions(conds), delayTime(delTime)
        {
        }
    };
    /** enumeration of relay flags*/
    // count_t numAlgRoots = 0;        //!< counter for the number of root finding operations
    // related to the condition checking
    std::vector<std::shared_ptr<Condition>> conditions;  //!< state conditionals for the system
    std::vector<std::shared_ptr<EventAdapter>>
        actions;  //!< actions to take in response to triggers
    std::vector<std::vector<index_t>> actionTriggers;  //!< the conditions that cause actions
    std::vector<std::vector<CoreTime>>
        actionDelays;  //!< the periods of time in which the condition must be true for an action to
                       //!< occur
    std::vector<ConditionStatus> cStates;  //!< a vector of states for the conditions
    std::vector<CoreTime> conditionTriggerTimes;  //!< the times at which the condition triggered
    std::vector<condCheckTime>
        condChecks;  //!< a vector of condition action pairs that are in wait and see mode
    std::vector<std::vector<mcondTrig>>
        multiConditionTriggers;  //!< a vector for action which have multiple triggers
    std::vector<index_t> conditionsWithRoots;  //!< indices of the conditions with root finding
                                               //!< functions attached to them
  private:
    /** clear all the conditional checks that have passed the initial trigger but not the full time
    duration for a particular condition
    @param[in] conditionNumber the index of the condition to clear
    */
    void clearCondChecks(index_t conditionNumber);
    /** actually trigger a particular action via a particular condition
    @param[in] actionNumber the index of the action to execute
    @param[in] conditionNumber the ndex of the condition which triggered the action
    @param[in] actionTime the time which to execute the action
    @return a ChangeCode indicating the effect of the action
    */
    ChangeCode executeAction(index_t actionNumber, index_t conditionNumber, CoreTime actionTime);
    /** trigger a specific condition
    @param[in] conditionNum  the index of the condition to trigger
    @param[in] conditionTriggerTime the time of the trigger
    @param[in] minimumDelayTime  ignore all trigger delays below the minimumDelayTime
    */
    ChangeCode triggerCondition(index_t conditionNum,
                                CoreTime conditionTriggerTime,
                                CoreTime minimumDelayTime);

    /** check and if all conditions hold execute a multi-condition trigger
    @param[in] conditionNum  the index of the condition that was just triggered that might also
    trigger a multi-condition
    @param[in] conditionTriggerTime the time of the trigger
    @param[in] minimumDelayTime  ignore all trigger delays below the minimumDelayTime
    */
    ChangeCode multiConditionCheckExecute(index_t conditionNum,
                                          CoreTime conditionTriggerTime,
                                          CoreTime minimumDelayTime);
    /** evaluate a condition awaiting a delay and execute the action if appropriate
    @param[in] cond the condition to check
    @param[in] checkTime the time to check
    @return a change code indicating the effect of any action Taken
    */
    ChangeCode evaluateCondCheck(condCheckTime& cond, CoreTime checkTime);
};

}  // namespace griddyn
