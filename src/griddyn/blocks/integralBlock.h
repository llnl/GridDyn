/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/** @brief class implementing an integral block
computes the integral of the input
*/
class integralBlock: public Block {
  public:
  protected:
    double iv = 0.0;  //!< the initial value(current value) of the integral
  public:
    //!< default constructor
    explicit integralBlock(const std::string& objName = "integralBlock_#");
    /** alternate constructor to add in the gain
@param[in] gain  the multiplication factor of the block
*/
    integralBlock(double gain, const std::string& objName = "integralBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // virtual index_t findIndex(const std::string &field, const solverMode &sMode) const;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const stateData& sD,
                                 double deriv[],
                                 const solverMode& sMode) override;
    virtual void blockResidual(double input,
                               double didt,
                               const stateData& sD,
                               double resid[],
                               const solverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       index_t argLoc,
                                       const solverMode& sMode) override;
    virtual double step(coreTime time, double inputA) override;
    // virtual void timestep(coreTime time, const IOdata &inputs, const solverMode &sMode);
    // virtual void setTime(coreTime time){prevTime=time;};
};
}  // namespace griddyn::blocks

