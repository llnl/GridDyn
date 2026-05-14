/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <memory>

namespace griddyn {
class gridDynSimulation;
class SolverInterface;

/** @brief the purpose of this class is to try to recover a valid power flow solution after a solver
 * failure*/
class powerFlowErrorRecovery {
  public:
    /** @brief enumeration describing possible return options
     */
    enum class RecoveryReturnCodes {
        MORE_OPTIONS,
        OUT_OF_OPTIONS,
    };

    /** @brief constructor
  @param[in] gds the gridDynSimulation object to work from
  @param[in] solverData the SolverInterface object to work from
  */
    powerFlowErrorRecovery(gridDynSimulation* gds,
                           std::shared_ptr<SolverInterface> solverData);

    /** @brief virtual destructor*/
    virtual ~powerFlowErrorRecovery();

    /** @brief attempt the various fixes in order
  @param[in] error_code optional error code value
  @return RecoveryReturnCodes::MORE_OPTIONS if attemptFix can be called again without reset
  RecoveryReturnCodes::OUT_OF_OPTIONS if no more fix attempts are available
  */
    virtual RecoveryReturnCodes attemptFix(int error_code = 0);

    /** @brief reset the fix counter so it can try again*/
    void reset();
    /** @brief update recovery mechanism to use a different solver
  @param[in] solverData the new solver Data object to use
  */
    void updateInfo(std::shared_ptr<SolverInterface> solverData);

    /** @brief return the number of attempts taken so far
  @return the number of attempts
  */
    int attempts() const;

  protected:
    int attempt_number = 0;  //!< the current attempt number
    gridDynSimulation* sim;  //!< the gridDynsimulation to work from
    std::shared_ptr<SolverInterface> solver;  //!< the SolverInterface to use

    bool powerFlowFix1();
    bool powerFlowFix2();
    bool powerFlowFix3();
    bool powerFlowFix4();
    bool powerFlowFix5();

    bool lowVoltageFix();
};

}  // namespace griddyn
