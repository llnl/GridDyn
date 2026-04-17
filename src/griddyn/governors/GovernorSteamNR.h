/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GOVERNOR_IEEE_SIMPLE_H_
#define GOVERNOR_IEEE_SIMPLE_H_
#pragma once

#include "GovernorIeeeSimple.h"
#include <string>

namespace griddyn {
namespace governors {
    class GovernorSteamNR: public GovernorIeeeSimple {
      public:
      protected:
        model_parameter Tch;  //!< [s] steam reheat chest time constant
      public:
        GovernorSteamNR(const std::string& objName = "govSteamNR_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual ~GovernorSteamNR();
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual index_t findIndex(const std::string& field, const solverMode& sMode) const override;

        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;
        // only called if the genModel is not present
        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
    };

}  // namespace governors
}  // namespace griddyn

#endif  // GOVERNOR_IEEE_SIMPLE_H_
