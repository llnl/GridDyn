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

REGISTER_MESSAGE_TYPE(m1, "SET", controlMessagePayload::SET);
REGISTER_MESSAGE_TYPE(m2, "GET", controlMessagePayload::GET);
REGISTER_MESSAGE_TYPE(m3, "GET MULTIPLE", controlMessagePayload::GET_MULTIPLE);
REGISTER_MESSAGE_TYPE(m4, "GET PERIODIC", controlMessagePayload::GET_PERIODIC);
REGISTER_MESSAGE_TYPE(m5, "SET MULTIPLE", controlMessagePayload::SET_MULTIPLE);
REGISTER_MESSAGE_TYPE(m6, "SET SUCCESS", controlMessagePayload::SET_SUCCESS);
REGISTER_MESSAGE_TYPE(m7, "SET FAIL", controlMessagePayload::SET_FAIL);
REGISTER_MESSAGE_TYPE(m8, "GET RESULT", controlMessagePayload::GET_RESULT);
REGISTER_MESSAGE_TYPE(m9, "GET RESULT MULTIPLE", controlMessagePayload::GET_RESULT_MULTIPLE);
REGISTER_MESSAGE_TYPE(m10, "SET SCHEDULED", controlMessagePayload::SET_SCHEDULED);
REGISTER_MESSAGE_TYPE(m11, "GET SCHEDULED", controlMessagePayload::GET_SCHEDULED);
REGISTER_MESSAGE_TYPE(m12, "CANCEL", controlMessagePayload::CANCEL);
REGISTER_MESSAGE_TYPE(m13, "CANCEL SUCCESS", controlMessagePayload::CANCEL_SUCCESS);
REGISTER_MESSAGE_TYPE(m14, "CANCEL FAIL", controlMessagePayload::CANCEL_FAIL);
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
            for (auto& fld : multiFields) {
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
