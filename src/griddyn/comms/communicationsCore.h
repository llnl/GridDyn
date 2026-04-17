#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gridDynDefinitions.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace griddyn {
class Communicator;
class commMessage;

typedef std::unordered_map<std::string, Communicator*> commMapString;
typedef std::unordered_map<std::uint64_t, Communicator*> commMapID;

#define SEND_SUCCESS (0)
#define DESTINATION_NOT_FOUND (-1);
/** communicationCore object represents a very basic communications router for internal use inside
GridDyn
@details it maintains a table and sends messages to the appropriate destination using callbacks
*/
class communicationsCore {
  public:
    /** get an instance of the singleton core */
    static std::shared_ptr<communicationsCore> instance();
    virtual ~communicationsCore() = default;
    /** register a communicator*/
    virtual void registerCommunicator(Communicator* comm);
    /** unregister a communicator */
    virtual void unregisterCommunicator(Communicator* comm);
    /** send a message to a specified destination
  @param[in] source -the identity of the source of the message
  @param[in] dest the name of the destination
  @param[in] message - the message being sent
  @return SEND_SUCCESS if the message send was successful DESTINATION_NOT_FOUND otherwise
  */
    virtual int
        send(std::uint64_t source, const std::string& dest, std::shared_ptr<commMessage> message);
    /** send a message to a specified destination
  @param[in] source -the identity of the source of the message
  @param[in] dest the name of the destination
  @param[in] message - the message being sent
  @return SEND_SUCCESS if the message send was successful DESTINATION_NOT_FOUND otherwise
  */
    virtual int
        send(std::uint64_t source, std::uint64_t dest, std::shared_ptr<commMessage> message);
    coreTime getTime() const { return m_time; }
    void setTime(coreTime nTime) { m_time = nTime; }
    /** lookup an id by name
  @param[in] commName the name of the communicator
  @return the id associated with the communicator*/
    virtual std::uint64_t lookup(const std::string& commName) const;
    /** lookup an name by id
  @param[in] did the id associated with a communicator
  @the name of the communicator*/
    virtual std::string lookup(std::uint64_t did) const;

  private:
    /** private constructor*/
    communicationsCore() = default;
    commMapString m_stringMap;  //!< map containing the strings
    commMapID m_idMap;  //!< map containing the id
    coreTime m_time = timeZero;  //!< current time of the communicator
};
}  // namespace griddyn
