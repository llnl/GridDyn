/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
 *  @brief define some operations related to matrixData objects
 */
#include "matrixData.hpp"
#include <vector>
/** multiply a matrix data object by a vector
@details assumes vec has the appropriate size otherwise undefined behavior will occur
@param[in] md the matrix to multiply size MxN
@param[in] vec the vector data to multiply at least size N
@param[out] res the location to store the results
@return a vector with the results the vector will be of length M
*/
void matrixDataMultiply(matrixData<double>& md, const double vec[], double res[]);

/** multiply a matrix data object by a vector
@details assumes vec has the appropriate size otherwise undefined behavior will occur
@param[in] md the matrix to multiply size MxN
@param[in] vec the vector data to multiply at least size N
@return a vector with the results the vector will be of length M
*/
std::vector<double> matrixDataMultiply(matrixData<double>& md, const double vec[]);
