/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef PVector_h
#define PVector_h

#include "Vector.h"

namespace griddyn {
namespace paradae {
    class PVector: public Vector {
      public:
        PVector(): Vector() {};
        PVector(const Vector& v);
        virtual ~PVector() {};
        PVector& operator=(const Vector& v);
        PVector& operator=(const PVector& v);
        void Set(int m_, Real* data_);
    };
}  // namespace paradae
}  // namespace griddyn

#endif
