/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_IMPTRAPEZOIDAL_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_IMPTRAPEZOIDAL_H_

#include "RungeKutta_DIRK.h"
#include <string>
namespace griddyn {
namespace paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cc}
  0&&\\
  1&\frac{1}{2}&\frac{1}{2}\\\hline
  (2)&\frac{1}{2}&\frac{1}{2}
  \end{array}
  \f]
 */
    class ImpTrapezoidal: public RungeKutta_DIRK {
      public:
        ImpTrapezoidal(Equation* eq);
        virtual std::string GetName() { return "RK_ImpTrap_2"; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_IMPTRAPEZOIDAL_H_
