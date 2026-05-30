/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"  //some special features for links
#include "../Relay.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace griddyn {
class CommMessage;
namespace comms {
    class ControlMessagePayload;
}

class GridSimulation;
class FunctionEventAdapter;
class GridGrabber;

enum class ChangeCode;
namespace relays {
    /**helper class for delayed execution of set functions*/
    struct DelayedControlAction {
        std::uint64_t sourceID;  //!< the id of the source
        std::uint64_t actionID;  //!< the id of the action itself
        std::string field;  //!< the field to act upon
        coreTime triggerTime;  //!< the time the delayed action should be triggered
        coreTime executionTime;  //!< the time it was executed
        double val;  //!< the value associated with the change
        units::unit unitType = units::defunit;  //!< the units associated with the action
        bool executed;  //!< flag indicating the action is executed
        bool measureAction;  //!< flag indicating the action is a measurement event
    };

    /** @brief relay with control functionality  i.e. the ability to control an object through a
     * comm channel
     */
    class ControlRelay: public Relay {
      public:
        enum ControlRelayFlags {
            LINK_TYPE_SOURCE = object_flag9,
            LINK_TYPE_SINK = object_flag10,
            NO_MESSAGE_REPLY = object_flag11,
        };

      protected:
        coreTime actionDelay = timeZero;  //!< the delay between comm signal and action
        coreTime measureDelay =
            timeZero;  //!< the delay between comm measure request and action measurement extraction
        count_t instructionCounter = 0;  //!< counter for the number of instructions
        std::int16_t m_terminal =
            1;  //!< the terminal of a link device to act upon(if source or sink is a link
        std::int16_t autoName = -1;  //!< variable for autonaming
        std::vector<DelayedControlAction> actions;  //!< queue for delayed control actions
        GridSimulation* rootSim = nullptr;  //!< pointer to the root object
        std::vector<std::unique_ptr<GridGrabber>>
            measurement_points_;  //!< vector of grabbers defining measurement points
        std::unordered_map<std::string, index_t>
            pointNames_;  //!< vector of names for the pointlist;
      private:
        std::string m_terminal_key;  //!< string related to the terminal
      public:
        explicit ControlRelay(const std::string& objName = "controlRelay_$");
        virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
        virtual void setFlag(std::string_view flag, bool val = true) override;
        virtual void set(std::string_view param, std::string_view val) override;

        virtual void
            set(std::string_view param, double val, units::unit unitType = units::defunit) override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void updateObject(CoreObject* obj,
                                  ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override;
        /** add a measurement point to the relay
    @param[in] measure a string representing the measurement
    */
        void addMeasurement(std::string_view measure);
        /** retrieve a numbered measurement*/
        double getMeasurement(index_t num) const;
        /** retrieve the value of a named measurement*/
        double getMeasurement(std::string_view pointName) const;
        /** locate a numerical index of a measurement from its name*/
        index_t findMeasurement(std::string_view pointName) const;

      protected:
        virtual void actionTaken(index_t actionNum,
                                 index_t conditionNum,
                                 ChangeCode actionReturn,
                                 coreTime actionTime) override;

        virtual void receiveMessage(std::uint64_t sourceID,
                                    std::shared_ptr<CommMessage> message) override;
        std::string generateAutoName(int code);
        std::string generateCommName() override;

        ChangeCode executeAction(index_t actionNum);

        index_t findAction(std::uint64_t actionID);
        index_t getFreeAction();

        std::unique_ptr<FunctionEventAdapter>
            generateGetEvent(coreTime eventTime,
                             std::uint64_t sourceID,
                             comms::ControlMessagePayload* message);
        std::unique_ptr<FunctionEventAdapter>
            generateSetEvent(coreTime eventTime,
                             std::uint64_t sourceID,
                             comms::ControlMessagePayload* message);
    };
}  // namespace relays
}  // namespace griddyn
