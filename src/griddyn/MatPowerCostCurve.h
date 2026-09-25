/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace griddyn {
class Generator;

/** MATPOWER/PYPOWER cost curve data held by optimization models. */
struct MatPowerCostCurve {
    int model = 0;  //!< 1 = piecewise linear, 2 = polynomial.
    double startupCost = 0.0;
    double shutdownCost = 0.0;
    std::vector<double> coefficients;  //!< Source-file order; MW/MVAr and cost units.

    bool present() const
    {
        return model != 0 || startupCost != 0.0 || shutdownCost != 0.0 || !coefficients.empty();
    }

    bool valid() const
    {
        if (!std::isfinite(startupCost) || !std::isfinite(shutdownCost)) {
            return false;
        }
        for (const auto coefficient : coefficients) {
            if (!std::isfinite(coefficient)) {
                return false;
            }
        }
        if (model == 2) {
            return !coefficients.empty();
        }
        if (model != 1 || coefficients.size() < 4 || (coefficients.size() % 2 != 0)) {
            return false;
        }
        for (std::size_t index = 2; index < coefficients.size(); index += 2) {
            if (coefficients[index] <= coefficients[index - 2]) {
                return false;
            }
        }
        return true;
    }
};

/** Optional optimization-side access to source-format cost data for case export. */
class MatPowerCostCurveProvider {
  public:
    virtual ~MatPowerCostCurveProvider() = default;

    virtual const MatPowerCostCurve* matPowerCostCurve(const Generator* generator,
                                                       bool reactive = false) const = 0;
};
}  // namespace griddyn
