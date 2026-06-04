/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CommMessage.h"

#include "gmlc/utilities/stringConversion.h"
#include <charconv>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

using archiver = cereal::PortableBinaryOutputArchive;

using retriever = cereal::PortableBinaryInputArchive;

namespace griddyn {
namespace {
    std::map<std::string_view, std::uint32_t, std::less<>> gAlarmCodeMap{
        {"overcurrent", OVERCURRENT_ALARM},
        {"undercurrent", UNDERCURRENT_ALARM},
        {"overvoltage", OVERVOLTAGE_ALARM},
        {"undervoltage", UNDERVOLTAGE_ALARM},
        {"temperature_alarm1", TEMPERATURE_ALARM1},
        {"temperature", TEMPERATURE_ALARM1},
        {"temperature_alarm2", TEMPERATURE_ALARM2},
        {"temperature2", TEMPERATURE_ALARM2},
    };
}  // namespace

using gmlc::utilities::numeric_conversion;

REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_IGNORE, "IGNORE", CommMessage::IGNORE_MESSAGE_TYPE);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_PING, "ping", CommMessage::PING_MESSAGE_TYPE);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REPLY, "reply", CommMessage::REPLY_MESSAGE_TYPE);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_NO_EVENT, "NO EVENT", CommMessage::NO_EVENT);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_LOCAL_FAULT, "LOCAL FAULT", CommMessage::LOCAL_FAULT_EVENT);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REMOTE_FAULT, "REMOTE FAULT", CommMessage::REMOTE_FAULT_EVENT);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_BREAKER_TRIP, "BREAKER TRIP", CommMessage::BREAKER_TRIP_EVENT);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_BREAKER_CLOSE,
                      "BREAKER CLOSE",
                      CommMessage::BREAKER_CLOSE_EVENT);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_LOCAL_FAULT_CLEARED,
                      "LOCAL FAULT CLEARED",
                      CommMessage::LOCAL_FAULT_CLEARED);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REMOTE_FAULT_CLEARED,
                      "REMOTE FAULT CLEARED",
                      CommMessage::REMOTE_FAULT_CLEARED);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_BREAKER_TRIP_COMMAND,
                      "BREAKER TRIP COMMAND",
                      CommMessage::BREAKER_TRIP_COMMAND);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_BREAKER_CLOSE_COMMAND,
                      "BREAKER CLOSE COMMAND",
                      CommMessage::BREAKER_CLOSE_COMMAND);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_BREAKER_OOS_COMMAND,
                      "BREAKER OOS COMMAND",
                      CommMessage::BREAKER_OOS_COMMAND);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_ALARM_TRIGGER_EVENT,
                      "ALARM TRIGGER EVENT",
                      CommMessage::ALARM_TRIGGER_EVENT);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_ALARM_CLEARED_EVENT,
                      "ALARM CLEARED EVENT",
                      CommMessage::ALARM_CLEARED_EVENT);

CommMessage::CommMessage(std::uint32_t type): m_messageType(type)
{
    payload = CorePayloadFactory::instance().createPayload(type);
    ptype = PayloadType::shared;
}

CommMessage::CommMessage(std::uint32_t type, std::uint32_t messagecode):
    m_messageType(type), code(messagecode)
{
    payload = CorePayloadFactory::instance().createPayload(type);
    ptype = PayloadType::shared;
}

std::string CommMessage::to_string() const
{
    std::string message = MessageTypeRegistry::instance().getTypeString(m_messageType);

    if (code != 0xFFFF'FFFF) {
        message.push_back('[');
        message.append(std::to_string(code));
        message.push_back(']');
    }
    if (payload) {
        message.push_back(':');
        message.append(payload->to_string(m_messageType, code));
    }
    return message;
}
void CommMessage::from_string(std::string_view fromString)
{
    auto delimiterPos = fromString.find_first_of(":[");
    if (delimiterPos == std::string::npos) {
        m_messageType = MessageTypeRegistry::instance().getType(fromString);
        code = 0xFFFF'FFFF;
        return;
    }
    m_messageType = MessageTypeRegistry::instance().getType(fromString.substr(0, delimiterPos));
    if (fromString[delimiterPos] == '[') {
        auto end = fromString.find_first_of(']', delimiterPos + 1);
        code = numeric_conversion<std::uint32_t>(
            std::string{fromString.substr(delimiterPos + 1, end - delimiterPos - 1)}, 0xFFFFFFFF);
        delimiterPos = end + 1;
        if (delimiterPos >= fromString.size()) {
            return;
        }
    } else {
        code = 0xFFFF'FFFF;
    }
    payload = CorePayloadFactory::instance().createPayload(m_messageType);
    payload->from_string(m_messageType, code, fromString, delimiterPos + 1);
}

int CommMessage::toByteArray(char* data, size_t bufferSize) const
{
    if ((data == nullptr) || (bufferSize == 0)) {
        return -1;
    }
    boost::iostreams::basic_array_sink<char> sinkRange(data, bufferSize);
    boost::iostreams::stream<boost::iostreams::basic_array_sink<char>> sinkStream(sinkRange);

    archiver outputArchive(sinkStream);
    try {
        save(outputArchive);
        return static_cast<int>(boost::iostreams::seek(sinkStream, 0, std::ios_base::cur));
    }
    catch (const std::ios_base::failure&) {
        return -1;
    }
}

std::string CommMessage::toDataString() const
{
    std::string data;
    boost::iostreams::back_insert_device<std::string> inserter(data);
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(
        inserter);
    archiver outputArchive(outputStream);

    save(outputArchive);

    // don't forget to flush the stream to finish writing into the buffer
    outputStream.flush();
    return data;
}

std::vector<char> CommMessage::toVector() const
{
    std::vector<char> data;
    boost::iostreams::back_insert_device<std::vector<char>> inserter(data);
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::vector<char>>> outputStream(
        inserter);
    archiver outputArchive(outputStream);

    save(outputArchive);

    // don't forget to flush the stream to finish writing into the buffer
    outputStream.flush();
    return data;
}

void CommMessage::toVector(std::vector<char>& data) const
{
    data.clear();
    boost::iostreams::back_insert_device<std::vector<char>> inserter(data);
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::vector<char>>> outputStream(
        inserter);
    archiver outputArchive(outputStream);

    save(outputArchive);

    // don't forget to flush the stream to finish writing into the buffer
    outputStream.flush();
}

void CommMessage::toDataString(std::string& data) const
{
    data.clear();

    boost::iostreams::back_insert_device<std::string> inserter(data);
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(
        inserter);
    archiver outputArchive(outputStream);

    save(outputArchive);

    // don't forget to flush the stream to finish writing into the buffer
    outputStream.flush();
}

void CommMessage::fromByteArray(const char* data, size_t bufferSize)
{
    boost::iostreams::basic_array_source<char> device(data, bufferSize);
    boost::iostreams::stream<boost::iostreams::basic_array_source<char>> inputStream(device);
    retriever inputArchive(inputStream);
    try {
        load(inputArchive);
    }
    catch (const cereal::Exception&) {
        m_messageType = UNKNOWN_MESSAGE_TYPE;
    }
}

void CommMessage::fromDataString(std::string_view data)
{
    fromByteArray(data.data(), data.size());
}

void CommMessage::fromVector(const std::vector<char>& data)
{
    fromByteArray(data.data(), data.size());
}

std::uint32_t getAlarmCode(std::string_view alarmStr)
{
    auto fnd = gAlarmCodeMap.find(alarmStr);
    if (fnd != gAlarmCodeMap.end()) {
        return fnd->second;
    }
    return 0xFFFFFFFF;
}

MessageTypeRegistry& MessageTypeRegistry::instance()
{
    // can't use make shared because the constructor is private  note it is static so only created
    // once
    static MessageTypeRegistry registry;
    return registry;
}

void MessageTypeRegistry::registerType(std::string_view name, std::uint32_t type)
{
    typeMapA[std::string{name}] = type;
    typeMapB[type] = std::string{name};
}

uint32_t MessageTypeRegistry::getType(std::string_view name) const
{
    auto fnd = typeMapA.find(std::string{name});

    if (fnd != typeMapA.end()) {
        return fnd->second;
    }
    if (name.starts_with("type_")) {
        std::uint32_t type{CommMessage::UNKNOWN_MESSAGE_TYPE};
        const auto typeId = name.substr(5);
        const auto* begin = typeId.data();
        const auto* end = begin + typeId.size();
        const auto result = std::from_chars(begin, end, type);
        if ((result.ec == std::errc{}) && (result.ptr == end)) {
            return type;
        }
    }
    return CommMessage::UNKNOWN_MESSAGE_TYPE;
}

std::string MessageTypeRegistry::getTypeString(int32_t type) const
{
    auto fnd = typeMapB.find(type);

    if (fnd != typeMapB.end()) {
        return fnd->second;
    }
    auto ret = std::string("type_");
    ret.append(std::to_string(type));
    return ret;
}

CorePayloadFactory& CorePayloadFactory::instance()
{
    // can't use make shared because the constructor is private  note it is static so only created
    // once
    static CorePayloadFactory factory;
    return factory;
}

void CorePayloadFactory::registerFactory(std::string_view name, PayloadFactory* messageFactory)
{
    auto ret = m_factoryMap.emplace(std::string{name}, messageFactory);
    if (!ret.second) {
        ret.first->second = messageFactory;
    }
}

void CorePayloadFactory::registerFactory(PayloadFactory* messageFactory)
{
    auto ret = m_factoryMap.emplace(messageFactory->name, messageFactory);
    if (!ret.second) {
        ret.first->second = messageFactory;
    }
}

PayloadFactory* CorePayloadFactory::getFactory(std::string_view factoryName)
{
    auto mfind = m_factoryMap.find(std::string{factoryName});
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    return nullptr;
}

// always find the narrowest range that valid
PayloadFactory* CorePayloadFactory::getFactory(std::uint32_t type)
{
    std::uint32_t crange = 0xFFFFFFFF;
    PayloadFactory* cfact = nullptr;
    for (auto& fact : m_factoryMap) {
        if (fact.second->inRange(type)) {
            if (fact.second->range() < crange) {
                crange = fact.second->range();
                cfact = fact.second;
            }
        }
    }

    return cfact;
}

std::vector<std::string> CorePayloadFactory::getPayloadTypeNames()
{
    std::vector<std::string> typeNames;
    typeNames.reserve(m_factoryMap.size());
    for (const auto& typeName : m_factoryMap) {
        typeNames.push_back(typeName.first);
    }
    return typeNames;
}

std::shared_ptr<CommPayload> CorePayloadFactory::createPayload(std::string_view messageType)
{
    auto mfind = m_factoryMap.find(std::string{messageType});
    if (mfind != m_factoryMap.end()) {
        auto obj = mfind->second->makePayload();
        return obj;
    }
    return nullptr;
}

std::shared_ptr<CommPayload> CorePayloadFactory::createPayload(std::string_view messageType,
                                                               std::uint32_t type)
{
    auto mfind = m_factoryMap.find(std::string{messageType});
    if (mfind != m_factoryMap.end()) {
        auto obj = mfind->second->makePayload();
        return obj;
    }
    return createPayload(type);
}

// always find the narrowest range that valid
std::shared_ptr<CommPayload> CorePayloadFactory::createPayload(std::uint32_t type)
{
    std::uint32_t crange = 0xFFFFFFFF;
    PayloadFactory* cfact = nullptr;
    for (auto& fact : m_factoryMap) {
        if (fact.second->inRange(type)) {
            if (fact.second->range() < crange) {
                crange = fact.second->range();
                cfact = fact.second;
            }
        }
    }

    if (cfact != nullptr) {
        return cfact->makePayload();
    }

    return nullptr;
}

bool CorePayloadFactory::isValidMessage(std::string_view messageType)
{
    auto mfind = m_factoryMap.find(std::string{messageType});
    return (mfind != m_factoryMap.end());
}

}  // namespace griddyn
