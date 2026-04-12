/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_SDIRK_12_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_SDIRK_12_H_

#include "RungeKutta_SDIRK.h"
#include <string>
namespace griddyn {
namespace paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cc}
  1&1&\\
  0&-1&1\\\hline
  (2)&\frac{1}{2}&\frac{1}{2}\\
  (1)&1&0
  \end{array}
  \f]
 */
    class SDIRK_12: public RungeKutta_SDIRK {
      public:
        SDIRK_12(Equation* eq, bool variable_step = false);
        virtual std::string GetName() { return "RK_SDIRK_12"; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_SDIRK_12_H_
