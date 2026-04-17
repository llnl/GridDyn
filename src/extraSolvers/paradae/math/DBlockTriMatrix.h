#pragma once

/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "DenseMatrix.h"
#include "SparseMatrix.h"
#include "VirtualMatrix.h"

namespace griddyn::paradae {
    class DBlockTriMatrix: public VirtualMatrix {
      protected:
        Matrix** diag;
        Matrix** data;
        int s;
        int nb_blocks;
        bool isdense;

      private:
        void operator=(const DBlockTriMatrix& mat) {};

      public:
        DBlockTriMatrix(): VirtualMatrix(), diag(NULL), data(NULL) {};
        DBlockTriMatrix(int m_, int s_, bool isdense_ = true);
        DBlockTriMatrix(const DBlockTriMatrix& mat);
        virtual ~DBlockTriMatrix();

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
