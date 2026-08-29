/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>
#include <utility>
#include <vector>

namespace griddyn::blocks {
/**
 * @brief Static piecewise-linear lookup-table block.
 *
 * Given strictly increasing abscissas @f$x_i@f$ and tabulated values @f$y_i@f$,
 * this block evaluates
 * @f[
 *   y = K f(u + b),
 * @f]
 * where @f$f@f$ is linear on each interval @f$[x_i,x_{i+1}]@f$ and is held
 * at the nearest endpoint outside the table domain.  Its analytic input
 * Jacobian is @f$K (y_{i+1}-y_i)/(x_{i+1}-x_i)@f$ in an interval and zero
 * in either endpoint-held region.
 *
 * The table may be set with @c lut (replacing all points), @c element
 * (appending points), or @c file.  A desired-output initialization is
 * available only for a monotonic table with a finite nonzero gain, since a
 * non-monotonic table does not have an unambiguous inverse.
 */
class LutBlock: public GridBlock {
  public:
  private:
    std::vector<std::pair<double, double>> lut;  //!< the lookup table

    struct LookupResult {
        double value;
        double slope;
    };

    /** @brief Check that a table can be evaluated without ambiguity or division by zero. */
    static void validateTable(const std::vector<std::pair<double, double>>& table);
    /** @brief Evaluate the table and its local derivative without mutable solver-side caching. */
    [[nodiscard]] LookupResult evaluate(double input) const;
    /** @brief Return the input that produces a table value for monotonic-table initialization. */
    [[nodiscard]] double inverseValue(double value) const;

  public:
    explicit LutBlock(const std::string& objName = "lutBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // virtual void dynObjectInitializeA (CoreTime time0, std::uint32_t flags);
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void blockAlgebraicUpdate(double input,
                                      const StateData& stateDataValue,
                                      double update[],
                                      const SolverMode& sMode) override;
    // virtual double blockResidual (double input, double didt, const StateData&sD, double
    // resid[], const SolverMode &sMode) override;
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataValue,
                                       MatrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const SolverMode& sMode) override;
    virtual double step(CoreTime time, double input) override;
    // virtual void setTime(CoreTime time){prevTime=time;};
    /** @brief Return the endpoint-clamped, piecewise-linear table value. */
    [[nodiscard]] double computeValue(double input) const;
};
}  // namespace griddyn::blocks
