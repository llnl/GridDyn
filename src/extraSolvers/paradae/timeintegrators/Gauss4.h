/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_GAUSS4_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_GAUSS4_H_

#include "RungeKutta_Implicit.h"
#include <string>
namespace griddyn {
namespace paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|cc}
  \frac{1}{2}-\frac{\sqrt{3}}{6}&\frac{1}{4}&\frac{1}{4}-\frac{\sqrt{3}}{6}\\
  \frac{1}{2}+\frac{\sqrt{3}}{6}&\frac{1}{4}+\frac{\sqrt{3}}{6}&\frac{1}{4}\\\hline
  (4)&\frac{1}{2}&\frac{1}{2}
  \end{array}
  \f]
 */
    class Gauss4: public RungeKutta_Implicit {
      public:
        Gauss4(Equation* eq);
        virtual std::string GetName() { return "RK_Gauss_4"; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_GAUSS4_H_
