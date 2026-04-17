/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sundialsMatrixData.h"
#include <algorithm>
#include <cstring>

namespace griddyn::solvers {
sundialsMatrixDataSparseColumn::sundialsMatrixDataSparseColumn(SUNMatrix mat):
    matrixData<double>(static_cast<count_t>(SM_ROWS_S(mat)),
                       static_cast<count_t>(SM_COLUMNS_S(mat))),
    J(mat)
{
}

void sundialsMatrixDataSparseColumn::clear()
{
    SUNMatZero(J);
}
void sundialsMatrixDataSparseColumn::assign(index_t row, index_t col, double num)
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

void sundialsMatrixDataSparseColumn::setMatrix(SUNMatrix mat)
{
    J = mat;
    setRowLimit(static_cast<count_t>(SM_ROWS_S(J)));
    setColLimit(static_cast<count_t>(SM_COLUMNS_S(J)));
}

count_t sundialsMatrixDataSparseColumn::size() const
{
    return static_cast<count_t>(SM_INDEXPTRS_S(J)[colLimit()]);
}
count_t sundialsMatrixDataSparseColumn::capacity() const
{
    return static_cast<count_t>(SM_NNZ_S(J));
}
matrixElement<double> sundialsMatrixDataSparseColumn::element(index_t N) const
{
    matrixElement<double> ret;
    ret.row = static_cast<index_t>(SM_INDEXVALS_S(J)[N]);
    auto res =
        std::lower_bound(SM_INDEXPTRS_S(J), &(SM_INDEXPTRS_S(J)[colLimit()]), static_cast<int>(N));
    ret.col = static_cast<index_t>(*res - 1);
    ret.data = SM_DATA_S(J)[N];
    return ret;
}

void sundialsMatrixDataSparseColumn::start()
{
    cur = 0;
    ccol = 0;
}

matrixElement<double> sundialsMatrixDataSparseColumn::next()
{
    matrixElement<double> ret{static_cast<index_t>(SM_INDEXVALS_S(J)[cur]),
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

double sundialsMatrixDataSparseColumn::at(index_t rowN, index_t colN) const
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
