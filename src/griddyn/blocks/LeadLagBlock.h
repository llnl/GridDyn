/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/**
 * @file LeadLagBlock.h
 * @brief GridBlock implementation of a first-order lead--lag transfer function.
 */

#include "../Block.h"
#include "LeadLag.h"
#include <string>

namespace griddyn::blocks {
/**
 * @brief GridBlock wrapper for a first-order lead--lag section.
 *
 * @f[
 * G(s)=K\frac{1+T_a s}{1+T_b s},\qquad
 * T_b\dot{x}=u+b-x,\qquad
 * y=K\left[x+\frac{T_a}{T_b}(u+b-x)\right].
 * @f]
 *
 * The output is algebraic and the lag state is differential.  Configured
 * GridBlock output limits apply to the exposed output only; they do not alter
 * the linear section's state equation.  A non-positive denominator time is
 * invalid rather than silently changing this block into a gain, because that
 * would change its state and solver contract after construction.
 *
 * The block may be created through the `block` factory as `leadlag` or
 * `lead_lag`.  Parameters `ta` and `tb` set @f$T_a@f$ and @f$T_b@f$;
 * `t` aliases `tb`; inherited `k`/`gain` and `bias` set @f$K@f$ and @f$b@f$.
 * At equilibrium, @f$x=u+b@f$ and @f$y=K(u+b)@f$.  Initialization from a
 * desired output back-solves that steady-state relation.  The local
 * @ref step method uses the exact zero-order-hold solution for @f$x@f$;
 * solver execution uses the residual and analytic Jacobian above.
 */
class LeadLagBlock: public GridBlock {
  public:
    /** @brief Construct a unity-gain block with @f$T_a=0@f$, @f$T_b=1@f$. */
    explicit LeadLagBlock(const std::string& objName = "leadLagBlock_#");
    /**
     * @brief Construct a unity-gain lead--lag block.
     * @param[in] lagTime Denominator time constant @f$T_b@f$.
     * @param[in] leadTime Numerator time constant @f$T_a@f$.
     * @param[in] objName Name of the block.
     */
    LeadLagBlock(double lagTime, double leadTime, const std::string& objName = "leadLagBlock_#");
    /**
     * @brief Construct a lead--lag block with explicit gain.
     * @param[in] lagTime Denominator time constant @f$T_b@f$.
     * @param[in] leadTime Numerator time constant @f$T_a@f$.
     * @param[in] gainValue Transfer-function gain @f$K@f$.
     * @param[in] objName Name of the block.
     */
    LeadLagBlock(double lagTime,
                 double leadTime,
                 double gainValue,
                 const std::string& objName = "leadLagBlock_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void blockDerivative(double input,
                         double didt,
                         const StateData& stateDataValue,
                         double deriv[],
                         const SolverMode& solverModeValue) override;
    void blockAlgebraicUpdate(double input,
                              const StateData& stateDataValue,
                              double update[],
                              const SolverMode& solverModeValue) override;
    void blockJacobianElements(double input,
                               double didt,
                               const StateData& stateDataValue,
                               MatrixData<double>& matrixDataValue,
                               index_t argLoc,
                               const SolverMode& solverModeValue) override;
    double step(CoreTime time, double input) override;
    stringVec localStateNames() const override;

  private:
    LeadLagKernel section;
    void validateParameters() const;
};
}  // namespace griddyn::blocks
