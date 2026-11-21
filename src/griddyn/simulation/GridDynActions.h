/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {

/** A command error with a category that callers can use without parsing its message. */
class ActionError: public std::runtime_error {
  public:
    enum class Kind { SYNTAX, TARGET, UNSUPPORTED, EXECUTION };

    ActionError(Kind kind, const std::string& message): std::runtime_error(message), kind_(kind) {}
    Kind kind() const noexcept { return kind_; }

  private:
    Kind kind_;
};

/** @brief class to define action and parameters for GridDyn operations
 */
class GridDynAction {
  public:
    /** @brief the list of possible actions
     */
    enum class GdAction {
        IGNORE_ACTION,  //!< null action
        SET_ACTION,  //!< SET a parameter
        SETSOLVER,  //!< set a parameter in the solver
        SETALL,  //!< set a parameter in all the models of a particular type
        PRINT,  //!< PRINT a variable
        INITIALIZE,  //!< INITIALIZE the models
        POWERFLOW,  //!< run a power flow
        RESET,  //!< RESET the models
        ITERATE,  //!< perform an iterative power flow
        EVENTMODE,  //!< run in event Mode
        DYNAMIC_DAE,  //!< do a dynamic calculation using the DAE solver
        DYNAMIC_PART,  //!< do a dynamic calculation using the partitioned solver
        DYNAMIC_DECOUPLED,  //!< do a dynamic calculation using the decoupled mode
        STEP,  //!< perform a single STEP operation
        RUN,  //!< advance the model using its configured solution method
        SAVE,  //!< SAVE the results
        CHECK,  //!< CHECK the current results in various ways
        LOAD,  //!< LOAD a state into the simulation
        ADD,  //!< ADD a model to the simulation
        ROLLBACK,  //!< Restore a checkpoint or solver snapshot from a file
        CHECKPOINT,  //!< Capture solver and declared component state to a file
        CONTINGENCY,  //!< perform a CONTINGENCY analysis
        CONTINUATION,  //!< perform a CONTINUATION analysis
        INVALID,  //!< INVALID command
        DYNAMIC_DEFAULT,  //!< run dynamics using the simulation's configured solver method
        OPF,  //!< solve DC optimal power flow
        ACTION_FILE,  //!< execute the actions in a script file
        GET_ACTION  //!< read a field or list its model's declared fields
    };
    GdAction command = GdAction::IGNORE_ACTION;  //!< the command to execute
    std::string string1;  //!< string parameter 1 of the action
    std::string string2;  //!< string parameter 2 of the action
    int val_int1{-1};  //!< integer parameter of action
    int val_int2{-1};  //!< second integer parameter of the action
    int flag{0};  //!< additional flag values for various purposes
    double val_double{kNullVal};  //!< double parameter 1 of the action
    double val_double2{kNullVal};  //!< double parameter 2 of the action
    std::vector<index_t> monitorBuses;  //!< continuation bus user IDs, in output order

    /** @brief constructor*/
    GridDynAction() = default;
    /** @brief constructor taking a command
    @param[in] action command
    */
    /*IMPLICIT*/ GridDynAction(GdAction action) noexcept;
    /** @brief constructor with action string
    @param[in] operation  a string containing the information for a specific action*/
    /*IMPLICIT*/ GridDynAction(std::string_view operation);

    /** @brief fill an actions parameters based on a string
    @param[in] operation  a string containing the information for a specific action*/
    void process(std::string_view operation);

    /** Return the action's canonical command name. */
    std::string_view commandName() const noexcept;

    /** @brief reset the action to base state*/
    void reset();
};
}  // namespace griddyn
