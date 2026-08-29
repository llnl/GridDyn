/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/**
 * @brief Differential integrator with optional GridBlock limits.
 *
 * For output state @f$y@f$, the governing equation is
 * @f$\dot y=K(u+b)@f$.  `iv`/`initial_value` specifies the state used when
 * no desired output is supplied; `t` is accepted as an inverse gain, setting
 * @f$K=1/t@f$.  Inherited value and ramp limits use GridBlock's root and
 * anti-windup behavior.  The local stepping path uses trapezoidal input
 * integration.
 */
class IntegralBlock: public GridBlock {
  public:
  protected:
    double iv = 0.0;  //!< the initial value(current value) of the integral
  public:
    //!< default constructor
    explicit IntegralBlock(const std::string& objName = "integralBlock_#");
    /** alternate constructor to add in the gain
@param[in] gain  the multiplication factor of the block
*/
    IntegralBlock(double gain, const std::string& objName = "integralBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // virtual index_t findIndex(const std::string &field, const SolverMode &sMode) const;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const StateData& stateDataValue,
                                 double deriv[],
                                 const SolverMode& sMode) override;
    virtual void blockResidual(double input,
                               double didt,
                               const StateData& stateDataValue,
                               double resid[],
                               const SolverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataValue,
                                       MatrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const SolverMode& sMode) override;
    virtual double step(CoreTime time, double inputA) override;
    // virtual void timestep(CoreTime time, const IOdata &inputs, const SolverMode &sMode);
    // virtual void setTime(CoreTime time){prevTime=time;};
};
}  // namespace griddyn::blocks
