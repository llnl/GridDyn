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
  \f[\begin{array}{c|c}
  0&\\\hline
  (1)&1
  \end{array}
  \f]
 */
    class ForwardEuler: public RungeKutta_Explicit {
      public:
        ForwardEuler(Equation* eq);
        virtual std::string GetName() { return "RK_FEuler_1"; };
    };
}  // namespace griddyn::paradae
