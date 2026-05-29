/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>
#include <vector>

namespace griddyn::blocks {
/** @brief lookup table block*/
class LutBlock: public GridBlock {
  public:
  private:
    std::vector<std::pair<double, double>> lut;  //!< the lookup table
    double b = 0;  //!< the intercept of the interpolation function of the current lookup section
    double m = 0;  //!< the slope of the interpolation function of the current lookup section
    double vlower = -kBigNum;  //!< the lower value of the current lookup table section
    double vupper = kBigNum;  //!< the upper value of the current lookup table section
    int lindex = -1;  //!< the index of the current lookup table section
    // NOTE: extra 4 bytes here
  public:
    explicit LutBlock(const std::string& objName = "lutBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // virtual void dynObjectInitializeA (coreTime time0, std::uint32_t flags);
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void blockAlgebraicUpdate(double input,
                                      const StateData& stateDataValue,
                                      double update[],
                                      const SolverMode& sMode) override;
    // virtual double blockResidual (double input, double didt, const StateData&sD, double
    // resid[], const SolverMode &sMode) override;
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataValue,
                                       matrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const SolverMode& sMode) override;
    virtual double step(coreTime time, double input) override;
    // virtual void setTime(coreTime time){prevTime=time;};
    double computeValue(double input);
};
}  // namespace griddyn::blocks
