/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

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
