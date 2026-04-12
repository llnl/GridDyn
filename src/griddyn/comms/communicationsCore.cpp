/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "communicationsCore.h"

#include "Communicator.h"
#include "gmlc/containers/mapOps.hpp"
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
std::shared_ptr<communicationsCore> communicationsCore::instance()
{
    static auto m_pInstance = std::shared_ptr<communicationsCore>(new communicationsCore());
    return m_pInstance;
}

void communicationsCore::registerCommunicator(Communicator* comm)
{
    auto ret = m_stringMap.emplace(comm->getName(), comm);
    if (!ret.second) {
        throw(std::invalid_argument("communicator already registered"));
    }
    auto ret2 = m_idMap.emplace(comm->getID(), comm);
    if (!ret2.second) {
        // removing the successful m_stringMap emplace operation the m_stringMap emplace success for
        // exception safety
        auto resName = m_stringMap.find(comm->getName());
        if (resName != m_stringMap.end()) {
            m_stringMap.erase(resName);
        }
        throw(std::invalid_argument("communicator already registered"));
    }
}

void communicationsCore::unregisterCommunicator(Communicator* comm)
{
    auto resName = m_stringMap.find(comm->getName());
    if (resName != m_stringMap.end()) {
        m_stringMap.erase(resName);
    }
    auto resID = m_idMap.find(comm->getID());
    if (resID != m_idMap.end()) {
        m_idMap.erase(resID);
    }
}

int communicationsCore::send(std::uint64_t source,
                             const std::string& dest,
                             std::shared_ptr<commMessage> message)
{
    auto res = m_stringMap.find(dest);
    if (res != m_stringMap.end()) {
        res->second->receive(source, dest, std::move(message));
        return SEND_SUCCESS;
    }
    return DESTINATION_NOT_FOUND;
}

int communicationsCore::send(std::uint64_t source,
                             std::uint64_t dest,
                             std::shared_ptr<commMessage> message)
{
    auto res = m_idMap.find(dest);
    if (res != m_idMap.end()) {
        res->second->receive(source, dest, std::move(message));
        return SEND_SUCCESS;
    }
    return DESTINATION_NOT_FOUND;
}

std::uint64_t communicationsCore::lookup(const std::string& commName) const
{
    auto res = m_stringMap.find(commName);
    return (res != m_stringMap.end()) ? res->second->getID() : 0;
}
std::string communicationsCore::lookup(std::uint64_t did) const
{
    auto res = m_idMap.find(did);
    return (res != m_idMap.end()) ? res->second->getName() : "";
}

}  // namespace griddyn
