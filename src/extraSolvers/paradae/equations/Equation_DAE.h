/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Equation.h"

namespace griddyn::paradae {
    class Equation_DAE: public Equation {
      public:
        // Redefinition of inherited virtual methods
        virtual ~Equation_DAE() {};
        virtual type_Equation GetTypeEq() { return DAE; };
    };
}  // namespace griddyn::paradae
