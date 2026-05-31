/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/** @brief class implementing a delay block
block implementing \f$H(S)=\frac{K}{1+T_1 s}\f$
if the time constant is very small it reverts to the basic block
*/
class DelayBlock: public GridBlock {
  public:
  protected:
    model_parameter mT1 = 0.1;  //!< the time constant
  public:
    //!< default constructor
    explicit DelayBlock(const std::string& objName = "delayBlock_#");
    /** alternate constructor to add in the time constant
@param[in] timeConstant  the time constant
@param[in] objName the name of the block
*/
    DelayBlock(double timeConstant, const std::string& objName = "delayBlock_#");
    /** alternate constructor to add in the time constant
@param[in] timeConstant  the time constant
@param[in] gainValue the block gain
@param[in] objName the name of the object
*/
    DelayBlock(double timeConstant, double gainValue, const std::string& objName = "delayBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

  protected:
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // virtual index_t findIndex(const std::string &field, const SolverMode &sMode) const;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const StateData& stateDataRef,
                                 double deriv[],
                                 const SolverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataRef,
                                       MatrixData<double>& jacobian,
                                       index_t argLoc,
                                       const SolverMode& sMode) override;
    virtual double step(CoreTime time, double inputA) override;
    // virtual void setTime(CoreTime time){prevTime=time;};
};

}  // namespace griddyn::blocks
