/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "matrixCreation.h"

#include "matrixDataSparseSM.hpp"
#include <memory>

std::unique_ptr<matrixData<double>> makeSparseMatrix(count_t size, count_t maxElements)
{
    if (size < 65535) {
        if (size < 100) {
            return std::make_unique<matrixDataSparseSMB<0, std::uint32_t>>(maxElements);
        }
        if (size < 1000) {
            return std::make_unique<matrixDataSparseSMB<1, std::uint32_t>>(maxElements);
        }
        if (size < 20000) {
            return std::make_unique<matrixDataSparseSMB<2, std::uint32_t>>(maxElements);
        }
        return std::make_unique<matrixDataSparseSMB<2, std::uint32_t>>(maxElements);
    }
    return std::make_unique<matrixDataSparseSMB<2, std::uint64_t>>(maxElements);
}
