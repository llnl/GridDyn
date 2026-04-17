/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../common/def.h"
#include <cstring>
#include <iostream>
#include <string>

namespace griddyn::paradae {
class Vector {
  protected:
    int m;
    Real* data;

  public:
    Vector(): m(0), data(nullptr) {};
    virtual ~Vector() {};
    void Fill(Real fill_ = 0);

    Real operator()(int i) const;
    Real& operator()(int i);
    Vector& operator+=(const Vector& v);
    Vector& operator-=(const Vector& v);
    Vector& operator*=(Real alpha);
    Vector& AXPBY(Real alpha, Real beta, const Vector& x);
    void CopyData(const Vector& v);

    Real Norm2() const;
    Real NormInf() const;
    int GetM() const { return m; };
    Real* GetData() const { return data; };

    void dump() const;
    void dump(std::ostream& output) const;
    void dump(std::string filename) const;
};

std::ostream& operator<<(std::ostream& output, const Vector& vec);

}  // namespace griddyn::paradae
