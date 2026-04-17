#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Block.h"
#include <string>
#include <vector>

namespace griddyn::blocks {
    /** @brief class defining a null block  meaning input==output
     */
    class nullBlock final: public Block {
      public:
        /** @brief default constructor*/
        explicit nullBlock(const std::string& objName = "nullblock_#");

        virtual coreObject* clone(coreObject* obj = nullptr) const override;

      protected:
        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

      public:
        virtual void setFlag(const std::string& flag, bool val) override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual double get(const std::string& param,
                           units::unit unitType = units::defunit) const override;

        virtual void blockResidual(double input,
                                   double didt,
                                   const stateData& sD,
                                   double resid[],
                                   const solverMode& sMode) override;

        virtual void blockDerivative(double input,
                                     double didt,
                                     const stateData& sD,
                                     double deriv[],
                                     const solverMode& sMode) override;

        virtual void blockAlgebraicUpdate(double input,
                                          const stateData& sD,
                                          double update[],
                                          const solverMode& sMode) override;

        virtual void blockJacobianElements(double input,
                                           double didt,
                                           const stateData& sD,
                                           matrixData<double>& md,
                                           index_t argLoc,
                                           const solverMode& sMode) override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        virtual double step(coreTime time, double input) override;
        virtual void rootTest(const IOdata& inputs,
                              const stateData& sD,
                              double roots[],
                              const solverMode& sMode) override;
        virtual void rootTrigger(coreTime time,
                                 const IOdata& inputs,
                                 const std::vector<int>& rootMask,
                                 const solverMode& sMode) override;
        virtual change_code rootCheck(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode,
                                      check_level_t level) override;
        // virtual void setTime(coreTime time){prevTime=time;};

        virtual double getBlockOutput(const stateData& sD, const solverMode& sMode) const override;
        virtual double getBlockOutput() const override;
        virtual double getBlockDoutDt(const stateData& sD, const solverMode& sMode) const override;
        virtual double getBlockDoutDt() const override;
    };

}  // namespace griddyn::blocks
