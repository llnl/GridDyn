/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "MatrixData.hpp"
#include <memory>

std::unique_ptr<MatrixData<double>> makeSparseMatrix(count_t size, count_t maxElements);
