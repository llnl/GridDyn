#pragma once

/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta_Implicit.h"
#include <string>
namespace griddyn::paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cc}
  \frac{1}{3}&\frac{5}{12}&-\frac{1}{12}\\
  1&\frac{3}{4}&\frac{1}{4}\\\hline
  (3)&\frac{3}{4}&\frac{1}{4}
  \end{array}
  \f]
 */
    class Radau3: public RungeKutta_Implicit {
      public:
        Radau3(Equation* eq);
        virtual std::string GetName() { return "RK_Radau_3"; };
    };
}  // namespace griddyn::paradae
