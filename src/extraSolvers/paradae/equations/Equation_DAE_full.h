/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef Equation_DAE_full_h
#define Equation_DAE_full_h

#include "Equation_DAE.h"

namespace griddyn {
namespace paradae {
    class Equation_DAE_full: public Equation_DAE {
      public:
        // Redefinition of inherited virtual methods
        virtual ~Equation_DAE_full() {};
        virtual void Get_dy_from_y(Real t, const Vector& y, const Vector& state, Vector& dy)
        {
            abort();
        };
    };
}  // namespace paradae
}  // namespace griddyn

#endif
