/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RungeKutta_DIRK.h"
#include <string>
namespace griddyn::paradae {
/*!
Butcher tableau:
\f[\begin{array}{c|cccc}
1&1&&&\\
1&1&&&\\
\frac{4}{9}&-\frac{88}{45}&0&\frac{12}{5}&\\
\frac{1}{3}&-\frac{407}{75}&0&\frac{144}{25}&\\\hline
(2)&\frac{1}{10}&0&\frac{9}{10}&0\\
(1)&1&0&0&0
\end{array}
\f]
*/
class ImpVarUnk_12: public RungeKutta_DIRK {
  public:
    ImpVarUnk_12(Equation* eq, bool variable_step = false);
    virtual std::string GetName() { return "RK_Imp_12"; };
};
}  // namespace griddyn::paradae
