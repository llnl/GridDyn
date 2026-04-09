/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_FAULTRESETRECOVERY_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_FAULTRESETRECOVERY_H_

#include <memory>
#include <vector>

namespace griddyn {
class gridDynSimulation;
class SolverInterface;

enum class reset_levels;

/** @brief the purpose of this class is to try to recover a valid initial condition for dynamic
 * simulations*/
class faultResetRecovery {
  protected:
    std::vector<double> initVolts;

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
    faultResetRecovery(gridDynSimulation* gds, std::shared_ptr<SolverInterface> sd);

    /** @brief virtual destructor*/
    virtual ~faultResetRecovery();

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

    int faultResetFix1();
    int faultResetFix2(reset_levels rlevel);
    int faultResetFix3();
    int faultResetFix4();
};

}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_FAULTRESETRECOVERY_H_
