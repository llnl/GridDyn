/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "DBlockTriMatrix.h"

#include "PMultiVector.h"
#include "PVector.h"
#include "SVector.h"
#include "Vector.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <typeinfo>

namespace griddyn {
namespace paradae {
    using namespace std;

    DBlockTriMatrix::DBlockTriMatrix(int m_, int s_, bool isdense_):
        VirtualMatrix(m_), s(s_), isdense(isdense_)
    {
        nb_blocks = (m * (m - 1)) / 2;
        diag = new Matrix*[m];
        data = new Matrix*[nb_blocks];

        if (isdense) {
            for (int i = 0; i < m; i++)
                diag[i] = new DenseMatrix(s, 0);
            for (int i = 0; i < nb_blocks; i++)
                data[i] = new DenseMatrix(s, 0);
        } else {
            for (int i = 0; i < m; i++)
                diag[i] = new SparseMatrix(s, 0);
            for (int i = 0; i < nb_blocks; i++)
                data[i] = new SparseMatrix(s, 0);
        }
    }

    DBlockTriMatrix::DBlockTriMatrix(const DBlockTriMatrix& mat): VirtualMatrix(mat)
    {
        nb_blocks = mat.nb_blocks;
        s = mat.s;
        isdense = mat.isdense;
        if (mat.diag != NULL) {
            diag = new Matrix*[m];
            data = new Matrix*[nb_blocks];
            if (isdense) {
                for (int i = 0; i < m; i++)
                    diag[i] = new DenseMatrix;
                for (int i = 0; i < nb_blocks; i++)
                    data[i] = new DenseMatrix;
            } else {
                for (int i = 0; i < m; i++)
                    diag[i] = new SparseMatrix;
                for (int i = 0; i < nb_blocks; i++)
                    data[i] = new SparseMatrix;
            }
            for (int i = 0; i < m; i++)
                diag[i]->Clone(*(mat.diag[i]));
            for (int i = 0; i < nb_blocks; i++)
                data[i]->Clone(*(mat.data[i]));
        } else {
            diag = NULL;
            data = NULL;
        }
    }

    DBlockTriMatrix::~DBlockTriMatrix()
    {
        if (diag != NULL) {
            for (int i = 0; i < m; i++)
                delete diag[i];
            delete[] diag;
        }
        if (data != NULL) {
            for (int i = 0; i < nb_blocks; i++)
                delete data[i];
            delete[] data;
        }
    }

    void DBlockTriMatrix::Clone(const VirtualMatrix& mat)
    {
        try {
            const DBlockTriMatrix& dmat = dynamic_cast<const DBlockTriMatrix&>(mat);
            if (diag != NULL && (m != dmat.m || dmat.diag == NULL)) {
                for (int i = 0; i < m; i++)
                    delete diag[i];
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
                        for (int i = 0; i < m; i++)
                            diag[i] = new DenseMatrix;
                        for (int i = 0; i < nb_blocks; i++)
                            data[i] = new DenseMatrix;
                    } else {
                        for (int i = 0; i < m; i++)
                            diag[i] = new SparseMatrix;
                        for (int i = 0; i < nb_blocks; i++)
                            data[i] = new SparseMatrix;
                    }
                }
                for (int i = 0; i < m; i++)
                    diag[i]->Clone(*(dmat.diag[i]));
                for (int i = 0; i < nb_blocks; i++)
                    data[i]->Clone(*(dmat.data[i]));
            } else {
                diag = NULL;
                data = NULL;
            }
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in DBlockTriMatrix::Clone" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    Real& DBlockTriMatrix::operator()(int i, int j)
    {
        int ib, jb, il, jl;
        ib = i / s;
        jb = j / s;
        il = i - ib * s;
        jl = j - jb * s;
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= s * m || j < 0 || j >= s * m || ib < jb) {
            cerr << "Bad index in DBlockTriMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
#endif
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        if (ib == jb)
            return diag[ib]->operator()(il, jl);
        else
            return data[(ib * (ib - 1)) / 2 + jb]->operator()(il, jl);
    }

    Real DBlockTriMatrix::operator()(int i, int j) const
    {
        int ib, jb, il, jl;
        ib = i / s;
        jb = j / s;
        il = i - ib * s;
        jl = j - jb * s;
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= s * m || j < 0 || j >= s * m || ib < jb) {
            cerr << "Bad index in DBlockTriMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
#endif
        const Matrix* mat;
        if (ib == jb)
            mat = diag[ib];
        else
            mat = data[(ib * (ib - 1)) / 2 + jb];

        return mat->operator()(il, jl);
    }

    void DBlockTriMatrix::operator*=(Real alpha)
    {
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        for (int i = 0; i < m; i++)
            diag[i]->operator*=(alpha);
        for (int i = 0; i < nb_blocks; i++)
            data[i]->operator*=(alpha);
    }

    void DBlockTriMatrix::Fill(Real fill_)
    {
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        for (int i = 0; i < m; i++)
            diag[i]->Fill(fill_);
        for (int i = 0; i < nb_blocks; i++)
            data[i]->Fill(fill_);
    }

    void DBlockTriMatrix::SetSubMat(int i, int j, const VirtualMatrix& mat, Real multcoeff)
    {
#ifdef CHECK_MEM_OP
        if (i % s != 0 || j % s != 0) {
            cerr << "SetSubMat overlaps block" << endl;
            abort();
        }
#endif
        i = i / s;
        j = j / s;
#ifdef CHECK_MEM_OP
        if (i < 0 || j < 0 || i >= m || j >= m || i < j) {
            cerr << "Bad index in DBlockTriMatrix[" << m << "," << s << "]::SetBlock(" << i << ","
                 << j << ")" << endl;
            abort();
        }
        if (s != mat.GetM()) {
            cerr << "Inserting VirtualMatrix[" << mat.GetM() << "] into DBlockTriMatrix[" << m
                 << "," << s << "]::SetBlock(" << i << "," << j << ")" << endl;
            abort();
        }
#endif
        if (i == j) {
            diag[i]->Clone(mat);
            if (multcoeff != 1) diag[i]->operator*=(multcoeff);
            if (isfacto && !mat.IsFacto()) {
                diag[i]->Factorize();
            }
        } else {
            data[(i * (i - 1)) / 2 + j]->Clone(mat);
            if (multcoeff != 1) data[(i * (i - 1)) / 2 + j]->operator*=(multcoeff);
        }
    }

    void DBlockTriMatrix::AXPBY(Real alpha, Real beta, const VirtualMatrix& x)
    {
#ifdef CHECK_MEM_OP
        if (m != x.GetM()) {
            cerr << "Bad index in DBlockTriMatrix[" << m << "]::AXPBY=(VirtualMatrix[" << x.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        if (isfacto) {
            cerr << "Warning : Modifying a factorized matrix ! Deleting all facto-related" << endl;
            this->ClearFacto();
        }
        try {
            const DBlockTriMatrix& dmat = dynamic_cast<const DBlockTriMatrix&>(x);
            for (int i = 0; i < m; i++)
                diag[i]->AXPBY(alpha, beta, *(dmat.diag[i]));
            for (int i = 0; i < nb_blocks; i++)
                data[i]->AXPBY(alpha, beta, *(dmat.data[i]));
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in DBlockTriMatrix::AXPBY" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    void DBlockTriMatrix::MatMult(Vector& b, bool transpose) const
    {
#ifdef CHECK_MEM_OP
        if (b.GetM() != s * m) {
            cerr << "Bad index in DBlockTriMatrix[" << m << "," << s << "]::MatMult(Vector["
                 << b.GetM() << "])" << endl;
            abort();
        }
#endif
        PMultiVector mv_b(b, m, s);
        SVector tmp;
        if (m > 0) tmp.Resize(s);
        PVector curr;
        if (transpose) {
            for (int i = 0; i < m; i++) {
                mv_b.GetPVector(i, curr);
                diag[i]->MatMult(curr, transpose);
                for (int j = i + 1; j < m; j++) {
                    mv_b.GetSVector(j, tmp);
                    data[(j * (j - 1)) / 2 + i]->MatMult(tmp, transpose); /*to check*/
                    curr += tmp;
                }
            }
        } else {
            for (int i = m - 1; i >= 0; i--) {
                mv_b.GetPVector(i, curr);
                diag[i]->MatMult(curr, transpose);
                for (int j = 0; j < i; j++) {
                    mv_b.GetSVector(j, tmp);
                    data[(i * (i - 1)) / 2 + j]->MatMult(tmp, transpose);
                    curr += tmp;
                }
            }
        }
    }

    void DBlockTriMatrix::Factorize()
    {
        if (!isfacto) {
            for (int i = 0; i < m; i++)
                diag[i]->Factorize();
            isfacto = true;
            isrankdef = false;
        }
    }

    void DBlockTriMatrix::Solve(Vector& b, bool transpose) const
    {
        if (!isfacto) {
            cerr << "Trying to solve an unfactorized matrix !" << endl;
            abort();
        }
#ifdef CHECK_MEM_OP
        if (b.GetM() != s * m) {
            cerr << "Bad index in DenseMatrix[" << m << "," << s << "]::Solve(Vector[" << b.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        if (transpose) {
            cerr << "Trying to transpose-solve a BlockTriMatrix. Aborting" << endl;
            abort();
        } else {
            PMultiVector mv_b(b, m, s);
            SVector tmp;
            if (m > 0) tmp.Resize(s);
            PVector curr;
            for (int i = 0; i < m; i++) {
                mv_b.GetPVector(i, curr);
                for (int k = 0; k < i; k++) {
                    mv_b.GetSVector(k, tmp);
                    data[(i * (i - 1)) / 2 + k]->MatMult(tmp);
                    curr -= tmp;
                }
                diag[i]->Solve(curr);
            }
        }
    }

    void DBlockTriMatrix::ClearFacto()
    {
        for (int i = 0; i < m; i++)
            diag[i]->ClearFacto();
        for (int i = 0; i < nb_blocks; i++)
            data[i]->ClearFacto();
        isfacto = false;
        isrankdef = false;
    }

    void DBlockTriMatrix::dump(ostream& output) const
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
