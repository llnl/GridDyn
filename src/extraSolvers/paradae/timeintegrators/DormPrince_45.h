/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RungeKutta_Explicit.h"
#include <string>
namespace griddyn::paradae {
/*!
Butcher tableau:
\f[\begin{array}{c|ccccccc}
0&&&&&&&\\
\frac{1}{5}&\frac{1}{5}&&&&&&\\
\frac{3}{10}&\frac{3}{40}&\frac{9}{40}&&&&&\\
\frac{4}{5}&\frac{44}{45}&-\frac{56}{15}&\frac{32}{9}&&&&\\
\frac{8}{9}&\frac{19372}{6561}&-\frac{25360}{2187}&\frac{64448}{6561}&-\frac{212}{729}&&&\\
1&\frac{9017}{3168}&-\frac{355}{33}&\frac{46732}{5247}&\frac{49}{176}&-\frac{5103}{18656}&&\\
1&\frac{35}{384}&0&\frac{500}{1113}&\frac{125}{192}&-\frac{2187}{6784}&\frac{11}{84}&\\\hline
(5)&\frac{35}{384}&0&\frac{500}{1113}&\frac{125}{192}&-\frac{2187}{6784}&\frac{11}{84}&0\\
(4)&\frac{5179}{57600}&0&\frac{7571}{16695}&\frac{393}{640}&-\frac{92097}{339200}&\frac{187}{2100}&\frac{1}{40}
\end{array}
\f]
*/
class DormPrince_45: public RungeKutta_Explicit {
  public:
    DormPrince_45(Equation* eq, bool variable_step = false);
    virtual std::string GetName() { return "RK_ExpDP_45"; };
};
}  // namespace griddyn::paradae
