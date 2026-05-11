/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/** @brief class implementing a derivative
block implementing \f$H(S)=\frac{K s}{1+T_1 s} \frac{1}{1+T_2 s}\f$

*/
class filteredDerivativeBlock: public Block {
  protected:
    model_parameter mT1 = 0.1;  //!< delay time constant for the derivative filtering operation
    model_parameter mT2 = 0.1;  //!< filter on the derivative of block 1
  public:
    //!< default constructor
    explicit filteredDerivativeBlock(const std::string& objName = "filtDerivBlock_#");
    /** alternate constructor to add in the time constant
@param[in] t1  the time constant for the prederivative filter
@param[in] t2 the time constant for the derivative filter
*/
    filteredDerivativeBlock(double t1, double t2, const std::string& objName = "filtDerivBlock_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

  protected:
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // virtual index_t findIndex(const std::string &field, const solverMode &sMode) const;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const stateData& sD,
                                 double deriv[],
                                 const solverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       index_t argLoc,
                                       const solverMode& sMode) override;
    virtual double step(coreTime time, double inputA) override;

    virtual stringVec localStateNames() const override;
    // virtual void setTime(coreTime time){prevTime=time;};
};
}  // namespace griddyn::blocks
