/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "comms/commManager.h"
#include "core/objectOperatorInterface.hpp"
#include "gridPrimary.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class stateGrabber;
class gridGrabber;
class Condition;
class Communicator;
class eventAdapter;
class Event;
class commMessage;
class propertyBuffer;

enum class change_code;  // forward declare change_code enumeration

/**
*@brief relay class:
 relay's are sensors and actuators.  They can read data from griddyn and then take actions on other
* objects on a regular schedule or on a functional basis.
**/
class Relay: public gridPrimary, objectOperatorInterface {
  public:
    static std::atomic<count_t>
        relayCount;  //!< static counter for the number of relays to generate an id number
    /** @brief enumeration of the relay condition states*/
    enum class condition_status_t {
        active,  //!< the relay condition is active
        triggered,  //!< the relay condition is triggered and waiting a timeout
        disabled,  //!< the relay condition is disabled and not scanning
    };

  protected:
    /** flags for the relayFlags data*/
    enum relay_flags {
        relay_flag0 = 0,
        relay_flag1 = 1,
        relay_flag2 = 2,
        relay_flag3 = 3,
        relay_flag4 = 4,
        relay_flag5 = 5,
        relay_flag6 = 6,
        relay_flag7 = 7,
        relay_flag8 = 8,
        relay_flag9 = 9,
        relay_flag10 = 10,
        relay_flag11 = 11,
        relay_flag12 = 12,
        relay_flag13 = 13,
        relay_flag14 = 14,
        relay_flag15 = 15,
        relay_flag16 = 16,
        relay_flag17 = 17,
        relay_flag18 = 18,
        relay_flag19 = 19,
        relay_flag20 = 20,
        relay_flag21 = 21,
        relay_flag22 = 22,
        relay_flag23 = 23,
        relay_flag24 = 24,
        relay_flag25 = 25,
        relay_flag26 = 26,
        relay_flag27 = 27,
        relay_flag28 = 28,
        relay_flag29 = 29,
        relay_flag30 = 30,
        relay_flag31 = 31,

        continuous_flag = object_flag1,  //!< flag indicating the relay has some continuous checks
        resettable_flag = object_flag2,  //!< flag indicating that the conditions can be reset
        use_commLink = object_flag3,  //!< flag indicating that the relay uses communications
        power_flow_checks_flag = object_flag4,  //!< flag indicating that the relay should be in
                                                //!< operation during power flow
        extra_relay_flag =
            object_flag5,  //!< just defining an extra name for additional relay flags

    };
    coreTime triggerTime = maxTime;  //!< the next time execute
    coreObject* m_sourceObject = nullptr;  //!< the default object where the data comes from
    coreObject* m_sinkObject = nullptr;  //!< the default object where the actions occur
    std::uint16_t triggerCount = 0;  //!< count of the number of triggers
    std::uint16_t actionsTakenCount = 0;  //!< count of the number of actions taken
    std::bitset<32> relayFlags =
        0;  //!< a set of extra relays flags that derived classes can use beyond the opFlags
    // comm fields
    comms::commManager
        cManager;  //!< structure object to store and manage the communicator information

    std::shared_ptr<Communicator> commLink;  //!< communicator link

    coreTime m_nextSampleTime = maxTime;  //!< the next time to sample the conditions

  public:
    explicit Relay(const std::string& objName = "relay_$");

    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void add(coreObject* obj) override;

    /**
     *@brief add a Event to the relay
     **/
    virtual void add(std::shared_ptr<Event> ge);
    /**
     *@brief add an EventAdapter to the relay
     **/
    virtual void add(std::shared_ptr<eventAdapter> geA);
    /**
     * @brief add a condition to the relay
     **/
    virtual void add(std::shared_ptr<Condition> gc);

    /**
    *@brief update a specific action
    @param[in] ge a gridEvent associated with the action
    @param[in] actionNumber the index of the action to update
    **/
    virtual void updateAction(std::shared_ptr<Event> ge, index_t actionNumber);
    /**
    *@brief update a specific action
    @param[in] geA an event Adapter to associate with an action
    @param[in] actionNumber the index of the action to update
    **/
    virtual void updateAction(std::shared_ptr<eventAdapter> geA, index_t actionNumber);
    /**
    *@brief update a specific condition
    @param[in] gc a condition object to associate with a relay condition
    @param[in] conditionNumber the index of the condition to update with the new condition object
    **/
    virtual void updateCondition(std::shared_ptr<Condition> gc, index_t conditionNumber);

    /**
     *@brief reset the relay
     **/
    void resetRelay();
    /**
    * @brief set the relay source object
    @param[in] obj the object to act as the default source for measurements
    */
    void setSource(coreObject* obj);
    /**
    * @brief set the relay sink object
    @param[in] obj the object to act as the default sink for an object
    */
    void setSink(coreObject* obj);
    /**
    @brief set the status indicator for a particular condition
    @param[in] conditionNumber the index of the condition in question
    @param[in] newStatus the updated status of the condition active, triggered, disabled
    */
    void setConditionStatus(index_t conditionNumber,
                            condition_status_t newStatus = condition_status_t::active);
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
    /** retrieve and eventAdapter associated with a particular action
    @param[in] actionNumber the index of the action to retrieve
    @return a shared pointer associated with particular action*/
    std::shared_ptr<eventAdapter> getAction(index_t actionNumber);
    /**
    @brief get the status of one of the relays conditions
    @param[in] conditionNumber the index of the condition
    @return an enumeration of the condition status (active, triggered, or disabled)
    */
    condition_status_t getConditionStatus(index_t conditionNumber);
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
                             const stateData& sD,
                             const solverMode& sMode) const;
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
                                  coreTime delayTime = timeZero);
    /** manually trigger a particular action
    @param[in] actionNumber the index of the action to manually trigger
    @return a change_code associated with the action describing the level of change to the system
    */
    virtual change_code triggerAction(index_t actionNumber);
    /** define a set of conditions which all must be true for certain period of time before the
    action is triggered
    @param[in] multi_conditions the set of condition indices which must all be true before an action
    is taken
    @param[in] actionNumber the index of the action to take once all conditions are true for
    delayTime
    @param[in] delayTime the period of time which all conditions must be true before triggering the
    action
    */
    virtual void setActionMultiTrigger(index_t actionNumber,
                                       const IOlocs& multi_conditions,
                                       coreTime delayTime = timeZero);

    /** define the margin by which a resettable condition must be on the other side of reset level
    to actually reset
    @param[in] conditionNumber the index of the condition to alter
    @param[in] margin the numerical value by which a value must be on opposite side of the reset
    level to actually reset
    */
    void setResetMargin(index_t conditionNumber, double margin);
    virtual void setFlag(const std::string& flag, bool val = true) override;
    virtual void set(const std::string& param, const std::string& val) override;

    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    virtual void updateA(coreTime time) override;
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual change_code
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level) override;
    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& sMode) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t level) override;
    /** message processing function for use with communicators
    @param[in] sourceID  the source of the comm message
    @param[in] message the actual message to process
    */
    virtual void receiveMessage(std::uint64_t sourceID, std::shared_ptr<commMessage> message);
    /** send and alarm message
    @param[in] code the identifier to put in the alarm message
    @throw  if no commlink is present
    */
    void sendAlarm(std::uint32_t code);
    /** generate an alarm event
    @param[in] val a string defining the alarm
    */
    std::unique_ptr<eventAdapter> make_alarm(const std::string& val);
    // Object operator interface functions

    virtual void updateObjectLinkages(coreObject* newRoot) override;
    virtual void updateObject(coreObject* obj,
                              object_update_mode mode = object_update_mode::direct) override;
    virtual coreObject* getObject() const override;
    virtual void getObjects(std::vector<coreObject*>& objects) const override;

    virtual coreObject* find(const std::string& objName) const override;

  protected:
    /** update the number of root finding functions used in the relay
    @param[in] alertChange true if the function should send alerts to its parent object if the
    number of roots changes
    */
    virtual void updateRootCount(bool alertChange = true);
    /** do something when an action is taken
    @param ActionNum  the index of the action that was executed
    @param conditionNum the index of the condition that triggered the action
    @param actionReturn  the return code of the action execution
    @param actionTime the time at which the action was taken
    */
    virtual void actionTaken(index_t ActionNum,
                             index_t conditionNum,
                             change_code actionReturn,
                             coreTime actionTime);
    /** do something when an condition is triggered
    @param conditionNum the index of the condition that triggered the action
    @param timeTriggered the time at which the condition was triggered
    */
    virtual void conditionTriggered(index_t conditionNum, coreTime timeTriggered);
    /** do something when an condition is cleared
    @param conditionNum the index of the condition that triggered the action
    @param timeCleared the time at which the condition was cleared
    */
    virtual void conditionCleared(index_t conditionNum, coreTime timeCleared);

    /** generate the commlink name*/
    virtual std::string generateCommName();

  private:
    /** @brief subclass  data container for helping with condition time checks*/
    class condCheckTime {
      public:
        index_t conditionNum;  //!< the condition Number
        index_t actionNum;  //!< the action number
        coreTime testTime;  //!< the time the test should be performed
        bool multiCondition = false;  //!< flag if the condition is part of a multiCondition

        /** @brief constructor with all the data
        @param[in] cNum the conditionNumber
        @param[in] aNum the actionNumber
        @param[in] time the time to conduct a test
        @param[in] mcond  the value of the multiCondition flag
        */
        condCheckTime(index_t cNum = 0,
                      index_t aNum = 0,
                      coreTime time = maxTime,
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
        coreTime delayTime =
            timeZero;  //!< the delay time all conditions must be true before the action is taken
        //!< TODO:PT account for this delay
        mcondTrig() = default;
        mcondTrig(index_t actNum, const IOlocs& conds, coreTime delTime = timeZero):
            actionNum(actNum), multiConditions(conds), delayTime(delTime)
        {
        }
    };
    /** enumeration of relay flags*/
    // count_t numAlgRoots = 0;        //!< counter for the number of root finding operations
    // related to the condition checking
    std::vector<std::shared_ptr<Condition>> conditions;  //!< state conditionals for the system
    std::vector<std::shared_ptr<eventAdapter>>
        actions;  //!< actions to take in response to triggers
    std::vector<std::vector<index_t>> actionTriggers;  //!< the conditions that cause actions
    std::vector<std::vector<coreTime>>
        actionDelays;  //!< the periods of time in which the condition must be true for an action to
                       //!< occur
    std::vector<condition_status_t> cStates;  //!< a vector of states for the conditions
    std::vector<coreTime> conditionTriggerTimes;  //!< the times at which the condition triggered
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
    @return a change_code indicating the effect of the action
    */
    change_code executeAction(index_t actionNumber, index_t conditionNumber, coreTime actionTime);
    /** trigger a specific condition
    @param[in] conditionNum  the index of the condition to trigger
    @param[in] conditionTriggerTime the time of the trigger
    @param[in] minimumDelayTime  ignore all trigger delays below the minimumDelayTime
    */
    change_code triggerCondition(index_t conditionNum,
                                 coreTime conditionTriggerTime,
                                 coreTime minimumDelayTime);

    /** check and if all conditions hold execute a multi-condition trigger
    @param[in] conditionNum  the index of the condition that was just triggered that might also
    trigger a multi-condition
    @param[in] conditionTriggerTime the time of the trigger
    @param[in] minimumDelayTime  ignore all trigger delays below the minimumDelayTime
    */
    change_code multiConditionCheckExecute(index_t conditionNum,
                                           coreTime conditionTriggerTime,
                                           coreTime minimumDelayTime);
    /** evaluate a condition awaiting a delay and execute the action if appropriate
    @param[in] cond the condition to check
    @param[in] checkTime the time to check
    @return a change code indicating the effect of any action Taken
    */
    change_code evaluateCondCheck(condCheckTime& cond, coreTime checkTime);
};

}  // namespace griddyn
