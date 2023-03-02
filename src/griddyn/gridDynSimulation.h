/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

/**
@file
@brief define the simulation object itself and several helper classes and enumerations*/

// header files
#include "simulation/gridDynActions.h"
#include "simulation/gridSimulation.h"
// libraries
#include "griddyn/griddyn-config.h"
#include <functional>
#include <queue>
namespace griddyn {
#define SINGLE (1)
#define MULTICORE (2)
#define MULTIPROCESSOR (3)

#define STRINGIFY(x) #x

class Contingency;
class continuationSequence;
class SolverInterface;
class parameterSet;

/** additional flags for the controlFlags bitset*/
enum gd_flags {
    dense_solver = 31,
    power_adjust_enabled = 32,
    dcFlow_initialization = 33,
    parallel_residual_enabled = 34,
    parallel_jacobian_enabled = 35,
    parallel_contingency_enabled = 36,
    mpi_contingency_enabled = 37,
    first_run_limits_only = 38,
    no_reset = 39,
    voltage_constraints_flag = 40,
    record_on_halt_flag = 41,
    no_auto_slack_bus = 42,
    no_auto_disconnect = 43,
    single_step_mode = 44,
    dc_mode = 45,
    force_power_flow = 46,
    power_flow_only = 47,
    no_powerflow_adjustments = 48,
    save_power_flow_data = 49,
    no_powerflow_error_recovery = 50,
    dae_initialization_for_partitioned = 51,
    force_extra_powerflow = 52,
    droop_power_flow = 53,
    save_power_flow_input_data = 54,
    power_flow_input_saved = 55,
};

// for the status flags bitset

// extra local flags
enum gd_extra_flags {
    dcJacComp_flag = object_flag6,
    reset_voltage_flag = object_flag7,
    prev_setall_pqvlimit = object_flag8,
    invalid_state_flag = object_flag9,
    check_reset_voltage_flag = object_flag10,
    powerflow_saved = object_flag11,
    low_bus_voltage = object_flag12,
};

/** @brief helper structure for containing tolerances
 */
struct tolerances {
    double voltageTolerance = 1e-7;  //!< tolerance on the voltage levels
    double angleTolerance = 1e-8;  //!< tolerance on the angles
    double defaultTolerance = 1e-8;  //!< default tolerance on all other variables
    double toleranceRelaxationFactor = 1.0;  //!< relax the tolerances to help the solver
    double rtol = 1e-6;  //!< the relative tolerance
    coreTime timeTol =
        kSmallTime;  //!< the allowable time slop in events.  The time span below which the system
    //! doesn't really care about
};

class Recorder;
class Event;

#define HANDLER_NO_RETURN (-500)

enum class contingency_mode_t;  // forward declare the enumeration
/** @brief the GridDyn Simulation Class
  the gridDynSimulation class contains the mechanics for generating solutions to various power
  systems problems of interest
*/
class gridDynSimulation: public gridSimulation {
  public:
    friend class powerFlowErrorRecovery;
    friend class dynamicInitialConditionRecovery;
    friend class faultResetRecovery;
    //!< define various contingency modes  [probably will be changed in the near future]

    //!< define an enumeration of the dynamic solver methods
    enum class dynamic_solver_methods {
        dae,
        partitioned,
        decoupled,
    };
    /** @brief enumeration of ordering schemes for variables*/
    enum class offset_ordering {
        mixed = 0,  //!< everything is mixed through each other
        grouped = 1,  //!< all similar variables are grouped together (angles, then voltage, then
                      //!< algebraic, then
        //! differential)
        algebraic_grouped = 2,  //!< all the algebraic variables are grouped, then the differential
        voltage_first = 3,  //!< grouped with the voltage coming first
        angle_first = 4,  //!< grouped with the angle coming first
        differential_first = 5,  //!< differential and algebraic grouped with differential first

    };

  protected:
    // storageSpace for SUNDIALS SolverInterface

    std::bitset<64> controlFlags;  //!< storage container for user settable flags
    // ---------------solution mode-------------
    const solverMode* defPowerFlowMode = &cPflowSolverMode;  //!< link to the default powerFlow mode
    const solverMode* defDAEMode = &cDaeSolverMode;  //!< link to the default DAE solver mode
    const solverMode* defDynAlgMode =
        &cDynAlgSolverMode;  //!< link to the default algebraic solver mode
    const solverMode* defDynDiffMode =
        &cDynDiffSolverMode;  //!< link to the default differential solver mode

    dynamic_solver_methods defaultDynamicSolverMethod =
        dynamic_solver_methods::dae;  //!< specifies which dynamic solver method to use if it is not
                                      //!< otherwise
    //! specified.
    offset_ordering default_ordering =
        offset_ordering::mixed;  //!< the default_ordering scheme for state variables
    count_t max_Vadjust_iterations = 30;  //!< maximum number of Voltage adjust iterations
    count_t max_Padjust_iterations = 15;  //!< maximum number of Power adjust iterations
    count_t thread_count = 1;  //!< maximum thread count
    count_t haltCount = 0;  //!< counter for the number of times the solver was halted
    count_t residCount = 0;  //!< counter for the number of times the residual function was called
    count_t evalCount = 0;  //!< counter for the number of times the algUpdateFunction was called
    count_t JacobianCallCount = 0;  //!< counter for the number of calls to the Jacobian function
    count_t rootCount = 0;  //!< counter for the number of roots
    count_t busCount = 0;  //!< counter for the number of buses
    count_t linkCount = 0;  //!< counter for the number of links
    coreTime probeStepTime = coreTime(1e-3);  //!< initial step size
    double powerAdjustThreshold = 0.01;  //!< tolerance on the power adjust step
    coreTime powerFlowStartTime =
        negTime;  //!< power flow start time  if negTime then it computes based on start time;
    struct tolerances tols;  //!< structure of the tolerances

    std::string powerFlowFile;  //!< the power flow output file if any
    std::string powerFlowInputFile;  //!< the power flow before solving the power flow
    std::vector<std::shared_ptr<SolverInterface>> solverInterfaces;  //!< vector of solver data
    std::vector<const double*>
        extraStateInformation;  //!< a vector of additional state information for solveMode pairings
    std::vector<const double*> extraDerivInformation;  //!< a vector of additional derivative
                                                       //!< Information for solverMode pairings
    std::vector<gridComponent*>
        singleStepObjects;  //!< objects which require a state update after time step
    std::vector<gridBus*> slkBusses;  //!< vector of slack buses to aid in powerFlow adjust
    std::queue<gridDynAction> actionQueue;  //!< queue for actions for GridDyn to execute
    std::vector<std::shared_ptr<continuationSequence>>
        continList;  //!< set of continuation sequences to run
    std::vector<std::function<int()>>
        additionalPowerflowSetupFunctions;  //!< set of additional operations to execute after the
                                            //!< PflowInitializeA
    //!< step
  public:
    /** @ constructor to set the name
    @param[in] objName the name of the simulation*/
    explicit gridDynSimulation(const std::string& objName = "gridDynSim_#");
    ~gridDynSimulation();
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    /** @brief set a particular instantiation of the simulation object to be the master for various
    purposes this function along with getInstance is used by external libraries to get particular
    information about the simulation without needing to store a copy of the simulation pointer
    @param[in] gds a pointer to the simulation intended to be the master*/
    static void setInstance(gridDynSimulation* gds);

    /** @brief get the master instance of a gridDynSimulation
    @return a pointer to the master gridDynSimulation object*/
    static gridDynSimulation* getInstance(void);

    // simulation
    /** @brief define an enumeration for the network check level*/
    enum class network_check_type : char {
        full,  //!< a full network check
        simplified  //!< a simplified version of the network check
    };

    /** @brief check the simulation network
     function does checks for connectivity and make sure each subnet has a slack bus attached to it
    @param[in] checkType  the type of network check to perform
    @return in indicating success (0) or failure (non-zero)
    */
    int checkNetwork(
        network_check_type checkType);  // function to do a check on the network and potentially
    // reorder a few things and make sure it is solvable

    /** @brief perform a power flow calculation
  @return in indicating success (0) or failure (non-zero)*/
    int powerflow();

    /** @brief perform a sensitivity analysis
     this function will likely be changing as the sensitivity analysis is more developed
    @return in indicating success (0) or failure (non-zero)*/
    void pFlowSensitivityAnalysis();

    /** @brief perform a continuation power flow analysis
    @param[in] contName the name of the continuation analysis to perform
    @details this function will likely be changing as the continuation analysis develops further
    @return int indicating success (0) or failure (non-zero)*/
    void continuationPowerFlow(const std::string& contName);

    /** @brief function to get the system constraints
      constraints allowable are >0,  <0, >=0,  <=0
    this is not used very often
    @param[out] consData  the array to place the model constraint data
    @param[in] sMode the solver mode corresponding to array locations in consData
    */
    void getConstraints(double consData[], const solverMode& sMode) override;

    /** @brief check if the system has any constraints
     *@return true if there is constraints false if not
     */
    bool hasConstraints() const
    {
        return ((opFlags[has_constraints]) || (controlFlags[voltage_constraints_flag]));
    }

    /**@brief run the simulation until the specified time
    @param[in] t_end  the simulation time to stop defaults to the time given in system parameters
    @return int indicating success (0) or failure (non-zero)*/
    int run(coreTime t_end = negTime) override;

    /**@brief initialize the simulation for power flow at the specified time
    @param[in] time0 the time of the initialization default to 0
    @return int indicating success (0) or failure (non-zero)*/
    int pFlowInitialize(coreTime time0 = negTime);

    /**@brief step the simulation until the next event or stop point
    @param[in] nextStep the maximum time to stop
    @param[out] timeActual the actual time that was achieved
    @return int indicating success (0) or failure (non-zero)*/
    int step(coreTime nextStep, coreTime& timeActual);

    /**@brief step the simulation until the next event or stop point
    @param[out] timeActual the actual time that was achieved
    @return int indicating success (0) or failure (non-zero)*/
    int step(coreTime& timeActual) { return step(getSimulationTime() + stepTime, timeActual); }

    /**@brief step the simulation until the next event or stop point
    @return int indicating success (0) or failure (non-zero)*/
    int step() override;

    /**@brief run powerFlow in event driven mode,  evaluate the power flow at every given event or
    iteration time
    @param[in] t_end the stopping time for the simulation
    @param[in] t_step  the step size (the maximum time between powerFlow evaluation is t_step
    @return int indicating success (0) or failure (non-zero)*/
    virtual int eventDrivenPowerflow(coreTime t_end = negTime, coreTime t_step = negTime);

    /** @brief execute a specific command
    *@param[in] cmd  the command to execute
    @return the return code from the execution (typically 0 upon success)
    */
    virtual int execute(const std::string& cmd);

    /** @brief execute a specific command from a griddynAction
    *@param[in] cmd  the command to execute as an action
    @return the return code from the execution (typically 0 upon success)
    */
    virtual int execute(const gridDynAction& cmd);

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;
    virtual std::string getString(const std::string& param) const override;
    virtual void setFlag(const std::string& flag, bool val = true) override;

    /** @brief get a vector of the states
    @param[in]  sMode the solverMode to get the states for
    @return a vector containing the states
    */
    std::vector<double> getState(const solverMode& sMode = cLocalSolverMode) const;
    double getState(index_t offset) const override;

    /** @brief get a particular state
    @param[in] offset the offset of the state to grab
    @param[in] sMode the solverMode corresponding to the state to grab
    @return the value of a particular state
    */
    double getState(index_t offset, const solverMode& sMode) const;

    // saving and loading data

    // function used in initialization
    /**@brief initialize the simulation for dynamic simulation at the specified time
    @param[in] tStart the time of the initialization default to 0
    @return int indicating success (0) or failure (non-zero)*/
    int dynInitialize(coreTime tStart = negTime);  // code can detect this default param and use a
                                                   // previously specified start time
    void alert(coreObject* object, int code) override;

    /** @brief function to count the number of MPI objects required for this simulation
    @param[in] printInfo if set to true the information is printed to the console
    @return the number of MPI objects
    */
    int countMpiObjects(bool printInfo = false) const;

    /** @brief get the number of non-zeros in the most recent Jacobian calculation
    @param[in] sMode the solverMode to get the number of non-zeros for
    @return the number of non-zero elements in the Jacobian
    */
    count_t nonZeros(const solverMode& sMode) const;

    /** @brief compute the network residuals
      computes a set of function for the power system such $r(\hat{x},\hat{x'})=f(x,x)-
    f(\hat{x},\hat{x}')$ so that r approaches 0 as the $x$ == $\hat{x}
    @param[in] time  the simulation time of the evaluation
    @param[in] state  the state information to evaluation
    @param[in] dstate_dt  the time derivative of the state
    @param[out] resid the storage location for the residual function
    @param[in] sMode the solverMode to solve for
    @return integer indicating success (0) or failure (non-zero)
    */
    int residualFunction(coreTime time,
                         const double state[],
                         const double dstate_dt[],
                         double resid[],
                         const solverMode& sMode) noexcept;

    /** @brief compute the derivatives for all differential states
    @param[in] time  the simulation time of the evaluation
    @param[in] state  the state information to evaluation
    @param[out] dstate_dt  the time derivative of the state
    @param[in] sMode the solverMode to solve for
    @return integer indicating success (0) or failure (non-zero)
    */
    int derivativeFunction(coreTime time,
                           const double state[],
                           double dstate_dt[],
                           const solverMode& sMode) noexcept;

    /** @brief compute an update to all algebraic states
     compute $x=f(\hat{x})$
    @param[in] time  the simulation time of the evaluation
    @param[in] state  the state information to evaluation
    @param[out] update  the updated state information
    @param[in] sMode the solverMode to solve for
    @param[in] alpha a multiplication factor for updates that are expected to be iterative
    @return integer indicating success (0) or failure (non-zero)
    */
    int algUpdateFunction(coreTime time,
                          const double state[],
                          double update[],
                          const solverMode& sMode,
                          double alpha) noexcept;

    /** @brief compute the Jacobian of the residuals
      computes $\frac{\partial r}{\partial x}$ for all components of the residual
    @param[in] time  the simulation time of the evaluation
    @param[in] state  the state information to evaluation
    @param[in] dstate_dt  the time derivative of the state
    @param[out] md the matrixData object to store the Jacobian information into
    @param[in] cj the constant of integration for use in Jacobian elements using derivatives
    @param[in] sMode the solverMode to solve for
    @return integer indicating success (0) or failure (non-zero)
    */
    int jacobianFunction(coreTime time,
                         const double state[],
                         const double dstate_dt[],
                         matrixData<double>& md,
                         double cj,
                         const solverMode& sMode) noexcept;

    /** @brief compute any root values
      computes the roots for any root finding functions used in the system
    @param[in] time  the simulation time of the evaluation
    @param[in] state  the state information to evaluation
    @param[in] dstate_dt  the time derivative of the state
    @param[out] roots the storage location for the roots
    @param[in] sMode the solverMode to solve for
    @return integer indicating success (0) or failure (non-zero)
    */
    int rootFindingFunction(coreTime time,
                            const double state[],
                            const double dstate_dt[],
                            double roots[],
                            const solverMode& sMode) noexcept;

    /** @brief find the derivatives of the residual function with respect to the given parameters
    @param[in] time  the simulation time of the evaluation
    @param[in] indices the indices of the parameters
    @param[in] values the values for the parameters
    @param[in] state  the state information to evaluation
    @param[in] dstate_dt  the time derivative of the state
    @param[out] md the matrixData object to store the partial derivatives
    @param[in] sMode the solverMode to use for the computations
    */
    void parameterDerivatives(coreTime time,
                              parameterSet& po,
                              const index_t indices[],
                              const double values[],
                              count_t parameterCount,
                              const double state[],
                              const double dstate_dt[],
                              matrixData<double>& md,
                              const solverMode& sMode);

    /** @brief solve for the algebraic components of a system for use with the ode solvers
@param[in] time  the simulation time of the evaluation
@param[in] diffState  the current derivative information
@param[in] deriv the current derivative information
@param[in] sMode the solverMode to solve related to the differential state information
@return integer indicating success (0) or failure (non-zero)
*/
    int dynAlgebraicSolve(coreTime time,
                          const double diffState[],
                          const double deriv[],
                          const solverMode& sMode) noexcept;

    // solverMode and SolverInterface search functions

    /** @brief get the solverMode dependent on a particular index of the solver data structure
    @param[in] index the index into the SolverInterface storage array
    @return the solverMode named by the string or a blank one if none can be found
    */
    const solverMode& getSolverMode(index_t index) const;

    /** @brief get the solverMode named by a string
    @param[in] solverType the name of the SolverInterface to get the solverMode for
    @return the solverMode named by the string or a blank one if none can be found
    */
    solverMode getSolverMode(const std::string& solverType);

    /** @brief get the SolverInterface referenced by a particular index into the SolverInterface
  array
  @param[in] index the index into the SolverInterface storage array
  @return a shared pointer to a SolverInterface
  */
    std::shared_ptr<const SolverInterface> getSolverInterface(index_t index) const;

    /** @brief get the SolverInterface referenced by a particular index into the SolverInterface
    array
    @param[in] index the index into the SolverInterface storage array
    @return a shared pointer to a SolverInterface
    */
    std::shared_ptr<SolverInterface> getSolverInterface(index_t index);

    /** @brief get a shared pointer to a SolverInterface object
    @param[in] sMode the solver mode to get the residual information for
    @return a shared pointer to an SolverInterface object
    */
    std::shared_ptr<const SolverInterface> getSolverInterface(const solverMode& sMode) const;

    /** @brief get a shared pointer to a SolverInterface object
     non-const version that can create new SolverInterface objects if necessary
    @param[in] sMode the solver mode to get the residual information for
    @return a shared pointer to an SolverInterface object
    */
    std::shared_ptr<SolverInterface> getSolverInterface(const solverMode& sMode);

    /** @brief get the SolverInterface referenced by name
    @param[in] solverName string representing the SolverInterface name,  can be customized name or a
    particular type
    @return a shared pointer to a SolverInterface
    */
    std::shared_ptr<SolverInterface> getSolverInterface(const std::string& solverName);

    using gridSimulation::add;  // use the add functions from gridSimulation

    /** @brief  add a SolverInterface object to the solverDat storage array
    @param[in] nSolver the SolverInterface to add to the storage array
  */
    void add(std::shared_ptr<SolverInterface> nSolver);

    /** @brief  add an action to the queue
    @param[in] actionString a string containing the action to add
    */
    void add(const std::string& actionString);

    /** @brief  add an action to the run queue
    @param[in] newAction the action to add to the queue
    */
    void add(gridDynAction& newAction);

    /** @brief enumeration of the solution modes of operation*/
    enum class solution_modes_t {
        powerflow_mode,  //!< mode for power flow solutions
        dae_mode,  //!< mode for DAE solutions
        algebraic_mode,  //!< mode for algebraic solutions
        differential_mode,  //!< mode for differential equation solutions
    };

    /** @brief set the default solver for particular solution types to a specific solver
    @param[in] mode  the solution mode to set the solver For
    @param[in] sMode the solverMode corresponding to a particular solver
    */
    void setDefaultMode(solution_modes_t mode, const solverMode& sMode);

    /** @brief check if the simulation object has dynamic models
     basically checks if there are any differential states in the default solution mode
    */
    bool hasDynamics() const;

    /** @brief set a numeric parameter in a particular solver
     finds a solver by name then calls the solver set function
    @param[in] solverName  the name of the solver
    @param[in] field the field to set on the solver
    @param[in] val the value to the set the property to
    */
    virtual void solverSet(const std::string& solverName, const std::string& field, double val);

    /** @brief set a string parameter in a particular solver
     finds a solver by name then calls the solver set function
    @param[in] solverName  the name of the solver
    @param[in] field the field to set on the solver
    @param[in] val the value to the set the property to
    */
    virtual void
        solverSet(const std::string& solverName, const std::string& field, const std::string& val);

    /** @brief get the current solverMode from the simulation
    @param[in] sMode  input solverMode to check
    @return if sMode is valid it returns that if not it finds the current active mode and returns a
    reference to that
    */
    const solverMode& getCurrentMode(const solverMode& sMode = cEmptySolverMode) const;

    /** @brief makes sure the SolverInterface object is ready to run solutions
    @param[in] solver pointer to a solver to make ready
    */
    void getSolverReady(std::shared_ptr<SolverInterface>& solver);
    /** @brief load a stateData object with extra state information if necessary
    @param[in] sD the stateData object to load
    @param[in] sMode the solverMode of the state Data object
    */
    void fillExtraStateData(stateData& sD, const solverMode& sMode) const;
    /** @brief add an initialization function that will execute prior to the internal initialization
    in HELICS
    @param fptr a function object that returns an int.  if the value is non-zero it returns a
    failure the initialization will halt
    */
    void addInitOperation(std::function<int()> fptr);

  protected:
    /** @brief makes sure the specified mode has the correct offsets
    @param[in] sMode the solverMode of the offsets to check
    */
    void checkOffsets(const solverMode& sMode);

    /** @brief get and update a SolverInterface object
     the difference here is that the object may not exist, in which case it is created and loaded
    with recent information from the models
    @param[in] sMode the solverMode to get the data for
    @return a shared pointer to the SolverInterface object
    */
    std::shared_ptr<SolverInterface> updateSolver(const solverMode& sMode);

    /** @brief get a pointer to a solverMode based on a string
    @param[in] solverType the string representing the solverMode, this can be a particular type of
    solverMode or the name of a solver
    @return the solverMode named by the string or a blank one if none can be found
    */
    const solverMode* getSolverModePtr(const std::string& solverType) const;

    /** @brief get a pointer to a solverMode dependent on a particular index of the solver data
    structure
    @param[in] index the index into the SolverInterface storage array
    @return the solverMode named by the string or a blank one if none can be found
    */
    const solverMode* getSolverModePtr(index_t index) const;

    /** @brief reinitialize the power flow solver
    @param[in] sMode the solver Mode to reinitialize
    @param[in] change the adjustment mode
    */
    void reInitpFlow(const solverMode& sMode, change_code change = change_code::no_change);

    /** @brief perform a load balance operation on the power system
    @param[in] prevPower the previous total power output from slack bus generators
    @param[in] prevSlkGen the specifics of the power output from each slackbus
    @return true if any changes were made
    */
    virtual bool loadBalance(double prevPower, const std::vector<double>& prevSlkGen);

    /** @brief update the offsets associated with a particular mode
    @param[in] sMode the solver Mode to update the offsets for
    */
    virtual void updateOffsets(const solverMode& sMode);

    /** @brief make the simulator ready to perform a particular simulation
    @param[in] desiredState the desired state
    @param[in] sMode the solverMode to make the operations in
    @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    int makeReady(gridState_t desiredState, const solverMode& sMode);

    /** @brief set the maximum number of non-zeros in the Jacobian
    @param[in] sMode the solver mode to set the max number of non-zeros in the Jacobian
    @param[in] nonZeros the size to set
    */
    void setMaxNonZeros(const solverMode& sMode, count_t nonZeros);

    /** @brief reinitialize the dynamic simulation
    @param[in] sMode the solver mode to reinitialize
    @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    int reInitDyn(const solverMode& sMode);

    /** @brief execute a DAE simulation until the given stop time
    @param[in] tStop the stop time for the simulation
    @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    virtual int dynamicDAE(coreTime tStop);

    /** @brief execute a partitioned dynamic simulation
    @param[in] tStop the stop time for the simulation
    @param[in] tStep the step interval (defaults to the step size parameter stored in the simulation
   @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    virtual int dynamicPartitioned(coreTime tStop, coreTime tStep = negTime);

    /** @brief execute a decoupled dynamic simulation
    @param[in] tStop the stop time for the simulation
    @param[in] tStep the step interval (defaults to the step size parameter stored in the simulation
   @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    virtual int dynamicDecoupled(coreTime tStop, coreTime tStep = negTime);

    /** @brief ensure that the simulation has consistent initial conditions for starting a dynamic
    simulation
    @param[in] sMode the solver mode for which to generate the initial conditions
    @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    int generateDaeDynamicInitialConditions(const solverMode& sMode);

    /** @brief generate a convergent partitioned solution
    @param[in] sModeAlg the solver mode of the algebraic solver
    @param[in] sModeDiff  the solver mode of the differential solver
   @return FUNCTION_EXECUTION_SUCCESS(0) if successful negative number if not
    */
    int generatePartitionedDynamicInitialConditions(const solverMode& sModeAlg,
                                                    const solverMode& sModeDiff);

    /** @brief load the offset codes for the objects
    @param[in] sMode the solvermode to setup the offsets for
    @param[in] offsetOrdering the type of ordering to use
    */
    virtual void setupOffsets(const solverMode& sMode, offset_ordering offsetOrdering);

    /** @brief function to help with IDA solving steps
    @param[in] retval the return code from the solver
    @param[in] timeActual the actual time returned
    @param[in] dynData the SolverInterface currently in use
     */
    void handleEarlySolverReturn(int retval,
                                 coreTime timeActual,
                                 std::shared_ptr<SolverInterface>& dynData);

    /** @brief reset the dynamic simulation
     function checks for various conditions that cause specific things in the solver or simulation
    to be reset the nature of the reset can be driven by the reset_code given as an argument or
    internal flags from alerts or other mechanisms
    @param[in] sMode the solverMode to operate on
    @param[in] change the optional change code that the reset should function to, otherwise
    automatically detected
    @return true if the check did something, false if nothing has changed
    */
    bool dynamicCheckAndReset(const solverMode& sMode, change_code change = change_code::no_change);

    int handleStateChange(const solverMode& sMode);
    void handleRootChange(const solverMode& sMode, std::shared_ptr<SolverInterface>& dynData);

    int checkAlgebraicRoots(std::shared_ptr<SolverInterface>& dynData);
    /** @brief checks events for events needing to run and runs them then checks if a reset is
    needed if so it does so
    @param[in] cTime the time to run the events
    @param[in] sMode the solverMode to run
    @return true if the reset Function was run and did something
    */
    bool checkEventsForDynamicReset(coreTime cTime, const solverMode& sMode);

  private:
    void setupDynamicDAE();
    void setupDynamicPartitioned();

    int dynamicDAEStartupConditions(std::shared_ptr<SolverInterface>& dynData,
                                    const solverMode& sMode);
    int dynamicPartitionedStartupConditions(std::shared_ptr<SolverInterface>& dynDataDiff,
                                            std::shared_ptr<SolverInterface>& dynDataAlg,
                                            const solverMode& sModeDiff,
                                            const solverMode& sModeAlg);
    int runDynamicSolverStep(std::shared_ptr<SolverInterface>& dynData,
                             coreTime nextStop,
                             coreTime& timeActual);

    static std::atomic<gridDynSimulation*>
        s_instance;  //!< static variable to set the master simulation instance
};

}  // namespace griddyn
