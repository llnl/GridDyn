/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SundialsMatrixData.h"
#include <algorithm>
#include <cstring>

namespace griddyn::solvers {
SundialsMatrixDataSparseColumn::SundialsMatrixDataSparseColumn(SUNMatrix mat):
    MatrixData<double>(static_cast<count_t>(SM_ROWS_S(mat)),
                       static_cast<count_t>(SM_COLUMNS_S(mat))),
    J(mat)
{
}

void SundialsMatrixDataSparseColumn::clear()
{
    SUNMatZero(J);
}
void SundialsMatrixDataSparseColumn::assign(index_t row, index_t col, double num)
{
    int sti = SM_INDEXPTRS_S(J)[col];
    int stp = SM_INDEXPTRS_S(J)[col + 1];
    auto st = SM_INDEXVALS_S(J) + sti;
    while (sti < stp) {
        if (*st == static_cast<int>(row)) {
            SM_DATA_S(J)[sti] += num;
            break;
        }
        ++st;
        ++sti;
    }
}

void SundialsMatrixDataSparseColumn::setMatrix(SUNMatrix mat)
{
    J = mat;
    setRowLimit(static_cast<count_t>(SM_ROWS_S(J)));
    setColLimit(static_cast<count_t>(SM_COLUMNS_S(J)));
}

count_t SundialsMatrixDataSparseColumn::size() const
{
    return static_cast<count_t>(SM_INDEXPTRS_S(J)[colLimit()]);
}
count_t SundialsMatrixDataSparseColumn::capacity() const
{
    return static_cast<count_t>(SM_NNZ_S(J));
}
MatrixElement<double> SundialsMatrixDataSparseColumn::element(index_t n) const
{
    MatrixElement<double> ret{};
    ret.row = static_cast<index_t>(SM_INDEXVALS_S(J)[n]);
    auto res =
        std::lower_bound(SM_INDEXPTRS_S(J), &(SM_INDEXPTRS_S(J)[colLimit()]), static_cast<int>(n));
    ret.col = static_cast<index_t>(*res - 1);
    ret.data = SM_DATA_S(J)[n];
    return ret;
}

void SundialsMatrixDataSparseColumn::start()
{
    cur = 0;
    ccol = 0;
}

MatrixElement<double> SundialsMatrixDataSparseColumn::next()
{
    MatrixElement<double> ret{static_cast<index_t>(SM_INDEXVALS_S(J)[cur]),
                              ccol,
                              SM_DATA_S(J)[cur]};
    ++cur;
    if (static_cast<int>(cur) >= SM_INDEXPTRS_S(J)[ccol + 1]) {
        ++ccol;
        if (ccol > colLimit()) {
            --cur;
            --ccol;
        }
    }
    return ret;
}

double SundialsMatrixDataSparseColumn::at(index_t rowN, index_t colN) const
{
    if (static_cast<int>(colN) > SM_COLUMNS_S(J)) {
        return 0.0;
    }
    int sti = SM_INDEXPTRS_S(J)[colN];
    int stp = SM_INDEXPTRS_S(J)[colN + 1];
    for (int kk = sti; kk < stp; ++kk) {
        if (SM_INDEXVALS_S(J)[kk] == static_cast<int>(rowN)) {
            return SM_DATA_S(J)[kk];
        }
    }
    return 0.0;
}

}  // namespace griddyn::solvers
