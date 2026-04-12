/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_GRIDDYNACTIONS_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_GRIDDYNACTIONS_H_

#include "../gridDynDefinitions.hpp"
#include <string>

namespace griddyn {
// TODO(phlpt): Use variant and string_view.

/** @brief class to define action and parameters for GridDyn operations
 */
class gridDynAction {
  public:
    /** @brief the list of possible actions
     */
    enum class gd_action_t {
        ignore,  //!< null action
        set,  //!< set a parameter
        setsolver,  //!< set a parameter in the solver
        setall,  //!< set a parameter in all the models of a particular type
        print,  //!< print a variable
        initialize,  //!< initialize the models
        powerflow,  //!< run a power flow
        reset,  //!< reset the models
        iterate,  //!< perform an iterative power flow
        eventmode,  //!< run in event Mode
        dynamicDAE,  //!< do a dynamic calculation using the DAE solver
        dynamicPart,  //!< do a dynamic calculation using the partitioned solver
        dynamicDecoupled,  //!< do a dynamic calculation using the decoupled mode
        step,  //!< perform a single step operation
        run,  //!< run the script or the model based on stored parameters
        save,  //!< save the results
        check,  //!< check the current results in various ways
        load,  //!< load a state into the simulation
        add,  //!< add a model to the simulation
        rollback,  //!< rollback the simulation to a particular time point
        checkpoint,  //!< checkpoint the complete system state
        contingency,  //!< perform a contingency analysis
        continuation,  //!< perform a continuation analysis
        invalid  //!< invalid command
    };
    gd_action_t command = gd_action_t::ignore;  //!< the command to execute
    std::string string1;  //!< string parameter 1 of the action
    std::string string2;  //!< string parameter 2 of the action
    int val_int = -1;  //!< integer parameter of action
    double val_double = kNullVal;  //!< double parameter 1 of the action
    double val_double2 = kNullVal;  //!< double parameter 2 of the action

    /** @brief constructor*/
    gridDynAction() = default;
    /** @brief constructor taking a command
    @param[in] action command
    */
    /*IMPLICIT*/ gridDynAction(gd_action_t action) noexcept;
    /** @brief constructor with action string
    @param[in] operation  a string containing the information for a specific action*/
    /*IMPLICIT*/ gridDynAction(const std::string& operation);

    /** @brief fill an actions parameters based on a string
    @param[in] operation  a string containing the information for a specific action*/
    void process(const std::string& operation);

    /** @brief reset the action to base state*/
    void reset();
};
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SIMULATION_GRIDDYNACTIONS_H_
