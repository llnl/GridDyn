/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LISTMAINTAINER_H_
#define LISTMAINTAINER_H_

#include "../gridComponentHelperClasses.h"
#include <functional>
#include <vector>

template<class Y>
class matrixData;

namespace griddyn {
class gridPrimary;
class stateData;

/** @brief helper class for areas to maintain lists of objects used for execution of each mode
used in the area class*/
class listMaintainer {
  public:
    bool parResid = false;  //!< indicator that the residual should run in parallel
    bool parJac = false;  //!< indicator that the Jacobian should run in parallel
    bool parDeriv = false;  //!< indicator that the derivative should run in parallel
    bool parAlgebraic = false;  //!< indicator that the algebraic update should run in parallel
  private:
    std::vector<gridPrimary*> preExObjs;  //!< lists of all the objects that request pre-execution
    std::vector<std::vector<gridPrimary*>>
        objectLists;  //!< lists of all the objects with states in a certain mode
    std::vector<std::vector<gridPrimary*>>
        partialLists;  //!< list of all the non preEx object with states in a certain mode
    std::vector<solverMode> sModeLists;  //!< the list of solverModes relevant to each list

  public:
    listMaintainer();
    /** generate a list of the object that requires a preEx call*/
    void makePreList(const std::vector<gridPrimary*>& possObjs);
    /** make the list of objects for a certain mode*/
    void makeList(const solverMode& sMode, const std::vector<gridPrimary*>& possObjs);
    /** append a set of objects to preexisting lists*/
    void appendList(const solverMode& sMode, const std::vector<gridPrimary*>& possObjs);

    void jacobianElements(const IOdata& inputs,
                          const stateData& sD,
                          matrixData<double>& md,
                          const IOlocs& inputLocs,
                          const solverMode& sMode);
    void preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode);
    void residual(const IOdata& inputs,
                  const stateData& sD,
                  double resid[],
                  const solverMode& sMode);
    void algebraicUpdate(const IOdata& inputs,
                         const stateData& sD,
                         double update[],
                         const solverMode& sMode,
                         double alpha);
    void derivative(const IOdata& inputs,
                    const stateData& sD,
                    double deriv[],
                    const solverMode& sMode);

    void delayedResidual(const IOdata& inputs,
                         const stateData& sD,
                         double resid[],
                         const solverMode& sMode);
    void delayedDerivative(const IOdata& inputs,
                           const stateData& sD,
                           double deriv[],
                           const solverMode& sMode);
    void delayedJacobian(const IOdata& inputs,
                         const stateData& sD,
                         matrixData<double>& md,
                         const IOlocs& inputLocs,
                         const solverMode& sMode);
    void delayedAlgebraicUpdate(const IOdata& inputs,
                                const stateData& sD,
                                double update[],
                                const solverMode& sMode,
                                double alpha);

    /** check if a list is valid*/
    bool isListValid(const solverMode& sMode) const;
    void invalidate(const solverMode& sMode);
    void invalidate();

    /** get a const iterator for the beginning of a list of particular solverMode*/
    decltype(objectLists[0].cbegin()) cbegin(const solverMode& sMode) const;
    /** get a const iterator for the end of a list of particular solverMode*/
    decltype(objectLists[0].cbegin()) cend(const solverMode& sMode) const;

    /** get an iterator for the beginning of a list of particular solverMode*/
    decltype(objectLists[0].begin()) begin(const solverMode& sMode);

    /** get an iterator for the end of a list of particular solverMode*/
    decltype(objectLists[0].begin()) end(const solverMode& sMode);

    /** get a reverse iterator for the beginning of a list of particular solverMode*/
    decltype(objectLists[0].rbegin()) rbegin(const solverMode& sMode);
    /** get a reverse iterator for the end of a list of particular solverMode*/
    decltype(objectLists[0].rbegin()) rend(const solverMode& sMode);
};

}  // namespace griddyn
#endif
