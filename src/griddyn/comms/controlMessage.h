#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "commMessage.h"
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <string>
#include <vector>

#define BASE_CONTROL_MESSAGE_NUMBER 500

namespace griddyn::comms {
    /** a message type used for requesting control actions to relays and sending values in response
     */
    class controlMessagePayload: public CommPayload {
      public:
        /** the enumeration of the message types available in control messages*/
        enum control_message_t : std::uint32_t {
            SET = BASE_CONTROL_MESSAGE_NUMBER + 3,  //!< set a single value
            GET = BASE_CONTROL_MESSAGE_NUMBER + 4,  //!< request a single value
            GET_MULTIPLE = BASE_CONTROL_MESSAGE_NUMBER + 5,  //!< request multiple values
            GET_PERIODIC = BASE_CONTROL_MESSAGE_NUMBER + 6,  //!< get values on a periodic basis
            SET_MULTIPLE = BASE_CONTROL_MESSAGE_NUMBER + 8,  //!< set multiple values
            SET_SUCCESS = BASE_CONTROL_MESSAGE_NUMBER +
                10,  //!< acknowledge that the set operation was successful
            SET_FAIL =
                BASE_CONTROL_MESSAGE_NUMBER + 12,  //!< response when the set operation failed
            GET_RESULT = BASE_CONTROL_MESSAGE_NUMBER + 14,  //!< the result of get request
            GET_RESULT_MULTIPLE =
                BASE_CONTROL_MESSAGE_NUMBER + 16,  //!< the results of a get multiple request
            SET_SCHEDULED =
                BASE_CONTROL_MESSAGE_NUMBER + 18,  //!< set operation to be scheduled in the future
            GET_SCHEDULED =
                BASE_CONTROL_MESSAGE_NUMBER + 20,  //!< get operation to be scheduled in the future
            CANCEL = BASE_CONTROL_MESSAGE_NUMBER + 22,  //!< cancel a scheduled operation
            CANCEL_SUCCESS = BASE_CONTROL_MESSAGE_NUMBER +
                24,  //!< acknowledge that the cancel operation was successful
            CANCEL_FAIL =
                BASE_CONTROL_MESSAGE_NUMBER + 26  //!< respond to a cancel with an indication that
                                                  //!< the cancel operation was not successful
        };
        std::string m_field;  //!< the field in question
        double m_value = 0.0;  //!< the value of the field
        double m_time = -1.0;  //!< the time of the action
        std::int64_t m_actionID = 0;  //!< an identifier for reference of cancel operations
        std::vector<std::string> multiFields;  //!< a vector of strings in the *_MULTIPLE types
        std::vector<double> multiValues;  //!< a vector of values in the *_MULTIPLE operations
        std::string m_units;  //!< the units requested
        std::vector<std::string>
            multiUnits;  //!< the multiple units associated with different measurements
        /** default constructor*/
        controlMessagePayload() = default;

        /** constructor
    @param[in] type the type of the message
    @param[in] fld the name of the field in question
    @param[in] val the value associated with the field
    @param[in] time the time for a schedule
    */
        controlMessagePayload(const std::string& fld, double val, coreTime time):
            m_field(fld), m_value(val), m_time(time)
        {
        }

        virtual std::string to_string(uint32_t type, uint32_t code) const override;
        virtual void from_string(uint32_t type,
                                 uint32_t code,
                                 const std::string& fromString,
                                 size_t offset) override;

      private:
        friend class cereal::access;
        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<CommPayload>(this));
            ar(m_actionID, m_field, m_units, m_value, m_time);
            ar(multiFields, multiValues, multiUnits);
        }
    };

}  // namespace griddyn::comms

CEREAL_REGISTER_TYPE(griddyn::comms::controlMessagePayload)
