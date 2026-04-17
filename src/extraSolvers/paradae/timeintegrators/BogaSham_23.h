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
  \frac{3}{4}&0&\frac{3}{4}&&\\
  1&\frac{2}{9}&\frac{1}{3}&\frac{4}{9}&\\\hline
  (3)&\frac{2}{9}&\frac{1}{3}&\frac{4}{9}&0\\
  (2)&\frac{7}{24}&\frac{1}{4}&\frac{1}{3}&\frac{1}{8}
  \end{array}
  \f]
 */
    class BogaSham_23: public RungeKutta_Explicit {
      public:
        BogaSham_23(Equation* eq, bool variable_step = false);
        virtual std::string GetName() { return "RK_ExpBS_23"; };
    };
}  // namespace griddyn::paradae
