#pragma once

/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Equation.h"

namespace griddyn::paradae {
    class Equation_DAE: public Equation {
      public:
        // Redefinition of inherited virtual methods
        virtual ~Equation_DAE() {};
        virtual type_Equation GetTypeEq() { return DAE; };
    };
}  // namespace griddyn::paradae
