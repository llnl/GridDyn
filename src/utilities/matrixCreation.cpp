/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

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
