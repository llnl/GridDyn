/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "controlMessage.h"

#include "../gridDynDefinitions.hpp"
#include "gmlc/utilities/stringConversion.h"
#include <string>
#include <string_view>

namespace griddyn::comms {
namespace {
    // NOLINTNEXTLINE(bugprone-throwing-static-initialization)
    dPayloadFactory<controlMessagePayload,
                    BASE_CONTROL_MESSAGE_NUMBER,
                    BASE_CONTROL_MESSAGE_NUMBER + 16>
        controlPayloadFactory("control");
}  // namespace

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeSet, "SET", controlMessagePayload::SET);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeGet, "GET", controlMessagePayload::GET);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeGetMultiple, "GET MULTIPLE", controlMessagePayload::GET_MULTIPLE);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeGetPeriodic, "GET PERIODIC", controlMessagePayload::GET_PERIODIC);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeSetMultiple, "SET MULTIPLE", controlMessagePayload::SET_MULTIPLE);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeSetSuccess, "SET SUCCESS", controlMessagePayload::SET_SUCCESS);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeSetFail, "SET FAIL", controlMessagePayload::SET_FAIL);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeGetResult, "GET RESULT", controlMessagePayload::GET_RESULT);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeGetResultMultiple,
                      "GET RESULT MULTIPLE",
                      controlMessagePayload::GET_RESULT_MULTIPLE);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeSetScheduled,
                      "SET SCHEDULED",
                      controlMessagePayload::SET_SCHEDULED);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeGetScheduled,
                      "GET SCHEDULED",
                      controlMessagePayload::GET_SCHEDULED);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeCancel, "CANCEL", controlMessagePayload::CANCEL);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeCancelSuccess,
                      "CANCEL SUCCESS",
                      controlMessagePayload::CANCEL_SUCCESS);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeCancelFail, "CANCEL FAIL", controlMessagePayload::CANCEL_FAIL);
std::string controlMessagePayload::to_string(uint32_t type, uint32_t /*code*/) const
{
    std::string temp;
    switch (type) {
        case SET:
            if (m_actionID > 0) {
                temp = "(" + std::to_string(m_actionID) + ")" + m_field;
            } else {
                temp = m_field;
            }
            if (!m_units.empty()) {
                temp += '(' + m_units + ')';
            }
            temp += " = " + std::to_string(m_value) + '@' + std::to_string(m_time);
            break;
        case GET:
            temp = m_field;
            if (!m_units.empty()) {
                temp += '(' + m_units + ')';
            }
            break;
        case GET_MULTIPLE:
            for (const auto& fld : multiFields) {
                temp += ' ' + fld + ',';
            }
            temp.pop_back();  // get rid of the last comma
            break;
        case GET_PERIODIC:
            temp = m_field;
            if (!m_units.empty()) {
                temp += '(' + m_units + ')';
            }
            temp += " @" + std::to_string(m_time);
            break;
        case GET_RESULT_MULTIPLE:
            for (size_t ii = 0; ii < multiFields.size(); ++ii) {
                temp += ' ' + multiFields[ii] + '=' + std::to_string(multiValues[ii]) + ',';
            }
            temp.pop_back();  // get rid of the last comma
            break;
        case SET_SUCCESS:
        case SET_FAIL:
        case SET_SCHEDULED:
        case GET_SCHEDULED:
        case CANCEL:
        case CANCEL_SUCCESS:
        case CANCEL_FAIL:
            if (m_actionID > 0) {
                temp = std::to_string(m_actionID);
            }
            break;
        case GET_RESULT:
            temp = m_field;
            if (!m_units.empty()) {
                temp += '(' + m_units + ')';
            }
            temp += " = " + std::to_string(m_value) + '@' + std::to_string(m_time);
            break;
        default:
            break;
    }
    return temp;
}

void controlMessagePayload::from_string(uint32_t type,
                                        uint32_t /*code*/,
                                        std::string_view fromString,
                                        size_t offset)
{
    std::string idstring;
    bool vstrid = false;

    auto vstring = fromString.substr(offset);
    if (vstring[0] == '(') {
        const auto closeParen = vstring.find_first_of(')');
        idstring = std::string{vstring.substr(1, closeParen - 1)};
        if (idstring.empty()) {
            m_actionID = 0;
        } else {
            m_actionID = std::stoull(idstring);
        }
    }

    switch (type) {
        case SET: {
            auto svec = gmlc::utilities::stringOps::splitline(std::string{vstring}, "=@");
            auto psep = gmlc::utilities::stringOps::splitline(svec[0], "()");
            if (psep.size() > 1) {
                m_units = psep[1];
            }
            m_field = psep[0];
            m_value = gmlc::utilities::numeric_conversion(svec[1], kNullVal);
            if (svec.size() > 2) {
                m_time = gmlc::utilities::numeric_conversion(svec[2], kNullVal);
            }
        } break;
        case GET: {
            auto svec = gmlc::utilities::stringOps::splitline(std::string{vstring}, '@');
            auto psep = gmlc::utilities::stringOps::splitline(svec[0], "()");
            if (psep.size() > 1) {
                m_units = psep[1];
            }
            m_field = psep[0];

            if (svec.size() > 1) {
                m_time = gmlc::utilities::numeric_conversion(svec[1], kNullVal);
            }
        } break;
        case GET_PERIODIC: {
            auto svec = gmlc::utilities::stringOps::splitline(std::string{vstring}, '@');
            auto psep = gmlc::utilities::stringOps::splitline(std::string{vstring}, ',');
            if (psep.size() > 1) {
                m_units = psep[1];
            }
            m_field = psep[0];

            if (svec.size() > 1) {
                m_time = gmlc::utilities::numeric_conversion(svec[1], kNullVal);
            }
        } break;
        case GET_MULTIPLE: {
            multiFields = gmlc::utilities::stringOps::splitline(std::string{vstring}, ',');
        } break;
        case GET_RESULT_MULTIPLE: {
            auto multiFieldStrings =
                gmlc::utilities::stringOps::splitline(std::string{vstring}, ',');
            multiFields.resize(0);
            multiValues.resize(0);
            for (auto& mfl : multiFieldStrings) {
                auto fsep = gmlc::utilities::stringOps::splitline(mfl, '=');
                if (fsep.size() == 2) {
                    multiFields.push_back(fsep[0]);
                    multiValues.push_back(gmlc::utilities::numeric_conversion(fsep[1], kNullVal));
                }
            }
        } break;
        case GET_RESULT: {
            auto svec = gmlc::utilities::stringOps::splitline(std::string{vstring}, "=@");
            auto psep = gmlc::utilities::stringOps::splitline(svec[0], "()");
            if (psep.size() > 1) {
                m_units = psep[1];
            }
            m_field = psep[0];
            m_value = gmlc::utilities::numeric_conversion(svec[1], kNullVal);
            if (svec.size() > 2) {
                m_time = gmlc::utilities::numeric_conversion(svec[2], kNullVal);
            }
        } break;
        default:
            vstrid = true;
            break;
    }

    if (vstrid) {
        if (vstring.empty()) {
            m_actionID = 0;
        } else {
            m_actionID = std::stoull(std::string{vstring});
        }
    }
}

}  // namespace griddyn::comms
