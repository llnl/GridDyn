#pragma once

/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta_Explicit.h"
#include <string>
namespace griddyn::paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cccc}
  0&&&&\\
  \frac{1}{2}&\frac{1}{2}&&&\\
  \frac{1}{2}&0&\frac{1}{2}&&\\
  1&0&0&1&\\\hline
  (4)&\frac{1}{6}&\frac{1}{3}&\frac{1}{3}&\frac{1}{6}
  \end{array}
  \f]
 */
    class Kutta4: public RungeKutta_Explicit {
      public:
        Kutta4(Equation* eq);
        virtual std::string GetName() { return "RK_Kutta_4"; };
    };
}  // namespace griddyn::paradae
