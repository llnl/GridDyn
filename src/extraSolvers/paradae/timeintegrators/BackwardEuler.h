/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RungeKutta_DIRK.h"
#include <string>
namespace griddyn::paradae {
/*!
Butcher tableau:
\f[\begin{array}{c|c}
1&1\\\hline
(1)&1
\end{array}
\f]
*/
class BackwardEuler: public RungeKutta_DIRK {
  public:
    BackwardEuler(Equation* eq);
    virtual std::string GetName() { return "RK_BEuler_1"; };
};
}  // namespace griddyn::paradae
