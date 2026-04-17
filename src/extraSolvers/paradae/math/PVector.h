/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Vector.h"

namespace griddyn::paradae {
    class PVector: public Vector {
      public:
        PVector(): Vector() {};
        PVector(const Vector& v);
        virtual ~PVector() {};
        PVector& operator=(const Vector& v);
        PVector& operator=(const PVector& v);
        void Set(int m_, Real* data_);
    };
}  // namespace griddyn::paradae
