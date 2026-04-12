/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SBlockTriMatrix.h"

#include "DenseMatrix.h"
#include "PMultiVector.h"
#include "PVector.h"
#include "SVector.h"
#include "SparseMatrix.h"
#include "Vector.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <typeinfo>

namespace griddyn {
namespace paradae {
    using namespace std;

    SBlockTriMatrix::SBlockTriMatrix(int m_, int s_, bool isdense_)
    {
        m = m_;
        isfacto = false;
        isrankdef = false;
        s = s_;
        isdense = isdense_;
        nb_blocks = (m * (m - 1)) / 2;
        diag = new Matrix*[1];
        data = new Matrix*[nb_blocks];
        if (isdense) {
            diag[0] = new DenseMatrix(s, 0);
            for (int i = 0; i < nb_blocks; i++)
                data[i] = new DenseMatrix(s, 0);
        } else {
            diag[0] = new SparseMatrix(s, 0);
            for (int i = 0; i < nb_blocks; i++)
                data[i] = new SparseMatrix(s, 0);
        }
    }

    SBlockTriMatrix::SBlockTriMatrix(const SBlockTriMatrix& mat)
    {
        m = mat.m;
        isfacto = mat.isfacto;
        isrankdef = mat.isrankdef;
        nb_blocks = mat.nb_blocks;
        s = mat.s;
        isdense = mat.isdense;
        if (mat.diag != NULL) {
            diag = new Matrix*[1];
            data = new Matrix*[nb_blocks];
            if (isdense) {
                diag[0] = new DenseMatrix;
                for (int i = 0; i < nb_blocks; i++)
                    data[i] = new DenseMatrix;
            } else {
                diag[0] = new SparseMatrix;
                for (int i = 0; i < nb_blocks; i++)
                    data[i] = new SparseMatrix;
            }
            diag[0]->Clone(*(mat.diag[0]));
            for (int i = 0; i < nb_blocks; i++)
                data[i]->Clone(*(mat.data[i]));
        } else {
            diag = NULL;
            data = NULL;
        }
    }

    SBlockTriMatrix::~SBlockTriMatrix()
    {
        if (diag != NULL) {
            delete diag[0];
            delete[] diag;
        }
        if (data != NULL) {
            for (int i = 0; i < nb_blocks; i++)
                delete data[i];
            delete[] data;
        }
    }

    void SBlockTriMatrix::Clone(const VirtualMatrix& mat)
    {
        try {
            const SBlockTriMatrix& dmat = dynamic_cast<const SBlockTriMatrix&>(mat);
            if (diag != NULL && (m != dmat.m || dmat.diag == NULL)) {
                delete diag[0];
                delete[] diag;
                diag = NULL;
            }
            if (data != NULL && (m != dmat.m || dmat.data == NULL)) {
                for (int i = 0; i < nb_blocks; i++)
                    delete data[i];
                delete[] data;
                data = NULL;
            }
            m = dmat.m;
            isfacto = dmat.isfacto;
            isrankdef = dmat.isrankdef;
            if (dmat.diag != NULL) {
                if (diag == NULL) {
                    diag = new Matrix*[m];
                    data = new Matrix*[nb_blocks];
                    if (isdense) {
                        diag[0] = new DenseMatrix;
                        for (int i = 0; i < nb_blocks; i++)
                            data[i] = new DenseMatrix;
                    } else {
                        diag[0] = new SparseMatrix;
                        for (int i = 0; i < nb_blocks; i++)
                            data[i] = new SparseMatrix;
                    }
                }
                diag[0]->Clone(*(dmat.diag[0]));
                for (int i = 0; i < nb_blocks; i++)
                    data[i]->Clone(*(dmat.data[i]));
            } else {
                diag = NULL;
                data = NULL;
            }
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in SBlockTriMatrix::Clone" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    Real& SBlockTriMatrix::operator()(int i, int j)
    {
        int ib, jb, il, jl;
        ib = i / s;
        jb = j / s;
        il = i - ib * s;
        jl = j - jb * s;
        if (i < 0 || i >= s * m || j < 0 || j >= s * m || ib < jb) {
            cerr << "Bad index in SBlockTriMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        if (ib == jb)
            return diag[0]->operator()(il, jl);
        else
            return data[(ib * (ib - 1)) / 2 + jb]->operator()(il, jl);
    }

    Real SBlockTriMatrix::operator()(int i, int j) const
    {
        int ib, jb, il, jl;
        ib = i / s;
        jb = j / s;
        il = i - ib * s;
        jl = j - jb * s;
        if (i < 0 || i >= s * m || j < 0 || j >= s * m || ib < jb) {
            cerr << "Bad index in SBlockTriMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
        if (ib == jb)
            return diag[0]->operator()(il, jl);
        else
            return data[(ib * (ib - 1)) / 2 + jb]->operator()(il, jl);
    }

    void SBlockTriMatrix::operator*=(Real alpha)
    {
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        diag[0]->operator*=(alpha);
        for (int i = 0; i < nb_blocks; i++)
            data[i]->operator*=(alpha);
    }

    void SBlockTriMatrix::Fill(Real fill_)
    {
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        diag[0]->Fill(fill_);
        for (int i = 0; i < nb_blocks; i++)
            data[i]->Fill(fill_);
    }

    void SBlockTriMatrix::SetSubMat(int i, int j, const VirtualMatrix& mat, Real multcoeff)
    {
        if (i % s != 0 || j % s != 0) {
            cerr << "SetSubMat overlaps block" << endl;
            abort();
        }
        i = i / s;
        j = j / s;
        if (i < 0 || j < 0 || i >= m || j >= m || i < j) {
            cerr << "Bad index in SBlockTriMatrix[" << m << "," << s << "]::SetBlock(" << i << ","
                 << j << ")" << endl;
            abort();
        }
        if (s != mat.GetM()) {
            cerr << "Inserting VirtualMatrix[" << mat.GetM() << "] into SBlockTriMatrix[" << m
                 << "," << s << "]::SetBlock(" << i << "," << j << ")" << endl;
            abort();
        }

        if (i == j) {
            diag[0]->Clone(mat);
            if (multcoeff != 1.0) diag[0]->operator*=(multcoeff);
            if (isfacto && !mat.IsFacto()) {
                diag[0]->Factorize();
            }
        } else {
            data[(i * (i - 1)) / 2 + j]->Clone(mat);
            if (multcoeff != 1.0) data[(i * (i - 1)) / 2 + j]->operator*=(multcoeff);
        }
    }

    void SBlockTriMatrix::AXPBY(Real alpha, Real beta, const VirtualMatrix& x)
    {
        if (m != x.GetM()) {
            cerr << "Bad index in SBlockTriMatrix[" << m << "]::AXPBY=(VirtualMatrix[" << x.GetM()
                 << "])" << endl;
            abort();
        }
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        try {
            const SBlockTriMatrix& dmat = dynamic_cast<const SBlockTriMatrix&>(x);
            diag[0]->AXPBY(alpha, beta, *(dmat.diag[0]));
            for (int i = 0; i < nb_blocks; i++)
                data[i]->AXPBY(alpha, beta, *(dmat.data[i]));
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in SBlockTriMatrix::AXPBY" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    void SBlockTriMatrix::MatMult(Vector& b, bool transpose) const
    {
        if (b.GetM() != s * m) {
            cerr << "Bad index in SBlockTriMatrix[" << m << "," << s << "]::MatMult(Vector["
                 << b.GetM() << "])" << endl;
            abort();
        }

        PMultiVector mv_b(b, m, s);
        SVector tmp(s);
        PVector curr;
        if (transpose) {
            for (int i = 0; i < m; i++) {
                mv_b.GetPVector(i, curr);
                diag[0]->MatMult(curr, transpose);
                for (int j = i + 1; j < m; j++) {
                    mv_b.GetSVector(j, tmp);
                    data[(j * (j - 1)) / 2 + i]->MatMult(tmp, transpose); /*to check*/
                    curr += tmp;
                }
            }
        } else {
            for (int i = m - 1; i >= 0; i--) {
                mv_b.GetPVector(i, curr);
                diag[0]->MatMult(curr, transpose);
                for (int j = 0; j < i; j++) {
                    mv_b.GetSVector(j, tmp);
                    data[(i * (i - 1)) / 2 + j]->MatMult(tmp, transpose);
                    curr += tmp;
                }
            }
        }
    }

    void SBlockTriMatrix::Factorize()
    {
        if (!isfacto) {
            diag[0]->Factorize();
            isfacto = true;
            isrankdef = false;
        }
    }

    void SBlockTriMatrix::Solve(Vector& b, bool transpose) const
    {
        if (!isfacto) {
            cerr << "Trying to solve an unfactorized matrix !" << endl;
            abort();
        }
        if (b.GetM() != s * m) {
            cerr << "Bad index in DenseMatrix[" << m << "," << s << "]::Solve(Vector[" << b.GetM()
                 << "])" << endl;
            abort();
        }
        if (transpose) {
            cerr << "Trying to transpose-solve a BlockTriMatrix. Aborting" << endl;
            abort();
        } else {
            PMultiVector mv_b(b, m, s);
            SVector tmp(s);
            PVector curr;
            for (int i = 0; i < m; i++) {
                mv_b.GetPVector(i, curr);
                for (int k = 0; k < i; k++) {
                    mv_b.GetSVector(k, tmp);
                    data[(i * (i - 1)) / 2 + k]->MatMult(tmp);
                    curr -= tmp;
                }
                diag[0]->Solve(curr);
            }
        }
    }

    void SBlockTriMatrix::ClearFacto()
    {
        diag[0]->ClearFacto();
        for (int i = 0; i < nb_blocks; i++)
            data[i]->ClearFacto();
        isfacto = false;
        isrankdef = false;
    }

    void SBlockTriMatrix::dump(ostream& output) const
    {
        for (int i = 0; i < m * s; i++) {
            for (int j = 0; j < m * s; j++) {
                if (j / s <= i / s)
                    output << this->operator()(i, j) << " ";
                else
                    output << 0 << " ";
            }
            output << endl;
        }
    }
}  // namespace paradae
}  // namespace griddyn
