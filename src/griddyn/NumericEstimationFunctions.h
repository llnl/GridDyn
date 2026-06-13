/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridDynDefinitions.hpp"
#include <vector>

template<class X>
class MatrixData;

namespace griddyn {
class GridComponent;
class StateData;
class SolverMode;

/**
*@brief numerically compute the partial derivatives of the internal states with respect to inputs
and other internal states
@details numerically computes the elements of the Jacobian matrix of the internal states of the
given object
@param[in] comp the object to compute the partial derivatives for
@param[in] inputs the inputs for the secondary object
* @param[in] stateData the current state data for the simulation
* @param[out] matrixData the array to store the information in
* @param[in] inputLocs the vector of input argument locations
* @param[in] sMode the operations mode
**/

void numericJacobianCalculation(GridComponent* comp,
                                const IOdata& inputs,
                                const StateData& stateData,
                                MatrixData<double>& matrixData,
                                const IOlocs& inputLocs,
                                const SolverMode& sMode);

/** @brief get a vector of all the local state locations of an object
@param[in] comp  the object get all the state locations
@param[in] sMode the solver mode to get the locations for
@return a vector containing the indices of the states
*/
std::vector<index_t> getObjectLocalStateIndices(const GridComponent* comp, const SolverMode& sMode);

}  // namespace griddyn
