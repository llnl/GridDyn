/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/** @brief class implementing a control block
block implementing \f$H(S)=\frac{K(1+T_2 s}{1+T_1 s}\f$
default is \f$T_2 =0\f$ for behavior equivalent to a delay block
if T1 is 0 it behaves like the basic block
*/
class controlBlock: public Block {
  public:
  protected:
    model_parameter mT1 = 0.1;  //!< delay time constant
    model_parameter mT2 = 0.0;  //!< upper time constant
  public:
    //!< default constructor
    explicit controlBlock(const std::string& objName = "controlBlock_#");
    /** alternate constructor to add in the time constant
@param[in] timeConstant  the time constant
@param[in] objName the name of the block
*/
    controlBlock(double timeConstant,
                 const std::string& objName = "controlBlock_#");  // convert to the equivalent
                                                                  // of a delay block with t2=0;
    /** alternate constructor to add in the time constant
@param[in] timeConstant  the time constant
@param[in] upperTimeConstant the upper time constant
@param[in] objName the name of the block
*/
    controlBlock(double timeConstant,
                 double upperTimeConstant,
                 const std::string& objName = "controlBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const override;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const stateData& stateDataRef,
                                 double deriv[],
                                 const solverMode& sMode) override;
    virtual void blockAlgebraicUpdate(double input,
                                      const stateData& stateDataRef,
                                      double update[],
                                      const solverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& stateDataRef,
                                       matrixData<double>& jacobian,
                                       index_t argLoc,
                                       const solverMode& sMode) override;
    virtual double step(coreTime time, double input) override;
    // virtual void setTime(coreTime time){prevTime=time;};
    virtual stringVec localStateNames() const override;
};

}  // namespace griddyn::blocks

