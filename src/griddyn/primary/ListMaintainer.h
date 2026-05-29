/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GridComponentHelperClasses.h"
#include <functional>
#include <vector>

template<class Y>
class matrixData;

namespace griddyn {
class GridPrimary;
class stateData;

/** @brief helper class for areas to maintain lists of objects used for execution of each mode
used in the area class*/
class ListMaintainer {
  public:
    bool parResid = false;  //!< indicator that the residual should run in parallel
    bool parJac = false;  //!< indicator that the Jacobian should run in parallel
    bool parDeriv = false;  //!< indicator that the derivative should run in parallel
    bool parAlgebraic = false;  //!< indicator that the algebraic update should run in parallel
  private:
    std::vector<GridPrimary*> preExObjs;  //!< lists of all the objects that request pre-execution
    std::vector<std::vector<GridPrimary*>>
        objectLists;  //!< lists of all the objects with states in a certain mode
    std::vector<std::vector<GridPrimary*>>
        partialLists;  //!< list of all the non preEx object with states in a certain mode
    std::vector<SolverMode> sModeLists;  //!< the list of solverModes relevant to each list

  public:
    ListMaintainer();
    /** generate a list of the object that requires a preEx call*/
    void makePreList(const std::vector<GridPrimary*>& possObjs);
    /** make the list of objects for a certain mode*/
    void makeList(const SolverMode& sMode, const std::vector<GridPrimary*>& possObjs);
    /** append a set of objects to preexisting lists*/
    void appendList(const SolverMode& sMode, const std::vector<GridPrimary*>& possObjs);

    void jacobianElements(const IOdata& inputs,
                          const stateData& stateDataValue,
                          matrixData<double>& matrixDataValue,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode);
    void preEx(const IOdata& inputs, const stateData& stateDataValue, const SolverMode& sMode);
    void residual(const IOdata& inputs,
                  const stateData& stateDataValue,
                  double resid[],
                  const SolverMode& sMode);
    void algebraicUpdate(const IOdata& inputs,
                         const stateData& stateDataValue,
                         double update[],
                         const SolverMode& sMode,
                         double alpha);
    void derivative(const IOdata& inputs,
                    const stateData& stateDataValue,
                    double deriv[],
                    const SolverMode& sMode);

    void delayedResidual(const IOdata& inputs,
                         const stateData& stateDataValue,
                         double resid[],
                         const SolverMode& sMode);
    void delayedDerivative(const IOdata& inputs,
                           const stateData& stateDataValue,
                           double deriv[],
                           const SolverMode& sMode);
    void delayedJacobian(const IOdata& inputs,
                         const stateData& stateDataValue,
                         matrixData<double>& matrixDataValue,
                         const IOlocs& inputLocs,
                         const SolverMode& sMode);
    void delayedAlgebraicUpdate(const IOdata& inputs,
                                const stateData& stateDataValue,
                                double update[],
                                const SolverMode& sMode,
                                double alpha);

    /** check if a list is valid*/
    bool isListValid(const SolverMode& sMode) const;
    void invalidate(const SolverMode& sMode);
    void invalidate();

    /** get a const iterator for the beginning of a list of particular SolverMode*/
    decltype(objectLists[0].cbegin()) cbegin(const SolverMode& sMode) const;
    /** get a const iterator for the end of a list of particular SolverMode*/
    decltype(objectLists[0].cbegin()) cend(const SolverMode& sMode) const;

    /** get an iterator for the beginning of a list of particular SolverMode*/
    decltype(objectLists[0].begin()) begin(const SolverMode& sMode);

    /** get an iterator for the end of a list of particular SolverMode*/
    decltype(objectLists[0].begin()) end(const SolverMode& sMode);

    /** get a reverse iterator for the beginning of a list of particular SolverMode*/
    decltype(objectLists[0].rbegin()) rbegin(const SolverMode& sMode);
    /** get a reverse iterator for the end of a list of particular SolverMode*/
    decltype(objectLists[0].rbegin()) rend(const SolverMode& sMode);
};

}  // namespace griddyn
