/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "DenseMatrix.h"

#include "../common/Timer.h"
#include "Vector.h"
#include "lapacke_utils.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <typeinfo>

namespace griddyn {
namespace paradae {
    using namespace std;

    DenseMatrix::DenseMatrix(int m_, Real fill_): Matrix(m_), f_data(NULL), f_ipiv(NULL)
    {
        if (m_ >= 0) data = new Real[m_ * m_];
        fill(data, data + m_ * m_, fill_);
    }

    DenseMatrix::DenseMatrix(const DenseMatrix& mat): Matrix(mat)
    {
        if (mat.data != NULL) {
            data = new Real[m * m];
            memcpy(data, mat.data, m * m * sizeof(Real));
        } else
            data = NULL;

        if (mat.f_data != NULL) {
            f_data = new Real[m * m];
            memcpy(f_data, mat.f_data, m * m * sizeof(Real));
        } else
            f_data = NULL;

        if (mat.f_ipiv != NULL) {
            f_ipiv = new lapack_int[m];
            memcpy(f_ipiv, mat.f_ipiv, m * sizeof(lapack_int));
        } else
            f_ipiv = NULL;
    }

    DenseMatrix::~DenseMatrix()
    {
        if (data != NULL) delete[] data;
        if (f_data != NULL) delete[] f_data;
        if (f_ipiv != NULL) delete[] f_ipiv;
    }

    void DenseMatrix::Clone(const VirtualMatrix& mat)
    {
        try {
            const DenseMatrix& dmat = dynamic_cast<const DenseMatrix&>(mat);
            if (data != NULL && (m != dmat.m || dmat.data == NULL)) {
                delete[] data;
                data = NULL;
            }
            if (f_ipiv != NULL && (m != dmat.m || dmat.f_ipiv == NULL)) {
                delete[] f_ipiv;
                f_ipiv = NULL;
            }
            if (f_data != NULL && (m != dmat.m || dmat.f_data == NULL)) {
                delete[] f_data;
                f_data = NULL;
            }
            m = dmat.m;
            isfacto = dmat.isfacto;
            isrankdef = dmat.isrankdef;
            if (dmat.data != NULL) {
                if (data == NULL) data = new Real[m * m];
                memcpy(data, dmat.data, m * m * sizeof(Real));
            }
            if (dmat.f_ipiv != NULL) {
                if (f_ipiv == NULL) f_ipiv = new lapack_int[m];
                memcpy(f_ipiv, dmat.f_ipiv, m * sizeof(lapack_int));
            }
            if (dmat.f_data != NULL) {
                if (f_data == NULL) f_data = new Real[m * m];
                memcpy(f_data, dmat.f_data, m * m * sizeof(lapack_int));
            }
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in DenseMatrix::Clone" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    Real& DenseMatrix::operator()(int i, int j)
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= m || j < 0 || j >= m) {
            cerr << "Bad index in DenseMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
#endif
        if (isfacto) {
            ClearFacto();
        }
        return data[i * m + j];
    }

    Real DenseMatrix::operator()(int i, int j) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= m || j < 0 || j >= m) {
            cerr << "Bad index in DenseMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
#endif
        return data[i * m + j];
    }

    void DenseMatrix::operator*=(Real alpha)
    {
        if (isfacto) {
            ClearFacto();
        }
        for (int i = 0; i < m * m; i++)
            data[i] *= alpha;
    }

    void DenseMatrix::Fill(Real fill_)
    {
        if (isfacto) {
            ClearFacto();
        }
        fill(data, data + m * m, fill_);
    }

    void DenseMatrix::SetSubMat(int i, int j, const VirtualMatrix& mat, Real multcoeff)
    {
        int mat_m = mat.GetM();
#ifdef CHECK_MEM_OP
        if (i < 0 || i + mat_m - 1 >= m || j < 0 || j + mat_m - 1 >= m) {
            cerr << "Bad index in DenseMatrix[" << m << "]::SetSubMat(" << i << "," << j
                 << ",VirtualMatrix[" << mat_m << "])" << endl;
            abort();
        }
#endif
        if (isfacto || mat.IsFacto()) {
            ClearFacto();
        }
        try {
            const DenseMatrix& dmat = dynamic_cast<const DenseMatrix&>(mat);
            if (multcoeff == 1) {
                for (int idx = 0; idx < mat_m; idx++)
                    memcpy(data + (i + idx) * m + j, dmat.data + idx * mat_m, mat_m * sizeof(Real));
            } else {
                for (int idx = 0; idx < mat_m; idx++)
                    for (int idx2 = 0; idx2 < mat_m; idx2++)
                        data[(i + idx) * m + j + idx2] = multcoeff * dmat.data[idx * mat_m + idx2];
            }
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in DenseMatrix::SetSubMat" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    void DenseMatrix::AXPBY(Real alpha, Real beta, const VirtualMatrix& x)
    {
#ifdef CHECK_MEM_OP
        if (m != x.GetM()) {
            cerr << "Bad index in DenseMatrix[" << m << "]::AXPBY=(VirtualMatrix[" << x.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        if (isfacto) {
            ClearFacto();
        }
        try {
            const DenseMatrix& dmat = dynamic_cast<const DenseMatrix&>(x);
            for (int i = 0; i < m * m; i++)
                data[i] = alpha * dmat.data[i] + beta * data[i];
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in DenseMatrix::AXPBY" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    void DenseMatrix::MatMult(Vector& b, bool transpose) const
    {
#ifdef CHECK_MEM_OP
        if (b.GetM() != m) {
            cerr << "Bad index in DenseMatrix[" << m << "]::MatMult(Vector[" << b.GetM() << "])"
                 << endl;
            abort();
        }
#endif
        Real* oldata = new Real[m];
        Real* newdata = b.GetData();
        memcpy(oldata, newdata, m * sizeof(Real));

        Real val;

        if (transpose) {
            for (int i = 0; i < m; i++) {
                val = 0;
                for (int j = 0; j < m; j++)
                    val += data[j * m + i] * oldata[j];
                newdata[i] = val;
            }
        } else {
            for (int i = 0; i < m; i++) {
                val = 0;
                for (int j = 0; j < m; j++)
                    val += data[i * m + j] * oldata[j];
                newdata[i] = val;
            }
        }
        delete[] oldata;
    }

    void DenseMatrix::Factorize()
    {
#if TIMER_INTRUSION >= 2
        global_timer.Start("Dense facto");
#endif
        if (!isfacto) {
            lapack_int m_lap, n_lap, lda_lap, flag;
            if (f_ipiv != NULL) delete[] f_ipiv;
            if (f_data != NULL) delete[] f_data;
            f_ipiv = new lapack_int[m];
            f_data = new Real[m * m];
            memcpy(f_data, data, m * m * sizeof(Real));

            m_lap = m;
            n_lap = m;
            lda_lap = m;
#if REAL == 1
            flag = LAPACKE_sgetrf(LAPACK_ROW_MAJOR, m_lap, n_lap, f_data, lda_lap, f_ipiv);
#else
            flag = LAPACKE_dgetrf(LAPACK_ROW_MAJOR, m_lap, n_lap, f_data, lda_lap, f_ipiv);
#endif
            if (flag > 0) isrankdef = true;

            isfacto = true;
        }
#if TIMER_INTRUSION >= 2
        global_timer.Stop("Dense facto");
#endif
    }

    void DenseMatrix::Solve(Vector& b, bool transpose) const
    {
#if TIMER_INTRUSION >= 2
        global_timer.Start("Dense solve");
#endif
        if (!isfacto) {
            cerr << "Trying to solve an unfactorized matrix !" << endl;
            abort();
        }
#ifdef CHECK_MEM_OP
        if (b.GetM() != m) {
            cerr << "Bad index in DenseMatrix[" << m << "]::Solve(Vector[" << b.GetM() << "])"
                 << endl;
            abort();
        }
#endif
        char char_transpose = 'N';
        if (transpose) char_transpose = 'T';

        if (!isrankdef) {
            lapack_int n_lap, lda_lap;
            n_lap = m;
            lda_lap = m;

#if REAL == 1
            LAPACKE_sgetrs(LAPACK_ROW_MAJOR,
                           char_transpose,
                           n_lap,
                           1,
                           f_data,
                           lda_lap,
                           f_ipiv,
                           b.GetData(),
                           1);
#else
            LAPACKE_dgetrs(LAPACK_ROW_MAJOR,
                           char_transpose,
                           n_lap,
                           1,
                           f_data,
                           lda_lap,
                           f_ipiv,
                           b.GetData(),
                           1);
#endif
        } else {
            if (transpose) {
                cerr << "Trying to transpose-solve a rank deficient matrix. Aborting" << endl;
                abort();
            }
            lapack_int m_lap, n_lap, lda_lap, ldb_lap, rank;
            m_lap = m;
            n_lap = m;
            lda_lap = m;
            ldb_lap = 1;
            double rcond = -1;
            double* s = new double[m];

#if REAL == 1
            LAPACKE_sgelsd(LAPACK_ROW_MAJOR,
                           m_lap,
                           n_lap,
                           1,
                           data,
                           lda_lap,
                           b.GetData(),
                           ldb_lap,
                           s,
                           rcond,
                           &rank);
#else
            LAPACKE_dgelsd(LAPACK_ROW_MAJOR,
                           m_lap,
                           n_lap,
                           1,
                           data,
                           lda_lap,
                           b.GetData(),
                           ldb_lap,
                           s,
                           rcond,
                           &rank);
#endif

            delete[] s;
        }
#if TIMER_INTRUSION >= 2
        global_timer.Stop("Dense solve");
#endif
    }

    void DenseMatrix::ClearFacto()
    {
        if (f_data != NULL) {
            delete[] f_data;
            f_data = NULL;
        }
        if (f_ipiv != NULL) {
            delete[] f_ipiv;
            f_ipiv = NULL;
        }

        isfacto = false;
        isrankdef = false;
    }

    void DenseMatrix::SetIJV(int m_, int nnz_, int* ival, int* jval, Real* vval)
    {
        if (isfacto) {
            ClearFacto();
        }
        if (data != NULL && m != m_) {
            delete[] data;
            data = NULL;
        }
        m = m_;
        if (data == NULL) data = new Real[m * m];
        fill(data, data + m * m, 0);
        for (int i = 0; i < nnz_; i++)
            data[ival[i] * m + jval[i]] = vval[i];
    }

    void DenseMatrix::dump(ostream& output) const
    {
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < m; j++)
                output << data[i * m + j] << " ";
            output << endl;
        }
    }
}  // namespace paradae
}  // namespace griddyn
