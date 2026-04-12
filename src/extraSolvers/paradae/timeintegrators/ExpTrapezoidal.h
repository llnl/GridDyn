/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "RungeKutta_Explicit.h"
#include <string>
namespace griddyn {
namespace paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cc}
  0&&\\
  1&1&\\\hline
  (2)&\frac{1}{2}&\frac{1}{2}
  \end{array}
  \f]
 */
    class ExpTrapezoidal: public RungeKutta_Explicit {
      public:
        ExpTrapezoidal(Equation* eq);
        virtual std::string GetName() { return "RK_ExpTrap_2"; };
    };
}  // namespace paradae
}  // namespace griddyn
