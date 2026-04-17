/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sundialsMatrixData.h"
#include <algorithm>
#include <cstring>

namespace griddyn::solvers {
sundialsMatrixDataDense::sundialsMatrixDataDense(SUNMatrix mat):
    matrixData<double>(static_cast<count_t>(SM_ROWS_D(mat)),
                       static_cast<count_t>(SM_COLUMNS_D(mat))),
    J(mat)
{
}
void sundialsMatrixDataDense::clear()
{
    SUNMatZero(J);
}
void sundialsMatrixDataDense::assign(index_t X, index_t Y, double num)
{
    SM_ELEMENT_D(J, X, Y) += num;
}
void sundialsMatrixDataDense::setMatrix(SUNMatrix mat)
{
    J = mat;
    setRowLimit(static_cast<count_t>(SM_ROWS_D(J)));
    setColLimit(static_cast<count_t>(SM_COLUMNS_D(J)));
}

count_t sundialsMatrixDataDense::size() const
{
    return static_cast<count_t>(SM_ROWS_D(J) * SM_COLUMNS_D(J));
}
count_t sundialsMatrixDataDense::capacity() const
{
    return static_cast<count_t>(SM_ROWS_D(J) * SM_COLUMNS_D(J));
}
matrixElement<double> sundialsMatrixDataDense::element(index_t N) const
{
    return {N % static_cast<index_t>(SM_COLUMNS_D(J)),
            N / static_cast<index_t>(SM_COLUMNS_D(J)),
            SM_DATA_D(J)[N]};
}

double sundialsMatrixDataDense::at(index_t rowN, index_t colN) const
{
    return SM_ELEMENT_D(J, rowN, colN);
}
}  // namespace griddyn::solvers
