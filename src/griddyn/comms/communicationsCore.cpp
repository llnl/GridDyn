/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "communicationsCore.h"

#include "Communicator.h"
#include "gmlc/containers/mapOps.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
std::shared_ptr<communicationsCore> communicationsCore::instance()
{
    static auto m_pInstance = std::shared_ptr<communicationsCore>(new communicationsCore());
    return m_pInstance;
}

void communicationsCore::registerCommunicator(Communicator* comm)
{
    auto ret = mStringMap.emplace(comm->getName(), comm);
    if (!ret.second) {
        throw(std::invalid_argument("communicator already registered"));
    }
    auto ret2 = mIdMap.emplace(comm->getID(), comm);
    if (!ret2.second) {
        // removing the successful mStringMap emplace operation the mStringMap emplace success for
        // exception safety
        auto resName = mStringMap.find(comm->getName());
        if (resName != mStringMap.end()) {
            mStringMap.erase(resName);
        }
        throw(std::invalid_argument("communicator already registered"));
    }
}

void communicationsCore::unregisterCommunicator(Communicator* comm)
{
    auto resName = mStringMap.find(comm->getName());
    if (resName != mStringMap.end()) {
        mStringMap.erase(resName);
    }
    auto resId = mIdMap.find(comm->getID());
    if (resId != mIdMap.end()) {
        mIdMap.erase(resId);
    }
}

int communicationsCore::send(std::uint64_t source,
                             std::string_view dest,
                             std::shared_ptr<commMessage> message)
{
    auto res = mStringMap.find(std::string{dest});
    if (res != mStringMap.end()) {
        res->second->receive(source, dest, std::move(message));
        return SEND_SUCCESS;
    }
    return DESTINATION_NOT_FOUND;
}

int communicationsCore::send(std::uint64_t source,
                             std::uint64_t dest,
                             std::shared_ptr<commMessage> message)
{
    auto res = mIdMap.find(dest);
    if (res != mIdMap.end()) {
        res->second->receive(source, dest, std::move(message));
        return SEND_SUCCESS;
    }
    return DESTINATION_NOT_FOUND;
}

std::uint64_t communicationsCore::lookup(std::string_view commName) const
{
    auto res = mStringMap.find(std::string{commName});
    return (res != mStringMap.end()) ? res->second->getID() : 0;
}
std::string communicationsCore::lookup(std::uint64_t did) const
{
    auto res = mIdMap.find(did);
    return (res != mIdMap.end()) ? res->second->getName() : "";
}

}  // namespace griddyn
