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
  \f[\begin{array}{c|ccc}
  0&&&\\
  \frac{1}{2}&\frac{1}{2}&&\\
  1&-1&2&\\\hline
  (3)&\frac{1}{6}&\frac{2}{3}&\frac{1}{6}
  \end{array}
  \f]
 */
    class Kutta3: public RungeKutta_Explicit {
      public:
        Kutta3(Equation* eq);
        virtual std::string GetName() { return "RK_Kutta_3"; };
    };
}  // namespace griddyn::paradae
