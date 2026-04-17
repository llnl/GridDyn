/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sundialsMatrixData.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace griddyn {
namespace solvers {
    std::unique_ptr<matrixData<double>> makeSundialsMatrixData(SUNMatrix J)
    {
        switch (SUNMatGetID(J)) {
            case SUNMATRIX_DENSE:
                return std::make_unique<sundialsMatrixDataDense>(J);
            case SUNMATRIX_SPARSE:
                if (SM_SPARSETYPE_S(J) == CSR_MAT) {
                    return std::make_unique<sundialsMatrixDataSparseRow>(J);
                } else {
                    return std::make_unique<sundialsMatrixDataSparseColumn>(J);
                }
            case SUNMATRIX_CUSTOM:
            default:
                return nullptr;
        }
    }

}  // namespace solvers

}  // namespace griddyn
