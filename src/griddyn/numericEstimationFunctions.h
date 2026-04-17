#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynDefinitions.hpp"
#include <vector>

template<class X>
class matrixData;

namespace griddyn {
class gridComponent;
class stateData;
class solverMode;

/**
*@brief numerically compute the partial derivatives of the internal states with respect to inputs
and other internal states
@details numerically computes the elements of the Jacobian matrix of the internal states of the
given object
@param[in] obj the object to compute the partial derivatives for
@param[in] inputs the inputs for the secondary object
* @param[in] sD the current state data for the simulation
* @param[out] md  the array to store the information in
* @param[in] inputLocs the vector of input argument locations
* @param[in] sMode the operations mode
**/

void numericJacobianCalculation(gridComponent* comp,
                                const IOdata& inputs,
                                const stateData& sD,
                                matrixData<double>& md,
                                const IOlocs& inputLocs,
                                const solverMode& sMode);

/**
@brief function to copy the local state of an object from one data to another
@param[in] obj  the object to copy the state for
@param[in] state the current state vector
@param[out]  the location to copy the state information
@param[in] sMode the solver mode corresponding to the state
*/
void copyObjectLocalState(const gridComponent* comp,
                          const double state[],
                          double newstate[],
                          const solverMode& sMode);

/** @brief get a vector of all the local state locations of an object
@param[in] obj  the object get all the state locations
@param[in] sMode the solver mode to get the locations for
@return a vector containing the indices of the states
*/
std::vector<index_t> getObjectLocalStateIndices(const gridComponent* comp, const solverMode& sMode);

}  // namespace griddyn
