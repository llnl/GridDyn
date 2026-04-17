/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../griddyn/sources/rampSource.h"
#include "helicsSupport.h"
#include <string>
namespace griddyn::helicsLib {
    class helicsCoordinator;

    /** class defining an object that pulls in data from a helics co-simulation*/
    class helicsSource: public sources::rampSource {
      public:
        enum helics_source_flags {
            use_ramp = object_flag8,
            predictive_ramp = object_flag9,
            initial_query = object_flag10,
        };

      protected:
        std::string valKey;  //!< time series containing the load information
        model_parameter scaleFactor = 1.0;  //!< scaling factor on the load
        units::unit inputUnits = units::defunit;  //!< units of the incoming data
        units::unit outputUnits = units::defunit;  //!< units of the outgoing data
        helics::data_type valueType;  //!< the type of value that is used through helics
        int32_t valueIndex = -1;  //!< the index into the helics coordinator
        helicsCoordinator* coord_ = nullptr;  //!< pointer to the helics coordinator
        int32_t elementIndex = 0;  //!< index into a vector from HELICS
      private:
        double prevVal = 0;

      public:
        explicit helicsSource(const std::string& objName = "helicsSource_$");

        coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, uint32_t flags) override;
        virtual void pFlowObjectInitializeB() override;
        virtual void dynObjectInitializeA(coreTime time0, uint32_t flags) override;

        virtual void updateA(coreTime time) override;
        virtual void
            timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode) override;
        virtual void setFlag(const std::string& param, bool val = true) override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

      private:
        /** update the publication and subscription info in the helics coordinator*/
        void updateSubscription();
    };

}  // namespace griddyn::helicsLib
