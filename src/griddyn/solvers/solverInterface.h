/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/helperObject.h"
#include "griddyn/gridComponentHelperClasses.h"
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace griddyn {
enum class SolverPrintLevel {
    DEBUG_PRINT = 2,
    ERROR_LOG = 1,
    ERROR_TRAP = 0,
};

class gridDynSimulation;

/** error class for throwing solver exceptions*/
class solverException: public std::exception {
  protected:
    int errorCode;  ///< the actual solver Error Code
    std::string message;

  public:
    explicit solverException(int ecode = 0):
        errorCode(ecode), message(std::format("solver Exception:error code={}", errorCode))
    {
    }
    virtual const char* what() const noexcept override { return message.c_str(); }
    /** return the full name of the object that threw the exception*/
    int code() const noexcept { return errorCode; }
};

/** error class for throwing an invalid solver operation exception from a solver
 */
class InvalidSolverOperation: public solverException {
  protected:
  public:
    explicit InvalidSolverOperation(int ecode = 0): solverException(ecode)
    {
        message = std::format("invalid solver operation:error code={}", errorCode);
    }
    virtual const char* what() const noexcept override { return message.c_str(); }
};

// solver return codes from the solve and initIC functions
#define SOLVER_ROOT_FOUND 2
#define SOLVER_INVALID_STATE_ERROR (-36)
#define SOLVER_INITIAL_SETUP_ERROR (-38)
#define SOLVER_CONVERGENCE_ERROR (-12)

enum solver_flags : int {
    dense_flag = 0,  //!< if the solver should use a dense or sparse version
    constantJacobian_flag = 1,  //!< if the solver should just keep a constant Jacobian
    useMask_flag = 2,  //!< if the solver should use a mask to filter out specific states
    parallel_flag = 3,  //!< if the solver should use a parallel version
    locked_flag = 4,  //!< if the solverMode is locked from further updates
    use_omp_flag = 5,  //!< flag indicating whether to use omp data constructs
    allocated_flag = 6,  //!< if the solver has been allocated
    initialized_flag = 7,  //!< flag indicating if these vectors have been initialized
    fileCapture_flag = 8,
    directLogging_flag =
        9,  //!< flag telling the SolverInterface to capture a log directly from the solver
    use_newton_flag = 11,
    use_bdf_flag = 12,
    block_mode_only = 13,  //!< flag indicating that the solver only supports block mode
    extra_solver_flag1 = 16,
    extra_solver_flag2 = 17,
    extra_solver_flag3 = 18,
    extra_solver_flag4 = 19,
    extra_solver_flag5 = 20,
    extra_solver_flag6 = 21,
    extra_solver_flag7 = 22,
    extra_solver_flag8 = 23,
    extra_solver_flag9 = 24,
    extra_solver_flag10 = 25,
    extra_solver_flag11 = 26,
    extra_solver_flag12 = 27,
    print_residuals = 28,
};
/** @brief class defining the data related to a specific solver
 the SolverInterface class is the base class for solvers for the GridDyn power systems program
a particular SolverInterface class will contain the interface and calls necessary to implement a
particular solver methodology
*/
class SolverInterface: public HelperObject {
  public:
    /** @brief enumeration of solver call modes*/
    enum class StepMode {
        NORMAL,  //!< normal operation
        SINGLE_STEP,  //!< single step operation
        BLOCK,  //!< the solver runs in a block mode all at once
    };
    /** @brief enumeration of initiaL condition call modes*/
    enum class ic_modes {
        fixed_masked_and_deriv,  //!< fixed_algebraic and differential state derivatives
        fixed_diff,  //!< differential states are fixed
    };
    /** @brief enumeration of initiaL condition call modes*/
    enum class SparseReinitMode {
        REFACTOR,  //!< refactor the sparse matrix
        RESIZE  //!< destroy and completely reinit the sparse calculations
    };

    std::vector<int> rootsfound;  //!< mask vector for which roots were found
  protected:
    std::string lastErrorString;  //!< string containing the last error

    // solver outputs

    std::vector<index_t> maskElements;  //!< vector of constant states in any problem
    std::string solverLogFile;  //!< file name and location of log file reference
    SolverPrintLevel printLevel = SolverPrintLevel::ERROR_TRAP;  //!< print_level for solver
    int solverPrintLevel = 1;  //!< print level for internal solver logging
    count_t rootCount = 0;  //!< the number of root finding functions
    count_t solverCallCount = 0;  //!< the number of times the solver has been called
    count_t jacCallCount = 0;  //!< the number of times the Jacobian function has been called
    count_t funcCallCount = 0;  //!< the number of times the function evaluation has been called
    count_t rootCallCount = 0;
    count_t max_iterations = 10000;  //!< the maximum number of iterations in the solver loop
    solverMode mode;  //!< to the solverMode
    double tolerance = 1e-8;  //!< the default solver tolerance
    coreTime solveTime = negTime;  //!< storage for the time the solver is called
    std::string jacFile;  //!< the file to write the Jacobian to
    std::string stateFile;  //!< the file to write the state and residual to
    gridDynSimulation* m_gds = nullptr;  //!< pointer the gridDynSimulation object used
    count_t svsize = 0;  //!< the state size
    count_t nnz = 0;  //!< the actual number of non-zeros in a Jacobian
    std::bitset<32> flags;  //!< flags for the solver
    int lastErrorCode = 0;  //!< the last error Code
  public:
    /** @brief default constructor
     * @param[in] objName  the name of the solver
     */
    explicit SolverInterface(const std::string& objName = "");

    /** @brief alternate constructor
    @param[in] gds  gridDynSimulation to link with
    @param[in] sMode the solverMode associated with the solver
    */
    SolverInterface(gridDynSimulation* gds, const solverMode& sMode);

    /** @brief make a copy of the solver interface
    @param[in] fullCopy set to true to initialize and copy over all data to the new object
    @return a unique ptr to the clones SolverInterface
    */
    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const;

    /** @brief make a copy of the solver interface
    @param[in] si a ptr to an existing interface that data should be copied to
    @param[in] fullCopy set to true to initialize and copy over all data to the new object
    */
    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const;
    /** @brief get a pointer to the state data
    @return a pointer to a double array with the state data
    */
    virtual double* state_data() noexcept;

    /** @brief get a pointer to the state time derivative information
    @return a pointer to a double array with the state time derivative information
    */
    virtual double* deriv_data() noexcept;

    /** @brief get a pointer to the type data
    @return a pointer to a double array containing the type data
    */
    virtual double* type_data() noexcept;

    /** @brief get a pointer to the const state data
    @return a pointer to a const double array with the state data
    */
    virtual const double* state_data() const noexcept;

    /** @brief get a pointer to the const state time derivative information
    @return a pointer to a const double array with the state time derivative information
    */
    virtual const double* deriv_data() const noexcept;

    /** @brief get a pointer to the const type data
    @return a pointer to a const double array containing the type data
    */
    virtual const double* type_data() const noexcept;

    /** @brief allocate the memory for the solver
    @param[in] size  the size of the state vector
    @param[in] numRoots  the number of root functions in the solution
    */
    virtual void allocate(count_t size, count_t numRoots = 0);

    /** @brief initialize the solver to time t0
    @param[in] t0  the time for the initialization
    */
    virtual void initialize(coreTime t0);

    /** @brief reinitialize the sparse components
    @param[in] mode the reinitialization mode
    */
    virtual void sparseReInit(SparseReinitMode mode);

    /** @brief load the constraints*/
    virtual void setConstraints();

    /** @brief perform an initial condition calculation
    @param[in] t0  the time for the initialization
    @param[in]  tstep0  the size of the first desired step
    @param[in] mode  the step mode
    @param[in] constraints  flag indicating that constraints should be used
    @return the function success status  FUNCTION_EXECUTION_SUCCESS on success
    */
    virtual int calcIC(coreTime t0, coreTime tstep0, ic_modes mode, bool constraints);
    /** @brief get the current solution
     usually called after a call to CalcIC to get the calculated conditions
    */
    virtual void getCurrentData();
    /** @brief get the locations of any found roots
     */
    virtual void getRoots();
    /** @brief update the number of roots to find
     */
    virtual void setRootFinding(index_t numRoots);

    /** @brief get a parameter from the solver
  @param[in] param  a string with the desired name of the parameter or result
  @return the value of the requested parameter
  */
    virtual double get(std::string_view param) const override;
    /** @brief set a string parameter in the solver
    @param[in] param  a string with the desired name of the parameter
    @param[in] val the value of the property to set
    */
    virtual void set(std::string_view param, std::string_view val) override;

    /** @brief set a numerical parameter on a solver
  @param[in] param  a string with the desired name of the parameter
  @param[in] val the value of the property to set
  */
    virtual void set(std::string_view param, double val) override;

    /** @brief set a flag parameter on a solver
    @param[in] flag  a string with the name of the flag to set
    @param[in] val the value of the property to set
    */
    virtual void setFlag(std::string_view flag, bool val = true) override;
    /** @brief get a flag parameter from a solver
    @param[in] flag  a string with the name of the flag to set
    */
    virtual bool getFlag(std::string_view flag) const override;
    /** get the last time the solver was called*/
    coreTime getSolverTime() const { return solveTime; }
    /** @brief perform the solver calculations
  @param[in] tStop  the requested return time   not that useful for algebraic solvers
  @param[out]  tReturn  the actual return time
  @param[in] stepMode  the step mode
  @return the function success status  FUNCTION_EXECUTION_SUCCESS on success
  */
    virtual int solve(coreTime tStop, coreTime& tReturn, StepMode stepMode = StepMode::NORMAL);
    /** @brief resize the storage array for the Jacobian
    @param[in] nonZeroCount  the number of elements to potentially store
    */
    virtual void setMaxNonZeros(count_t nonZeroCount);
    /** @brief check if the SolverInterface has been initialized
    @return true if initialized false if not
    */
    bool isInitialized() const { return flags[initialized_flag]; }

    /** @brief helper function to log specific solver stats
    @param[in] logLevel  the level of logging to display
    @param[in] iconly  flag indicating that the logging should be for the initial condition
    calculation only
    */
    virtual void logSolverStats(print_level logLevel, bool iconly = false) const;
    /** @brief helper function to log error weight information
    @param[in] logLevel  the level of logging to display
    */
    virtual void logErrorWeights(print_level logLevel) const;

    /** @brief get the state size
    @return the state size
    */
    count_t size() const { return svsize; }

    /** @brief get the actual number of non-zeros in the Jacobian
    @return the state size
    */
    count_t nonZeros() const { return nnz; }

    const solverMode& getSolverMode() const { return mode; }

    void lock() { flags.set(locked_flag); }

    void setIndex(index_t newIndex) { mode.offsetIndex = newIndex; }
    /** @brief print out all the state values
    @param[in] getNames use the actual state names vs some coding
    */
    void printStates(bool getNames = false);
    /** @brief input the simulation data to attach to
    @param[in] gds the gridDynSimulationObject to attach to
    @param[in] sMode the solverMode associated with the solver
    */
    virtual void setSimulationData(gridDynSimulation* gds, const solverMode& sMode);
    /** @brief input the simulation data to attach to
    @param[in] gds the gridDynSimulationObject to attach to
    */
    virtual void setSimulationData(gridDynSimulation* gds);

    /** @brief input the solverMode associated with the solver
    @param[in] sMode the solverMode to attach to
    */
    virtual void setSimulationData(const solverMode& sMode);

    void setApproximation(std::string_view approx);
    /** @brief load up masks to the states
      masks isolate specific values and don't let the solver alter them  for newton based solvers
    this implies overriding specific information in the Jacobian calculations and the residual
    calculations
    @param[in] msk  the indices of the state elements to fix
    */
    void setMaskElements(std::vector<index_t> msk);

    /** @brief add an index to the mask
      masks isolate specific values and don't let the solver alter them  for newton based solvers
    this implies overriding specific information in the Jacobian calculations and the residual
    calculations
    @param[in] newMaskElement the index of the values to mask
    */
    void addMaskElement(index_t newMaskElement);

    /** @brief add several new elements to a mask
      masks isolate specific values and don't let the solver alter them  for newton based solvers
    this implies overriding specific information in the Jacobian calculations and the residual
    calculations
    @param[in] newMsk  a vector of indices to add to an existing mask
    */
    void addMaskElements(const std::vector<index_t>& newMsk);

    void logMessage(int errorCode, std::string_view message);

    int getLastError() const { return lastErrorCode; }
    const std::string& getLastErrorString() const { return lastErrorString; }

  protected:
    /** @brief check the output of actual solver calls for proper results
    @param[in] flagvalue a return code <0 usually indicates an error
    @param[in] funcname  the name of the function that we are checking
    @param[in] opt  0 for allocation 1 for other functions
    @param[in] printError  boolean flag indicating whether to print a message on error or not
    */
    virtual void check_flag(void* flagvalue,
                            std::string_view funcname,
                            int opt,
                            bool printError = true) const;
};

/** @brief make a solver from a particular mode
@param[in] gds  the gridDynSimulation to link to
@param[in] sMode the solverMode to construct the SolverInterface from
@return a unique_ptr to a SolverInterface object
*/
std::unique_ptr<SolverInterface> makeSolver(gridDynSimulation* gds, const solverMode& sMode);
/** @brief make a solver from a string
@param[in] type the type of SolverInterface to create
@return a unique_ptr to a SolverInterface object
*/
std::unique_ptr<SolverInterface> makeSolver(std::string_view type, const std::string& name = "");

}  // namespace griddyn

