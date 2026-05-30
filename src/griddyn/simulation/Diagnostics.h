/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreDefinitions.hpp"
#include <memory>
#include <string>
#include <utility>

template<class X>
class MatrixData;

namespace griddyn {
class GridDynSimulation;
class GridComponent;
class SolverMode;
class SolverInterface;
class CoreObject;

inline constexpr double resid_check_tol = 1e-5;
inline constexpr double jac_check_tol = 1e-5;
/** @brief function check on the Jacobian
  function does a comparison between the computed Jacobian via the jacobianElements function call
and a numerically calculated version from the residual,  It will not check Jacobian elements
dependent on other state derivatives This function is mostly useful for diagnosing problems and is
used throughout the test suite
@param[in] gds the GridDynSimulation object to test
@param[in] queryMode the SolverMode to check the Jacobian for
@param[in] jacTol  the tolerance to check matches
@param[in] useStateNames  set to true to collect and print state names (vs numbers) for any mismatch
on the Jacobian check
@return the number of mismatches
*/
int jacobianCheck(GridDynSimulation* gds,
                  const SolverMode& queryMode,
                  double jacTol = jac_check_tol,
                  bool useStateNames = false);

/** @brief do a residual check
  function checks for any non-zero residuals usually used after an initialization or step.
@param[in] gds the griddynSimulation object to test
@param[in] sMode the SolverMode to check the residual
@param[in] residTol  the tolerance to check matches
@param[in] useStateNames  set to true to collect and print state names (vs numbers) for any mismatch
on the Jacobian check
@return the number of mismatches
*/
int residualCheck(GridDynSimulation* gds,
                  const SolverMode& sMode,
                  double residTol = resid_check_tol,
                  bool useStateNames = false);
/** @brief do a residual check
  function checks for any non-zero residuals usually used after an initialization or step.
@param[in] gds the griddynSimulation object to test
@param[in] time the time to check the residual
@param[in] sMode the SolverMode to check the residual
@param[in] residTol  the tolerance to check matches
@param[in] useStateNames  set to true to collect and print state names (vs numbers) for any mismatch
on the Jacobian check
@return the number of mismatches
*/
int residualCheck(GridDynSimulation* gds,
                  CoreTime time,
                  const SolverMode& sMode,
                  double residTol = resid_check_tol,
                  bool useStateNames = false);

std::pair<double, int> checkResid(GridDynSimulation* gds,
                                  const std::shared_ptr<SolverInterface>& sd);

std::pair<double, int>
    checkResid(GridDynSimulation* gds, CoreTime time, const std::shared_ptr<SolverInterface>& sd);

std::pair<double, int> checkResid(GridDynSimulation* gds, CoreTime time, const SolverMode& sMode);

int algebraicCheck(GridDynSimulation* gds,
                   CoreTime time,
                   const SolverMode& sMode,
                   double algTol = resid_check_tol,
                   bool useStateNames = false);

int derivativeCheck(GridDynSimulation* gds,
                    CoreTime time,
                    const SolverMode& sMode,
                    double derivTol = resid_check_tol,
                    bool useStateNames = false);

/** @brief do a convergence test on the solver
 */
void dynamicSolverConvergenceTest(GridDynSimulation* gds,
                                  const SolverMode& sMode,
                                  const std::string& file,
                                  count_t pts = 100000,
                                  int mode = 0);

/** @brief print out the structure and count of the Jacobian entries and counts
@param[in] md the matrix data object to analyze
@param[in] gds the GridDynSimulation object to work with
@param[in] sMode the solver mode in use
*/
void jacobianAnalysis(MatrixData<double>& md,
                      GridDynSimulation* gds,
                      const SolverMode& sMode,
                      int level);

/** @brief check object equivalence
@details checks if the objects are equivalent in function and if instructed spits out messages of
the differences
@param[in] obj1 the first object to compare
@param[in] obj2 the second object to compare
@param[in] printMessage bool indicating that messages should be printed
@return true if the objects are deemed equivalent*/

bool checkObjectEquivalence(const CoreObject* obj1,
                            const CoreObject* obj2,
                            bool printMessage = true);

/** @brief check the state sizes and print out state size information in a nice format for each
object in a hierarchy
@param[in] comp the component to print the state sizes for
@param[in] sMode the solver mode of the states to print
*/
void printStateSizes(const GridComponent* comp, const SolverMode& sMode);
}  // namespace griddyn
