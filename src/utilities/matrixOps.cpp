/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "matrixOps.h"

#include <cstring>
#include <vector>

std::vector<double> matrixDataMultiply(matrixData<double>& md, const double vec[])
{
    std::vector<double> res(md.rowLimit());
    matrixDataMultiply(md, vec, res.data());
    return res;
}

void matrixDataMultiply(matrixData<double>& md, const double vec[], double res[])
{
    memset(res, 0, sizeof(double) * md.rowLimit());
    auto sz = md.size();
    md.start();
    index_t ii = 0;
    while (ii++ < sz) {
        auto element = md.next();
        res[element.row] += element.data * vec[element.col];
    }
}
