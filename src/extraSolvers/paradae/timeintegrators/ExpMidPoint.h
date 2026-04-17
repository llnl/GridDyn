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
\frac{1}{2}&\frac{1}{2}&\\\hline
(2)&0&1
\end{array}
\f]
*/
class ExpMidPoint: public RungeKutta_Explicit {
  public:
    ExpMidPoint(Equation* eq);
    virtual std::string GetName() { return "RK_ExpMidPoint_2"; };
};
}  // namespace griddyn::paradae
