/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/**
 * @file DelayBlock.h
 * @brief First-order lag (measurement or transport-delay approximation) block.
 */

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/**
 * @brief First-order lag block.
 *
 * This block realizes the proper transfer function
 * @f[
 *     H(s)=\frac{K}{1+T_1s}.
 * @f]
 * For input @f$u@f$, GridBlock bias @f$b@f$, and differential output state
 * @f$y@f$, the DAE residual, derivative, and analytic Jacobian use
 * @f[
 *     T_1\dot y=K(u+b)-y.
 * @f]
 * Thus it is the existing GridDyn first-order lag/transducer primitive; it
 * does not include a lead numerator time constant.  At an equilibrium,
 * @f$y=K(u+b)@f$, so desired-output initialization back-solves this relation.
 *
 * Parameters `t1` and `t` set @f$T_1@f$; inherited `k`/`gain`, `bias`, and
 * output-limit parameters retain their GridBlock meanings.  A time constant
 * below the GridDyn numerical-resolution threshold selects the historical
 * simplified gain mode.  The solver path continues to use the equation above;
 * the separate @ref step path uses the legacy local integration routine and is
 * therefore not an exact sampled-data discretization.
 */
class DelayBlock: public GridBlock {
  public:
  protected:
    model_parameter mT1 = 0.1;  //!< Lag denominator time constant @f$T_1@f$.
  public:
    /** @brief Construct a unity-gain lag with @f$T_1=0.1@f$. */
    explicit DelayBlock(const std::string& objName = "delayBlock_#");
    /**
     * @brief Construct a unity-gain lag.
     * @param[in] timeConstant Lag time constant @f$T_1@f$.
     * @param[in] objName Name of the block.
     */
    DelayBlock(double timeConstant, const std::string& objName = "delayBlock_#");
    /**
     * @brief Construct a lag with explicit gain.
     * @param[in] timeConstant Lag time constant @f$T_1@f$.
     * @param[in] gainValue Steady-state gain @f$K@f$.
     * @param[in] objName Name of the block.
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
