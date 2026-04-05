/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef Equation_DAE_h
#define Equation_DAE_h

#include "Equation.h"

namespace griddyn {
namespace paradae {
    class Equation_DAE: public Equation {
      public:
        // Redefinition of inherited virtual methods
        virtual ~Equation_DAE() {};
        virtual type_Equation GetTypeEq() { return DAE; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif
