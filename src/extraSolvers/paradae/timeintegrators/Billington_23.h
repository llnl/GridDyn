/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RungeKutta_SDIRK.h"
#include <string>

namespace griddyn::paradae {
    /*!
  Butcher tableau:
  \f[\begin{array}{c|ccc}
  1-\frac{\sqrt{2}}{2}&1-\frac{\sqrt{2}}{2}&&\\
  -18+27\frac{\sqrt{2}}{2}&-19+14\sqrt{2}&1-\frac{\sqrt{2}}{2}&\\
  2-\frac{\sqrt{2}}{2}&\frac{53-5\sqrt{2}}{62}&\frac{9+5\sqrt{2}}{62}&1-\frac{\sqrt{2}}{2}\\\hline
  (3)&\frac{263-95\sqrt{2}}{186}&\frac{47+33\sqrt{2}}{186}&\frac{-2+\sqrt{2}}{3}\\
  (2)&\frac{53-5\sqrt{2}}{62}&\frac{9+5\sqrt{2}}{62}&
  \end{array}
  \f]
 */
    class Billington_23: public RungeKutta_SDIRK {
      public:
        Billington_23(Equation* eq, bool variable_step = false);
        virtual std::string GetName() { return "RK_ImpBi_23"; };
    };
}  // namespace griddyn::paradae
