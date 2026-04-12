/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LUT_BLOCK_H_
#define LUT_BLOCK_H_
#pragma once

#include "../Block.h"
#include <string>
#include <vector>

namespace griddyn {
namespace blocks {
    /** @brief lookup table block*/
    class lutBlock: public Block {
      public:
      private:
        std::vector<std::pair<double, double>> lut;  //!< the lookup table
        double b =
            0;  //!< the intercept of the interpolation function of the current lookup section
        double m = 0;  //!< the slope of the interpolation function of the current lookup section
        double vlower = -kBigNum;  //!< the lower value of the current lookup table section
        double vupper = kBigNum;  //!< the upper value of the current lookup table section
        int lindex = -1;  //!< the index of the current lookup table section
        // NOTE: extra 4 bytes here
      public:
        explicit lutBlock(const std::string& objName = "lutBlock_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        // virtual void dynObjectInitializeA (coreTime time0, std::uint32_t flags);
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void blockAlgebraicUpdate(double input,
                                          const stateData& sD,
                                          double update[],
                                          const solverMode& sMode) override;
        // virtual double blockResidual (double input, double didt, const stateData &sD, double
        // resid[], const solverMode &sMode) override;
        virtual void blockJacobianElements(double input,
                                           double didt,
                                           const stateData& sD,
                                           matrixData<double>& md,
                                           index_t argLoc,
                                           const solverMode& sMode) override;
        virtual double step(coreTime time, double input) override;
        // virtual void setTime(coreTime time){prevTime=time;};
        double computeValue(double input);
    };
}  // namespace blocks
}  // namespace griddyn
#endif  // LUT_BLOCK_H_
