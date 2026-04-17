/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "DBlockTriMatrix.h"

namespace griddyn::paradae {
class SBlockTriMatrix: public DBlockTriMatrix {
  private:
    void operator=(const SBlockTriMatrix& mat) {};

  public:
    SBlockTriMatrix(): DBlockTriMatrix() {};
    SBlockTriMatrix(int m_, int s_, bool isdense_ = true);
    SBlockTriMatrix(const SBlockTriMatrix& mat);
    virtual ~SBlockTriMatrix();

    virtual void Clone(const VirtualMatrix& mat);
    virtual Real& operator()(int i, int j);
    virtual Real operator()(int i, int j) const;
    virtual void operator*=(Real alpha);
    virtual void Fill(Real fill_ = 0);
    virtual void SetSubMat(int i, int j, const VirtualMatrix& mat, Real multcoeff = 1.0);

    virtual void AXPBY(Real alpha, Real beta, const VirtualMatrix& mat);
    virtual void MatMult(Vector& vec, bool transpose = false) const;
    virtual void Factorize();
    virtual void Solve(Vector& vec, bool transpose = false) const;
    virtual void ClearFacto();

    virtual void dump(std::ostream& output) const;
};
}  // namespace griddyn::paradae
