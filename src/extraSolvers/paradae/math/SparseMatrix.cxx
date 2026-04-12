/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SparseMatrix.h"

#include "../common/Timer.h"
#include "SVector.h"
#include "Vector.h"
#include "umfpack.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <typeinfo>

using namespace std;

namespace griddyn {
namespace paradae {
    SparseMatrix::SparseMatrix(int m_, int nnz_): Matrix(m_), nnz(nnz_), numeric(NULL)
    {
        rowptr = new int[m + 1];
        fill(rowptr, rowptr + m + 1, 0);
        if (nnz > 0) {
            col = new int[nnz];
            data = new Real[nnz];
        } else {
            col = new int[1];
            col[0] = 0;
            data = new Real[1];
            data[0] = 0;
        }
    }

    SparseMatrix::SparseMatrix(const SparseMatrix& mat): Matrix(mat)
    {
        nnz = mat.nnz;
        if (mat.rowptr != NULL) {
            rowptr = new int[m + 1];
            memcpy(rowptr, mat.rowptr, (m + 1) * sizeof(int));
        } else
            rowptr = NULL;

        if (mat.col != NULL) {
            if (nnz == 0) {
                col = new int[1];
                col[0] = 0;
            } else {
                col = new int[nnz];
                memcpy(col, mat.col, nnz * sizeof(int));
            }
        } else
            col = NULL;

        if (mat.data != NULL) {
            if (nnz == 0) {
                data = new Real[1];
                data[0] = 0;
            } else {
                data = new Real[nnz];
                memcpy(data, mat.data, nnz * sizeof(Real));
            }
        } else
            data = NULL;

        isfacto = false;
        if (mat.IsFacto()) this->Factorize();
    }

    SparseMatrix::~SparseMatrix()
    {
        if (data != NULL) delete[] data;
        if (rowptr != NULL) delete[] rowptr;
        if (col != NULL) delete[] col;
        if (numeric != NULL) umfpack_di_free_numeric(&numeric);
    }

    void SparseMatrix::Clone(const VirtualMatrix& mat)
    {
        try {
            const SparseMatrix& dmat = dynamic_cast<const SparseMatrix&>(mat);
            if (rowptr != NULL && (m != dmat.m || dmat.rowptr == NULL)) {
                delete[] rowptr;
                rowptr = NULL;
            }
            if (col != NULL && (nnz != dmat.nnz || dmat.col == NULL)) {
                delete[] col;
                col = NULL;
            }
            if (data != NULL && (nnz != dmat.nnz || dmat.data == NULL)) {
                delete[] data;
                data = NULL;
            }
            m = dmat.m;
            nnz = dmat.nnz;

            if (dmat.rowptr != NULL) {
                if (rowptr == NULL) rowptr = new int[m + 1];
                memcpy(rowptr, dmat.rowptr, (m + 1) * sizeof(int));
            }
            if (dmat.col != NULL) {
                if (col == NULL) col = new int[max(1, nnz)];
                if (nnz > 0) memcpy(col, dmat.col, nnz * sizeof(int));
            }
            if (dmat.data != NULL) {
                if (data == NULL) data = new Real[max(1, nnz)];
                if (nnz > 0) memcpy(data, dmat.data, nnz * sizeof(Real));
            }

            if (isfacto) ClearFacto();

            if (dmat.IsFacto()) this->Factorize();
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in SparseMatrix::Clone" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    Real& SparseMatrix::operator()(int i, int j)
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= m || j < 0 || j >= m) {
            cerr << "Bad index in SparseMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
#endif
        if (isfacto) {
            ClearFacto();
        }
        if (nnz == 0) return this->InsertVal(i, j, 0);

        int idx0 = rowptr[j];
        int idx1 = rowptr[j + 1];

        while (idx0 < idx1 && col[idx0] < i)
            idx0++;

        if (idx0 == idx1 || col[idx0] != i)
            return this->InsertVal(i, j, 0);
        else
            return data[idx0];
    }

    Real SparseMatrix::operator()(int i, int j) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= m || j < 0 || j >= m) {
            cerr << "Bad index in SparseMatrix[" << m << "]::operator(" << i << "," << j << ")"
                 << endl;
            abort();
        }
#endif
        if (nnz == 0) return 0;

        int idx0 = rowptr[j];
        int idx1 = rowptr[j + 1];

        while (idx0 < idx1 && col[idx0] < i)
            idx0++;

        if (idx0 != idx1 && col[idx0] == i)
            return data[idx0];
        else
            return 0;
    }

    void SparseMatrix::operator*=(Real alpha)
    {
        if (isfacto) {
            ClearFacto();
        }
        if (alpha == 0)
            this->Fill(0);
        else {
            for (int i = 0; i < nnz; i++)
                data[i] *= alpha;
        }
    }

    void SparseMatrix::Fill(Real fill_)
    {
        if (isfacto) {
            ClearFacto();
        }
        if (fill_ != 0) {
            cerr << "Filling a SparseMatrix with nonzero fill..." << endl;
            abort();
        }
        nnz = 0;
        if (data != NULL) delete[] data;
        if (col != NULL) delete[] col;
        fill(rowptr, rowptr + m + 1, 0);
        col = new int[1];
        col[0] = 0;
        data = new Real[1];
        data[0] = 0;
    }

    void SparseMatrix::FillKeepingStruct(Real fill_)
    {
        if (isfacto) {
            ClearFacto();
        }
        if (data != NULL) fill(data, data + nnz, fill_);
    }

    Real& SparseMatrix::InsertVal(int i, int j, Real val)
    {
        if (nnz == 0) {
            nnz++;
            for (int ii = j + 1; ii < m + 1; ii++)
                rowptr[ii]++;
            col[0] = i;
            data[0] = val;
            return data[0];
        }

        int idx0 = rowptr[j];
        int idx1 = rowptr[j + 1];

        while (idx0 < idx1 && col[idx0] < i)
            idx0++;

        if (idx0 < idx1 && col[idx0] == i) {
            cerr << "Try to insert a value that already exists (" << data[idx0]
                 << ")in SparseMatrix::InsertVal(" << i << "," << j << "," << val << endl;
            abort();
        } else {
            nnz++;
            for (int ii = j + 1; ii < m + 1; ii++)
                rowptr[ii]++;

            int* new_col = new int[nnz];
            memcpy(new_col, col, idx0 * sizeof(int));
            new_col[idx0] = i;
            memcpy(new_col + idx0 + 1, col + idx0, (nnz - idx0 - 1) * sizeof(int));
            Real* new_data = new Real[nnz];
            memcpy(new_data, data, idx0 * sizeof(Real));
            new_data[idx0] = val;
            memcpy(new_data + idx0 + 1, data + idx0, (nnz - idx0 - 1) * sizeof(Real));
            delete[] data;
            delete[] col;
            data = new_data;
            col = new_col;
            return data[idx0];
        }
    }

    void SparseMatrix::SetSubMat(int i, int j, const VirtualMatrix& mat, Real multcoeff)
    {
#ifdef CHECK_MEM_OP
        int mat_m = mat.GetM();
        if (i < 0 || i + mat_m - 1 >= m || j < 0 || j + mat_m - 1 >= m) {
            cerr << "Bad index in SparseMatrix[" << m << "]::SetSubMat(" << i << "," << j
                 << ",VirtualMatrix[" << mat_m << "])" << endl;
            abort();
        }
#endif
        if (isfacto) {
            ClearFacto();
        }
        try {
            const SparseMatrix& dmat = dynamic_cast<const SparseMatrix&>(mat);
            cerr << " TODOBETTER " << endl;
            for (int jloc = 0; jloc < dmat.m; jloc++) {
                for (int idxi = dmat.rowptr[jloc]; idxi < dmat.rowptr[jloc + 1]; idxi++) {
                    int iloc = dmat.col[idxi];
                    Real v = dmat.data[idxi];
                    this->operator()(i + iloc, j + jloc) = multcoeff * v;
                }
            }
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in SparseMatrix::SetSubMat" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    void SparseMatrix::AXPBY(Real alpha, Real beta, const VirtualMatrix& x)
    {
#ifdef CHECK_MEM_OP
        if (m != x.GetM()) {
            cerr << "Bad index in SparseMatrix[" << m << "]::AXPBY=(VirtualMatrix[" << x.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        if (isfacto) {
            ClearFacto();
        }
        if (alpha == 0) {
            this->operator*=(beta);
            return;
        }
        if (beta == 0) {
            if (alpha == 0) {
                this->Fill(0);
                return;
            }
            this->Clone(x);
            this->operator*=(alpha);
            return;
        }
        try {
            const SparseMatrix& dmat = dynamic_cast<const SparseMatrix&>(x);
            int* new_rowptr = new int[m + 1];
            int* new_col = new int[nnz + dmat.nnz];
            Real* new_data = new Real[nnz + dmat.nnz];
            int idx = 0;
            for (int j = 0; j < m; j++) {
                new_rowptr[j] = idx;
                int idxi_1 = rowptr[j];
                int idxi_2 = dmat.rowptr[j];
                while (idxi_1 < rowptr[j + 1] && idxi_2 < dmat.rowptr[j + 1]) {
                    if (col[idxi_1] < dmat.col[idxi_2]) {
                        new_col[idx] = col[idxi_1];
                        new_data[idx] = beta * data[idxi_1];
                        idx++;
                        idxi_1++;
                    } else if (col[idxi_1] > dmat.col[idxi_2]) {
                        new_col[idx] = dmat.col[idxi_2];
                        new_data[idx] = alpha * dmat.data[idxi_2];
                        idx++;
                        idxi_2++;
                    } else {
                        new_col[idx] = col[idxi_1];
                        new_data[idx] = alpha * dmat.data[idxi_2] + beta * data[idxi_1];
                        idx++;
                        idxi_1++;
                        idxi_2++;
                    }
                }
                for (int ii = idxi_1; ii < rowptr[j + 1]; ii++) {
                    new_col[idx] = col[ii];
                    new_data[idx] = beta * data[ii];
                    idx++;
                }
                for (int ii = idxi_2; ii < dmat.rowptr[j + 1]; ii++) {
                    new_col[idx] = dmat.col[ii];
                    new_data[idx] = alpha * dmat.data[ii];
                    idx++;
                }
            }
            new_rowptr[m] = idx;
            memcpy(rowptr, new_rowptr, (m + 1) * sizeof(int));
            delete[] new_rowptr;
            if (nnz != idx) {
                nnz = idx;
                delete[] col;
                col = new int[nnz];
                delete[] data;
                data = new Real[nnz];
            }
            memcpy(col, new_col, nnz * sizeof(int));
            delete[] new_col;
            memcpy(data, new_data, nnz * sizeof(Real));
            delete[] new_data;
        }
        catch (const bad_cast& e) {
            cerr << "Bad cast in SparseMatrix::AXPBY" << endl;
            cerr << e.what() << endl;
            abort();
        }
    }

    void SparseMatrix::MatMult(Vector& b, bool transpose) const
    {
#ifdef CHECK_MEM_OP
        if (b.GetM() != m) {
            cerr << "Bad index in SparseMatrix[" << m << "]::MatMult(Vector[" << b.GetM() << "])"
                 << endl;
            abort();
        }
#endif
        Real* oldata = new Real[m];
        Real* newdata = b.GetData();
        memcpy(oldata, newdata, m * sizeof(Real));
        fill(newdata, newdata + m, 0);

        if (transpose) {
            for (int j = 0; j < m; j++) {
                for (int idxi = rowptr[j]; idxi < rowptr[j + 1]; idxi++) {
                    int i = col[idxi];
                    newdata[j] += data[idxi] * oldata[i];
                }
            }
        } else {
            for (int j = 0; j < m; j++) {
                for (int idxi = rowptr[j]; idxi < rowptr[j + 1]; idxi++) {
                    int i = col[idxi];
                    newdata[i] += data[idxi] * oldata[j];
                }
            }
        }
        delete[] oldata;
    }

    void SparseMatrix::Factorize()
    {
#if TIMER_INTRUSION >= 2
        global_timer.Start("Sparse facto");
#endif
        if (!isfacto) {
            void* symbolic;
            double Info[UMFPACK_INFO];
#if REAL == 1
            cerr << "UMFPACK does not support single precision" << endl;
            abort();
#else
            umfpack_di_symbolic(m, m, rowptr, col, data, &symbolic, NULL, NULL);
            umfpack_di_numeric(rowptr, col, data, symbolic, &numeric, NULL, Info);
            umfpack_di_free_symbolic(&symbolic);
            if (Info[UMFPACK_STATUS] == UMFPACK_WARNING_singular_matrix) isrankdef = true;
#endif
            isfacto = true;
        }
#if TIMER_INTRUSION >= 2
        global_timer.Stop("Sparse facto");
#endif
    }

    void SparseMatrix::Solve(Vector& b, bool transpose) const
    {
#if TIMER_INTRUSION >= 2
        global_timer.Start("Sparse solve");
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
        if (!isrankdef) {
#if REAL == 1
            cerr << "UMFPACK does not support single precision" << endl;
            abort();
#else
            SVector x(b);
            if (transpose)
                umfpack_di_solve(
                    UMFPACK_At, rowptr, col, data, x.GetData(), b.GetData(), numeric, NULL, NULL);
            else
                umfpack_di_solve(
                    UMFPACK_A, rowptr, col, data, x.GetData(), b.GetData(), numeric, NULL, NULL);
            b.CopyData(x);
#endif
        } else {
            cerr << "Trying to solve a rank deficient sparse matrix. Aborting" << endl;
            VirtualMatrix::dump("rank_def_sparse.mat");
            abort();
        }
#if TIMER_INTRUSION >= 2
        global_timer.Stop("Sparse solve");
#endif
    }

    void SparseMatrix::ClearFacto()
    {
        if (numeric != NULL) umfpack_di_free_numeric(&numeric);
        isfacto = false;
        isrankdef = false;
    }

    void SparseMatrix::SetIJV(int m_, int nnz_, int* ival, int* jval, Real* vval)
    {
        if (isfacto) {
            ClearFacto();
        }
        if (rowptr != NULL && m != m_) {
            delete[] rowptr;
            rowptr = NULL;
        }
        if (col != NULL && nnz != nnz_) {
            delete[] col;
            col = NULL;
        }
        if (data != NULL && nnz != nnz_) {
            delete[] data;
            data = NULL;
        }
        m = m_;
        nnz = nnz_;
        if (rowptr == NULL) rowptr = new int[m + 1];
        if (col == NULL) {
            if (nnz == 0) {
                col = new int[1];
                col[0] = 0;
            } else
                col = new int[nnz];
        }
        if (data == NULL) {
            if (nnz == 0) {
                data = new Real[1];
                data[0] = 0;
            } else
                data = new Real[nnz];
        }
        int idxrowptr = 0;
        int prevrow = -1;
        for (int i = 0; i < nnz; i++) {
            while (prevrow != jval[i]) {
                rowptr[idxrowptr] = i;
                idxrowptr++;
                prevrow++;
            }
            col[i] = ival[i];
            data[i] = vval[i];
        }
        rowptr[m] = nnz;
    }

    int SparseMatrix::GetRowIndex(int idx_nnz) const
    {
#ifdef CHECK_MEM_OP
        if (idx_nnz >= nnz) {
            cerr << "Error in SparseMatrix[" << nnz << "]::GetRowIndex(" << idx_nnz << ")" << endl;
            abort();
        }
#endif
        return 0;
    }

    int SparseMatrix::GetColIndex(int idx_nnz) const
    {
#ifdef CHECK_MEM_OP
        if (idx_nnz >= nnz) {
            cerr << "Error in SparseMatrix[" << nnz << "]::GetColIndex(" << idx_nnz << ")" << endl;
            abort();
        }
#endif
        return col[idx_nnz];
    }

    Real SparseMatrix::GetValue(int idx_nnz) const
    {
#ifdef CHECK_MEM_OP
        if (idx_nnz >= nnz) {
            cerr << "Error in SparseMatrix[" << nnz << "]::GetValue(" << idx_nnz << ")" << endl;
            abort();
        }
#endif
        return data[idx_nnz];
    }

    void SparseMatrix::dump(ostream& output) const
    {
        int idx = 0;
        output << m << " " << nnz << endl;
        for (int i = 1; i < m + 1; i++) {
            while (idx < rowptr[i]) {
                output << col[idx] << " " << i - 1 << " " << data[idx] << endl;
                idx++;
            }
        }
    }
}  // namespace paradae
}  // namespace griddyn
