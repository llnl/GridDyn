/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RALSTON_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RALSTON_H_

#include "RungeKutta_Explicit.h"
#include <string>
namespace griddyn {
namespace paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cc}
  0&&\\
  \frac{2}{3}&\frac{2}{3}&\\\hline
  (2)&\frac{1}{4}&\frac{3}{4}
  \end{array}
  \f]
 */
    class Ralston: public RungeKutta_Explicit {
      public:
        Ralston(Equation* eq);
        virtual std::string GetName() { return "RK_Ralston_2"; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RALSTON_H_
