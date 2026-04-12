/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace griddyn {
class Communicator;
class propertyBuffer;
class commMessage;

namespace comms {
    /** class for build and maintaining communicators*/
    class commManager {
      private:
        std::string commName;  //!< The name to use on the commlink
        std::uint64_t commId = 0;  //!< the id to use on the commlink
        std::string commType;  //!< the type of comms to construct
        std::string commDestName;  //!< the default communication destination as a string
        std::uint64_t commDestId = 0;  //!< the default communication destination id

        std::shared_ptr<Communicator> commLink;  //!< communicator link
        std::unique_ptr<propertyBuffer> commPropBuffer;  //!< a property buffer for the communicator

      public:
        commManager();
        commManager(const commManager& cm);
        commManager(commManager&& cm);
        ~commManager();

        commManager& operator=(const commManager& cm);
        commManager& operator=(commManager&& cm);

        bool set(const std::string& param, const std::string& val);
        bool set(const std::string& param, double val);
        bool setFlag(const std::string& flag, bool val);
        std::shared_ptr<Communicator> build();
        std::shared_ptr<Communicator> getCommLink() const { return commLink; }

        void send(std::shared_ptr<commMessage> m) const;

        const std::string& destName() const { return commDestName; }
        const std::string& getName() const { return commName; }
        void setName(const std::string& name);
        std::uint64_t id() const { return commId; }
        std::uint64_t destId() const { return commDestId; }
    };
}  // namespace comms
}  // namespace griddyn
