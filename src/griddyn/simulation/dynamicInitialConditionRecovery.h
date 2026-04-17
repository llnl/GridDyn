/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_DYNAMICINITIALCONDITIONRECOVERY_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_DYNAMICINITIALCONDITIONRECOVERY_H_

#include <memory>

namespace griddyn {
class gridDynSimulation;
class SolverInterface;

/** @brief the purpose of this class is to try to recover a valid initial condition for dynamic
 * simulations*/
class dynamicInitialConditionRecovery {
  public:
    /** @brief constructor
    @param[in] gds the gridDynSimulation object to work from
    @param[in] sd the SolverInterface object to work from
    */
    dynamicInitialConditionRecovery(gridDynSimulation* gds, std::shared_ptr<SolverInterface> sd);

    /** @brief virtual destructor*/
    virtual ~dynamicInitialConditionRecovery();

    /** @brief attempt the various fixes in order
    @return recovery_return_codes::more_options if attemptFix can be called again without reset
    recovery_return_codes::out_of_options if no more fix attempts are available
    */
    virtual int attemptFix();

    /** @brief reset the fix counter so it can try again*/
    void reset();
    /** @brief update recovery mechanism to use a different solver
    @param[in] sd the new solver Data object to use
    */
    void updateInfo(std::shared_ptr<SolverInterface> sd);

    /** @brief return the number of attempts taken so far
    @return the number of attempts
    */
    int attempts() const;

    bool hasMoreFixes() const;

  protected:
    int attempt_number = 0;  //!< the current attempt number
    gridDynSimulation* sim;  //!< the gridDynsimulation to work from
    std::shared_ptr<SolverInterface> solver;  //!< the SolverInterface to use

    int dynamicFix1();
    int dynamicFix2();
    int dynamicFix3();
    int dynamicFix4();
    int dynamicFix5();

    int lowVoltageCheck();
};

}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_DYNAMICINITIALCONDITIONRECOVERY_H_
