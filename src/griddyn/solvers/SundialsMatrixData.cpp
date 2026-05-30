/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SundialsMatrixData.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace griddyn::solvers {
std::unique_ptr<MatrixData<double>> makeSundialsMatrixData(SUNMatrix j)
{
    switch (SUNMatGetID(j)) {
        case SUNMATRIX_DENSE:
            return std::make_unique<SundialsMatrixDataDense>(j);
        case SUNMATRIX_SPARSE:
            if (SM_SPARSETYPE_S(j) == CSR_MAT) {
                return std::make_unique<SundialsMatrixDataSparseRow>(j);
            } else {
                return std::make_unique<SundialsMatrixDataSparseColumn>(j);
            }
        case SUNMATRIX_CUSTOM:
        default:
            return nullptr;
    }
}

}  // namespace griddyn::solvers
