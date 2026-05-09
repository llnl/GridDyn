/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
        send(std::uint64_t source,
             std::string_view dest,
             const std::shared_ptr<commMessage>& message);
    /** send a message to a specified destination
  @param[in] source -the identity of the source of the message
  @param[in] dest the name of the destination
  @param[in] message - the message being sent
  @return SEND_SUCCESS if the message send was successful DESTINATION_NOT_FOUND otherwise
  */
    virtual int
        send(std::uint64_t source,
             std::uint64_t dest,
             const std::shared_ptr<commMessage>& message);
    coreTime getTime() const { return mTime; }
    void setTime(coreTime nTime) { mTime = nTime; }
    /** lookup an id by name
  @param[in] commName the name of the communicator
  @return the id associated with the communicator*/
    virtual std::uint64_t lookup(std::string_view commName) const;
    /** lookup an name by id
  @param[in] did the id associated with a communicator
  @the name of the communicator*/
    virtual std::string lookup(std::uint64_t did) const;

  private:
    /** private constructor*/
    communicationsCore() = default;
    commMapString mStringMap;  //!< map containing the strings
    commMapID mIdMap;  //!< map containing the id
    coreTime mTime = timeZero;  //!< current time of the communicator
};
}  // namespace griddyn
