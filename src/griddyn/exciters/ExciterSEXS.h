/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../gridDynDefinitions.hpp"
#include "Exciter.h"
#include "core/coreDefinitions.hpp"
#include "solvers/solverMode.hpp"
#include <string>
#include <vector>

namespace griddyn {
namespace exciters {

    /** @brief Simplified excitation system (SEXS) model.
     *
     * This model implements a lead-lag regulator with output limits followed by
     * a first-order field lag.
     */
    class ExciterSEXS: public Exciter {
      protected:
        model_parameter Te = 0.5;  //!< [s] exciter field time constant
        model_parameter Tb = 0.46;  //!< [s] lead-lag denominator time constant

      public:
        explicit ExciterSEXS(const std::string& objName = "exciterSEXS_#");
        coreObject* clone(coreObject* obj = nullptr) const override;

        void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        void dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet) override;

        void set(const std::string& param, const std::string& val) override;
        void set(const std::string& param,
                 double val,
                 units::unit unitType = units::defunit) override;

        stringVec localStateNames() const override;

        void residual(const IOdata& inputs,
                      const stateData& stateData,
                      double resid[],
                      const solverMode& solverMode) override;
        void derivative(const IOdata& inputs,
                        const stateData& stateData,
                        double deriv[],
                        const solverMode& solverMode) override;
        void jacobianElements(const IOdata& inputs,
                              const stateData& stateData,
                              matrixData<double>& matrix,
                              const IOlocs& inputLocs,
                              const solverMode& solverMode) override;

        void rootTest(const IOdata& inputs,
                      const stateData& stateData,
                      double root[],
                      const solverMode& solverMode) override;
        void rootTrigger(coreTime time,
                         const IOdata& inputs,
                         const std::vector<int>& rootMask,
                         const solverMode& solverMode) override;
        change_code rootCheck(const IOdata& inputs,
                              const stateData& stateData,
                              const solverMode& solverMode,
                              check_level_t level) override;

      private:
        double regulatorOutput(const IOdata& inputs, const double stateX) const;
    };

}  // namespace exciters
}  // namespace griddyn
