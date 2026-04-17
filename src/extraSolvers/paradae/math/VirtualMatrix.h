/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../common/def.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace griddyn::paradae {
class Vector;

class VirtualMatrix {
  private:
    void operator=(const VirtualMatrix& /*mat*/) {};

  protected:
    int m;
    bool isfacto;
    bool isrankdef;

  public:
    VirtualMatrix(): m(0), isfacto(false), isrankdef(false) {};
    VirtualMatrix(int m_): m(m_), isfacto(false), isrankdef(false) {};
    VirtualMatrix(const VirtualMatrix& mat):
        m(mat.m), isfacto(mat.isfacto), isrankdef(mat.isrankdef) {};
    virtual ~VirtualMatrix() {};

    inline int& GetM() { return m; };
    inline int GetM() const { return m; };
    inline bool& IsFacto() { return isfacto; };
    inline bool IsFacto() const { return isfacto; };
    inline bool& IsRankDeficient() { return isrankdef; };
    inline bool IsRankDeficient() const { return isrankdef; };

    virtual void Clone(const VirtualMatrix& mat) = 0;
    virtual Real& operator()(int i, int j) = 0;
    virtual Real operator()(int i, int j) const = 0;
    virtual void operator*=(Real alpha) = 0;
    virtual void Fill(Real fill_ = 0) = 0;
    virtual void SetSubMat(int i, int j, const VirtualMatrix& mat, Real multcoeff = 1.0) = 0;

    virtual void AXPBY(Real alpha, Real beta, const VirtualMatrix& mat) = 0;
    virtual void MatMult(Vector& vec, bool transpose = false) const = 0;
    virtual void Factorize() = 0;
    virtual void Solve(Vector& vec, bool transpose = false) const = 0;
    virtual void ClearFacto() = 0;

    void dump() const { this->dump(std::cout); };
    void dump(std::string filename) const;
    virtual void dump(std::ostream& output) const = 0;
};

std::ostream& operator<<(std::ostream& output, const VirtualMatrix& mat);

class Matrix: public VirtualMatrix {
  private:
    void operator=(const Matrix& mat) {};

  public:
    Matrix(): VirtualMatrix() {};
    Matrix(int m_): VirtualMatrix(m_) {};
    Matrix(const Matrix& mat): VirtualMatrix(mat) {};
    virtual ~Matrix() {};
    virtual void SetIJV(int n_, int nnz_, int* ival, int* jval, Real* vval) = 0;
};

}  // namespace griddyn::paradae
