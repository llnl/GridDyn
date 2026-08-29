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
/**
 * @brief Proper single-input, single-output continuous-time transfer-function block.
 *
 * The coefficients are in ascending powers of @f$s@f$:
 * @f[
 *   G(s) = K\frac{b_0 + b_1s + \ldots + b_ns^n}
 *                    {a_0 + a_1s + \ldots + a_ns^n}.
 * @f]
 * The numerator is zero-padded to the denominator order, so the block accepts a
 * strictly proper transfer function as well as one with direct feedthrough.  An
 * improper transfer function is rejected at initialization.
 *
 * For a denominator of order @f$n@f$, GridDyn uses the controllable companion
 * realization @f$\dot{x}_i=x_{i+1}@f$ for @f$i<n-1@f$ and
 * @f$\dot{x}_{n-1}=u-\sum_{i=0}^{n-1}(a_i/a_n)x_i@f$.  With
 * @f$d=b_n/a_n@f$, the unbounded output is
 * @f$y=K[d u+\sum_{i=0}^{n-1}(b_i/a_n-d a_i/a_n)x_i]@f$.
 * This realization gives an analytic Jacobian and makes the output an
 * algebraic state, including when @f$d=0@f$.
 *
 * A configured GridBlock output limiter clamps the exposed output while the
 * companion states continue to evolve.  Rate limits are intentionally not
 * applied because the exposed transfer-function output is algebraic.
 */
class TransferFunctionBlock: public GridBlock {
  public:
  protected:
    std::vector<double> a;  //!< denominator coefficients, ascending powers of s
    std::vector<double> b;  //!< numerator coefficients, ascending powers of s
  public:
    /** constructor to add in the order of the transfer function
@param[in] order  the order of the transfer function
*/
    explicit TransferFunctionBlock(int order = 1);

    /** constructor to add in the name of the block
@param[in] objName  the name
*/
    explicit TransferFunctionBlock(const std::string& objName);
    /** constructor to define the transfer function coefficients assuming $b_0=1$ and all others
are 0
@param[in] acoef the denominator coefficients
*/
    explicit TransferFunctionBlock(std::vector<double> acoef);
    /** constructor to define the transfer function coefficients
@param[in] acoef the denominator coefficients
@param[in] bcoef the numerator coefficients
*/
    TransferFunctionBlock(std::vector<double> acoef, std::vector<double> bcoef);
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const StateData& stateDataValue,
                                 double deriv[],
                                 const SolverMode& sMode) override;
    virtual void blockAlgebraicUpdate(double input,
                                      const StateData& stateDataValue,
                                      double update[],
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
                                       index_t inputLocation,
                                       const SolverMode& sMode) override;
    virtual double step(CoreTime time, double inputValue) override;
    virtual stringVec localStateNames() const override;

    /**
     * @brief Evaluate the unbounded transfer function for state owned by a parent controller.
     *
     * This permits a composite controller to reuse the same companion realization
     * while retaining ownership of its aggregate solver state and nonlinear limits.
     */
    [[nodiscard]] double externalStateOutput(double input, const double state[]) const;
    /** @brief Evaluate companion-state derivatives for state owned by a parent controller. */
    void externalStateDerivative(double input, const double state[], double derivative[]) const;

  private:
    [[nodiscard]] index_t order() const;
    void validateCoefficients();
    [[nodiscard]] double rawOutput(double input, const double state[]) const;
    void stateDerivative(double input, const double state[], double derivative[]) const;
};
}  // namespace griddyn::blocks
