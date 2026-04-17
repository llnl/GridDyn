#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_POWERFLOWERRORRECOVERY_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_POWERFLOWERRORRECOVERY_H_

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

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
    enum class recovery_return_codes {
        more_options,
        out_of_options,
    };

    /** @brief constructor
  @param[in] gds the gridDynSimulation object to work from
  @param[in] sd the SolverInterface object to work from
  */
    powerFlowErrorRecovery(gridDynSimulation* gds, std::shared_ptr<SolverInterface> sd);

    /** @brief virtual destructor*/
    virtual ~powerFlowErrorRecovery();

    /** @brief attempt the various fixes in order
  @param[in] optional error code value
  @return recovery_return_codes::more_options if attemptFix can be called again without reset
  recovery_return_codes::out_of_options if no more fix attempts are available
  */
    virtual recovery_return_codes attemptFix(int error_code = 0);

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

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_POWERFLOWERRORRECOVERY_H_
