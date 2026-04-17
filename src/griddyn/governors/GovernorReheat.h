/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <string>

namespace griddyn::governors {
    class GovernorReheat: public Governor {
      public:
      protected:
        double T3;  //!< [s]    Transient gain time constant
        double T4;  //!< [s]    Power fraction time constant
        double T5;  //!< [s]    Reheat time constant
      public:
        explicit GovernorReheat(const std::string& objName = "govReheat_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual ~GovernorReheat();
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
        virtual void derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode) override;
        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;

        // virtual void setTime (coreTime time) const{prevTime=time;};
    };

}  // namespace griddyn::governors
