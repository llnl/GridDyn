/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SundialsMatrixData.h"
#include <algorithm>
#include <cstring>

namespace griddyn::solvers {
SundialsMatrixDataDense::SundialsMatrixDataDense(SUNMatrix mat):
    MatrixData<double>(static_cast<count_t>(SM_ROWS_D(mat)),
                       static_cast<count_t>(SM_COLUMNS_D(mat))),
    J(mat)
{
}
void SundialsMatrixDataDense::clear()
{
    SUNMatZero(J);
}
void SundialsMatrixDataDense::assign(index_t x, index_t y, double num)
{
    SM_ELEMENT_D(J, x, y) += num;
}
void SundialsMatrixDataDense::setMatrix(SUNMatrix mat)
{
    J = mat;
    setRowLimit(static_cast<count_t>(SM_ROWS_D(J)));
    setColLimit(static_cast<count_t>(SM_COLUMNS_D(J)));
}

count_t SundialsMatrixDataDense::size() const
{
    return static_cast<count_t>(SM_ROWS_D(J) * SM_COLUMNS_D(J));
}
count_t SundialsMatrixDataDense::capacity() const
{
    return static_cast<count_t>(SM_ROWS_D(J) * SM_COLUMNS_D(J));
}
MatrixElement<double> SundialsMatrixDataDense::element(index_t n) const
{
    return {n % static_cast<index_t>(SM_COLUMNS_D(J)),
            n / static_cast<index_t>(SM_COLUMNS_D(J)),
            SM_DATA_D(J)[n]};
}

double SundialsMatrixDataDense::at(index_t rowN, index_t colN) const
{
    return SM_ELEMENT_D(J, rowN, colN);
}
}  // namespace griddyn::solvers
