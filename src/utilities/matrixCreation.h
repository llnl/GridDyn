/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#ifndef _MATRIX_CREATION_H_
#define _MATRIX_CREATION_H_
#pragma once
#include "matrixData.hpp"
#include <memory>

std::unique_ptr<matrixData<double>> makeSparseMatrix(count_t size, count_t maxElements);

#endif
