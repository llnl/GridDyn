/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridSubModel.h"
#include "utilities/matrixDataSparse.hpp"
#include <string>
#include <vector>
namespace griddyn {
class Block;

/** @brief class implementing a control system built from the defined control blocks*/
class controlSystem: public GridSubModel {
  protected:
    std::vector<Block*> blocks;  //!< the set of blocks to operate on
    matrixDataSparse<double> inputMult;  //!< multipliers for the input to the blocks
    matrixDataSparse<double> outputMult;  //!< multipliers for the outputs
    matrixDataSparse<double> connections;  //!< multipliers for the block inputs

    std::vector<double> blockOutputs;  //!< current vector of block outputs

  public:
    explicit controlSystem(const std::string& objName = "control_system_#");
    virtual ~controlSystem();

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void add(CoreObject* obj) override;
    virtual void add(Block* blk);

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

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
};

}  // namespace griddyn
