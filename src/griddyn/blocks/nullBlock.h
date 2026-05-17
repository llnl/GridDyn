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
/** @brief class defining a null block  meaning input==output
 */
class nullBlock final: public Block {
  public:
    /** @brief default constructor*/
    explicit nullBlock(const std::string& objName = "nullblock_#");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

  protected:
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void setFlag(std::string_view flag, bool val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void blockResidual(double input,
                               double didt,
                               const stateData& stateDataValue,
                               double resid[],
                               const solverMode& solverModeValue) override;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const stateData& stateDataValue,
                                 double deriv[],
                                 const solverMode& solverModeValue) override;

    virtual void blockAlgebraicUpdate(double input,
                                      const stateData& stateDataValue,
                                      double update[],
                                      const solverMode& solverModeValue) override;

    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& stateDataValue,
                                       matrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const solverMode& solverModeValue) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    virtual double step(coreTime time, double input) override;
    virtual void rootTest(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double roots[],
                          const solverMode& solverModeValue) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& solverModeValue) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  const solverMode& solverModeValue,
                                  check_level_t level) override;
    // virtual void setTime(coreTime time){prevTime=time;};

    virtual double getBlockOutput(const stateData& stateDataValue,
                                  const solverMode& solverModeValue) const override;
    virtual double getBlockOutput() const override;
    virtual double getBlockDoutDt(const stateData& stateDataValue,
                                  const solverMode& solverModeValue) const override;
    virtual double getBlockDoutDt() const override;
};

}  // namespace griddyn::blocks
