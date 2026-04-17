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
  (Steps 5 and 6 are probably not defined accurately enough... but seems ok on Expo1D)
  \f[\begin{array}{c|ccccccc}
  0&&&&&&&\\
  0.57178&0.28589&0.28589&&&&&\\
  1.352846005375866&0.142945&0.924011005375866&0.28589&&&&\\
  0.4&0.168035987087646&-0.04941651019480583&-0.00450947689284040&0.28589&&&\\
  0.75&0.182315003&-0.112951603&-0.027793233&0.422539833&0.28589&&\\
  0.9&0.247563917&-0.425378071&-0.107036282&0.395700134&0..503260302&0.28589&\\
  1.0&0.130014275084996&0&-0.0192901771565916&0.5353862667089795&0.2343169293377270&-0.1663172939751104&0.28589\\\hline
  (5)&0.130014275084996&0&-0.0192901771565916&0.5353862667089795&0.2343169293377270&-0.1663172939751104&0.28589\\
  (4)&0.153378753793186&0&0.0214472335406866&0.4225580392042185&0.4026159734619085&0&0
  \end{array}
  \f]
 */
    class Fudziah_45: public RungeKutta_DIRK {
      public:
        Fudziah_45(Equation* eq, bool variable_step = false);
        virtual std::string GetName() { return "RK_ImpFu_45"; };
    };
}  // namespace griddyn::paradae
