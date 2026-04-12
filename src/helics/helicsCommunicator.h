/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#pragma once

#include "griddyn/comms/Communicator.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
namespace helicsLib {

    class helicsCoordinator;

    /** class defining a Griddyn coordinator to communicate through HELICS*/
    class helicsCommunicator:
        public griddyn::Communicator

    {
      public:
        helicsCommunicator() = default;
        explicit helicsCommunicator(const std::string& name);
        helicsCommunicator(const std::string& m_name, std::uint64_t id);

        virtual ~helicsCommunicator() = default;

        virtual void transmit(const std::string& destName,
                              std::shared_ptr<griddyn::commMessage> message) override;

        virtual void transmit(std::uint64_t destID,
                              std::shared_ptr<griddyn::commMessage> message) override;

        virtual void initialize() override;  //!< XXX: Must be called by client
        virtual void disconnect() override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param, double val) override;

      private:
        std::string target;
        std::string coordName;
        helicsCoordinator* coord = nullptr;
        int32_t index = 0;
    };

}  // namespace helicsLib
}  // namespace griddyn
