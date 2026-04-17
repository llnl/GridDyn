/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RungeKutta_Explicit.h"
#include <string>
namespace griddyn::paradae {
/*!
Butcher tableau:
\f[\begin{array}{c|cc}
0&&\\
1&1&\\\hline
(2)&\frac{1}{2}&\frac{1}{2}\\
(1)&1&0
\end{array}
\f]
*/
class FE_ExpTrap_12: public RungeKutta_Explicit {
  public:
    FE_ExpTrap_12(Equation* eq, bool variable_step = false);
    virtual std::string GetName() { return "RK_ExpFE_12"; };
};
}  // namespace griddyn::paradae
