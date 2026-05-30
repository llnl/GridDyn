/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GridSubModel.h"
#include "utilities/MatrixDataSparse.hpp"
#include <string>
#include <vector>
namespace griddyn {
class GridBlock;

/** @brief class implementing a control system built from the defined control blocks*/
class ControlSystem: public GridSubModel {
  protected:
    std::vector<GridBlock*> blocks;  //!< the set of blocks to operate on
    MatrixDataSparse<double> inputMult;  //!< multipliers for the input to the blocks
    MatrixDataSparse<double> outputMult;  //!< multipliers for the outputs
    MatrixDataSparse<double> connections;  //!< multipliers for the block inputs

    std::vector<double> blockOutputs;  //!< current vector of block outputs

  public:
    explicit ControlSystem(const std::string& objName = "control_system_#");
    virtual ~ControlSystem();

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void add(CoreObject* obj) override;
    virtual void add(GridBlock* blk);

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  MatrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& sD,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;
    // virtual void setTime(CoreTime time){prevTime=time;};
};
}  // namespace griddyn
