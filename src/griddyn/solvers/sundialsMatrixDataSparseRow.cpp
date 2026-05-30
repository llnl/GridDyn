/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SundialsMatrixData.h"
#include <algorithm>
#include <cstring>

namespace griddyn::solvers {
SundialsMatrixDataSparseRow::SundialsMatrixDataSparseRow(SUNMatrix mat):
    MatrixData<double>(static_cast<count_t>(SM_ROWS_S(mat)),
                       static_cast<count_t>(SM_COLUMNS_S(mat))),
    J(mat)
{
}

void SundialsMatrixDataSparseRow::clear()
{
    SUNMatZero(J);
}
void SundialsMatrixDataSparseRow::assign(index_t row, index_t col, double num)
{
    int sti = SM_INDEXPTRS_S(J)[row];
    int stp = SM_INDEXPTRS_S(J)[row + 1];
    auto st = SM_INDEXVALS_S(J) + sti;
    while (sti < stp) {
        if (*st == static_cast<int>(col)) {
            SM_DATA_S(J)[sti] += num;
            break;
        }
        ++st;
        ++sti;
    }
}

void SundialsMatrixDataSparseRow::setMatrix(SUNMatrix mat)
{
    J = mat;
    setRowLimit(static_cast<count_t>(SM_ROWS_S(J)));
    setColLimit(static_cast<count_t>(SM_COLUMNS_S(J)));
}

count_t SundialsMatrixDataSparseRow::size() const
{
    return static_cast<count_t>(SM_INDEXPTRS_S(J)[rowLimit()]);
}
count_t SundialsMatrixDataSparseRow::capacity() const
{
    return static_cast<count_t>(SM_NNZ_S(J));
}
MatrixElement<double> SundialsMatrixDataSparseRow::element(index_t n) const
{
    MatrixElement<double> ret{};
    ret.col = static_cast<index_t>(SM_INDEXVALS_S(J)[n]);
    auto res =
        std::lower_bound(SM_INDEXPTRS_S(J), &(SM_INDEXPTRS_S(J)[rowLimit()]), static_cast<int>(n));
    ret.row = static_cast<index_t>(*res - 1);
    ret.data = SM_DATA_S(J)[n];
    return ret;
}

void SundialsMatrixDataSparseRow::start()
{
    cur = 0;
    crow = 0;
}

MatrixElement<double> SundialsMatrixDataSparseRow::next()
{
    MatrixElement<double> ret{crow,
                              static_cast<index_t>(SM_INDEXVALS_S(J)[cur]),
                              SM_DATA_S(J)[cur]};
    ++cur;
    if (static_cast<int>(cur) >= SM_INDEXPTRS_S(J)[crow + 1]) {
        ++crow;
        if (crow > rowLimit()) {
            --cur;
            --crow;
        }
    }
    return ret;
}

double SundialsMatrixDataSparseRow::at(index_t rowN, index_t colN) const
{
    if (static_cast<int>(rowN) > SM_ROWS_S(J)) {
        return 0.0;
    }
    int sti = SM_INDEXPTRS_S(J)[rowN];
    int stp = SM_INDEXPTRS_S(J)[rowN + 1];
    for (int kk = sti; kk < stp; ++kk) {
        if (SM_INDEXVALS_S(J)[kk] == static_cast<int>(colN)) {
            return SM_DATA_S(J)[kk];
        }
    }
    return 0.0;
}
}  // namespace griddyn::solvers
