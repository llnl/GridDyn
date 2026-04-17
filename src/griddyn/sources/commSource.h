/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_COMMSOURCE_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_COMMSOURCE_H_

#include "../comms/commManager.h"
#include "rampSource.h"
#include <memory>
#include <string>

namespace griddyn {
class Communicator;
class gridSimulation;
namespace sources {
    /** defining a source that can be connected to a communicator*/
    class commSource: public rampSource {
      protected:
        std::shared_ptr<Communicator> commLink;  //!< communicator link
        gridSimulation* rootSim = nullptr;  //!< pointer to the root simulation
        comms::commManager cManager;  //!< comm manager object to build and manage the comm link
        model_parameter maxRamp = kBigNum;  //!< the maximum rate of change of the source
      public:
        enum commSourceFlags {
            useRamp = object_flag3,  //!< indicator that the output should be interpolated
            no_message_reply =
                object_flag4,  //!< indicator that there should be no response to commands
        };
        commSource(const std::string& objName = "commSource_#");

        coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual void setFlag(const std::string& flag, bool val) override;

        virtual void setLevel(double val) override;
        virtual void updateA(coreTime time) override;

        /** message processing function for use with communicators
    @param[in] sourceID  the source of the comm message
    @param[in] message the actual message to process
    */
        virtual void receiveMessage(std::uint64_t sourceID, std::shared_ptr<commMessage> message);
    };

}  // namespace sources
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_COMMSOURCE_H_
