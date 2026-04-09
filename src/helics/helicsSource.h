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

#include "../griddyn/sources/rampSource.h"
#include "helicsSupport.h"
#include <string>
namespace griddyn {
namespace helicsLib {
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

}  // namespace helicsLib
}  // namespace griddyn
