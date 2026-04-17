/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Vector.h"

namespace griddyn::paradae {
class PVector;

class SVector: public Vector {
  public:
    SVector(): Vector() {};
    SVector(int m_, Real fill_ = 0);
    SVector(const SVector& v): SVector((Vector)v) {};
    SVector(const Vector& v);
    virtual ~SVector();
    void Free();
    void Resize(int m_, Real fill_ = 0);
    void Append(Real alpha);
    bool operator==(const SVector& v) const;
    SVector& operator=(const SVector& v);
    SVector& operator=(const PVector& v);

    static SVector Rand(int n, Real a = 0, Real b = 1);
};
}  // namespace griddyn::paradae
