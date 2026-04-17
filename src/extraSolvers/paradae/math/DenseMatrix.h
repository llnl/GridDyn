/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "VirtualMatrix.h"

#define HAVE_LAPACK_CONFIG_H
#define LAPACK_COMPLEX_CPP

namespace griddyn::paradae {
    typedef int lapack_int;

    class DenseMatrix: public Matrix {
      protected:
        Real* data;
        Real* f_data;
        lapack_int* f_ipiv;

      private:
        void operator=(const DenseMatrix& mat) {};

      public:
        DenseMatrix(): Matrix(), data(NULL), f_data(NULL), f_ipiv(NULL) {};
        DenseMatrix(int m_, Real fill_ = 0);
        DenseMatrix(const DenseMatrix& mat);
        virtual ~DenseMatrix();

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
        virtual void SetIJV(int m_, int nnz_, int* ival, int* jval, Real* vval);

        virtual void dump(std::ostream& output) const;
    };
}  // namespace griddyn::paradae
