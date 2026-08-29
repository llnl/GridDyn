/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/**
 * @file LeadLag.h
 * @brief Stateless first-order lead--lag equations reusable by control models.
 */

#include <cmath>

namespace griddyn::blocks {
/**
 * @brief Stateless equations for a first-order lead--lag section.
 *
 * The kernel realizes
 * @f[
 *   G(s) = K\frac{1 + T_a s}{1 + T_b s}
 * @f]
 * with the lag state @f$x@f$:
 * @f[
 *   T_b\dot{x}=u-x,\qquad
 *   y=K\left[x+\frac{T_a}{T_b}(u-x)\right].
 * @f]
 * Keeping these equations independent of @ref GridBlock lets composite
 * controller models reuse the same realization while retaining ownership of
 * their aggregate solver states, roots, and limiter policy.
 *
 * Some controller specifications instead expose the output itself as the
 * differential state.  For that state @f$y@f$, the equivalent realization is
 * @f[ T_b\dot{y}=u-y+T_a\dot{u}. @f]
 * @ref outputStateDerivative provides that form for models, such as TGOV1,
 * whose published equations use it directly.
 */
class LeadLagKernel {
  public:
    /** Construct a unity-gain lag section with @f$T_a=0@f$ and @f$T_b=1@f$. */
    constexpr LeadLagKernel() = default;
    /** Construct the section with numerator time @p leadTime, denominator time
     * @p lagTime, and gain @p gainValue. */
    constexpr LeadLagKernel(double leadTime, double lagTime, double gainValue = 1.0):
        Ta(leadTime), Tb(lagTime), K(gainValue)
    {
    }

    /**
     * @brief Set all transfer-function parameters.
     *
     * @param[in] leadTime Numerator time constant @f$T_a@f$; it may be
     * negative when a published output-state model requires it.
     * @param[in] lagTime Positive denominator time constant @f$T_b@f$.
     * @param[in] gainValue Transfer-function gain @f$K@f$.
     */
    constexpr void setParameters(double leadTime, double lagTime, double gainValue = 1.0)
    {
        Ta = leadTime;
        Tb = lagTime;
        K = gainValue;
    }
    /** @return true if all parameters are finite and @f$T_b>0@f$. */
    [[nodiscard]] bool isValid() const
    {
        return std::isfinite(Ta) && std::isfinite(Tb) && std::isfinite(K) && (Tb > 0.0);
    }
    /** @return the lead numerator time constant. */
    [[nodiscard]] constexpr double leadTime() const { return Ta; }
    /** @return the lag denominator time constant. */
    [[nodiscard]] constexpr double lagTime() const { return Tb; }
    /** @return the section gain. */
    [[nodiscard]] constexpr double gain() const { return K; }

    /**
     * @brief Evaluate the unbounded lag-state output @f$y@f$.
     * @param[in] input Input @f$u@f$.
     * @param[in] state Lag state @f$x@f$.
     */
    [[nodiscard]] double output(double input, double state) const
    {
        return K * (state + ((Ta / Tb) * (input - state)));
    }
    /**
     * @brief Evaluate the lag-state derivative @f$\dot{x}@f$.
     * @param[in] input Input @f$u@f$.
     * @param[in] state Lag state @f$x@f$.
     */
    [[nodiscard]] double derivative(double input, double state) const
    {
        return (input - state) / Tb;
    }
    /**
     * @brief Evaluate @f$\dot y=(u-y+T_a\dot u)/T_b@f$.
     *
     * This is the equivalent output-state realization, used when a model
     * explicitly owns @f$y@f$ rather than the lag state @f$x@f$.
     * @param[in] input Input @f$u@f$.
     * @param[in] outputState Output state @f$y@f$.
     * @param[in] inputDerivative Input derivative @f$\dot u@f$.
     */
    [[nodiscard]]
    double outputStateDerivative(double input, double outputState, double inputDerivative) const
    {
        return (input - outputState + (Ta * inputDerivative)) / Tb;
    }
    /** @brief Return the output partial derivative with respect to input, @f$\partial y/\partial
     * u@f$. */
    [[nodiscard]] double outputInputJacobian() const { return K * Ta / Tb; }
    /** @brief Return the output partial derivative with respect to lag state, @f$\partial
     * y/\partial x@f$. */
    [[nodiscard]] double outputStateJacobian() const { return K * (1.0 - (Ta / Tb)); }
    /** @brief Return the lag-state derivative partial with respect to input,
     * @f$\partial\dot{x}/\partial u@f$. */
    [[nodiscard]] double derivativeInputJacobian() const { return 1.0 / Tb; }
    /** @brief Return the lag-state derivative partial with respect to state,
     * @f$\partial\dot{x}/\partial x@f$. */
    [[nodiscard]] double derivativeStateJacobian() const { return -1.0 / Tb; }
    /** @return the coefficient of @f$\dot u@f$ in the output-state realization. */
    [[nodiscard]] double outputStateInputDerivativeJacobian() const { return Ta / Tb; }

  private:
    double Ta = 0.0;
    double Tb = 1.0;
    double K = 1.0;
};
}  // namespace griddyn::blocks
