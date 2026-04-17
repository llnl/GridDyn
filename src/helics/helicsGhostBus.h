/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../griddyn/gridBus.h"
#include <string>
namespace griddyn::helicsLib {
    class helicsCoordinator;
    /** class meant to implement a Ghost Bus
@details the bus gets its voltage from another simulation, otherwise it acts pretty much like an
infinite bus*/
    class helicsGhostBus: public gridBus {
      protected:
        std::string voltageKey;  //!< the key to send voltage
        std::string loadKey;  //!< time series containing the load information
        int32_t voltageIndex;  //!< reference indices for the voltage
        int32_t loadIndex;  //!< reference indices for the load
        units::unit outUnits = units::defunit;
        helicsCoordinator* coord_ = nullptr;

      public:
        explicit helicsGhostBus(const std::string& objName = "helicsGhostbus_$");

        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, uint32_t flags) override;
        virtual void pFlowObjectInitializeB() override;

        virtual void updateA(coreTime time) override;
        virtual coreTime updateB() override;
        virtual void
            timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode) override;
        virtual void setFlag(const std::string& param, bool val = true) override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

      private:
        void updateSubscription();
    };

}  // namespace griddyn::helicsLib
