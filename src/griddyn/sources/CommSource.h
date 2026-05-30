/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../comms/CommManager.h"
#include "RampSource.h"
#include <memory>
#include <string>

namespace griddyn {
class Communicator;
class GridSimulation;
namespace sources {
    /** defining a source that can be connected to a communicator*/
    class CommSource: public RampSource {
      protected:
        std::shared_ptr<Communicator> commLink;  //!< communicator link
        GridSimulation* rootSim = nullptr;  //!< pointer to the root simulation
        comms::CommManager cManager;  //!< comm manager object to build and manage the comm link
        model_parameter maxRamp = kBigNum;  //!< the maximum rate of change of the source
      public:
        enum CommSourceFlags {
            useRamp = object_flag3,  //!< indicator that the output should be interpolated
            noMessageReply =
                object_flag4,  //!< indicator that there should be no response to commands
        };
        CommSource(const std::string& objName = "commSource_#");

        CoreObject* clone(CoreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void
            set(std::string_view param, double val, units::unit unitType = units::defunit) override;
        virtual void setFlag(std::string_view flag, bool val) override;

        virtual void setLevel(double val) override;
        virtual void updateA(coreTime time) override;

        /** message processing function for use with communicators
    @param[in] sourceID  the source of the comm message
    @param[in] message the actual message to process
    */
        virtual void receiveMessage(std::uint64_t sourceID, std::shared_ptr<CommMessage> message);
    };

}  // namespace sources
}  // namespace griddyn
