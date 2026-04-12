/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_EQUATIONS_EQUATION_DAE_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_EQUATIONS_EQUATION_DAE_H_

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

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_EQUATIONS_EQUATION_DAE_H_
