/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/HelperObject.h"
#include "griddyn/griddyn-config.h"
#include "nativeDenseSolver.h"
#include "optHelperClasses.h"
#include "utilities/MatrixDataSparse.hpp"
#include "utilities/vectData.hpp"
#include <bitset>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
class GridDynOptimization;
class GridOptObject;

/** GridDyn log severity used for optimizer messages. */
enum class OptimizerPrintLevel {
    DEBUG_PRINT = 2,
    ERROR_LOG = 1,
    ERROR_TRAP = 0,
};

/** Flags selecting optimizer storage, lifecycle, and logging behavior. */
enum OptimizerFlags : int {
    OPT_DENSE_FLAG = 0,  //!< if the optimizer should use dense storage/linear algebra
    OPT_CONSTANT_JACOBIAN_FLAG = 1,  //!< if the constraint Jacobian can be reused
    OPT_PARALLEL_FLAG = 3,  //!< if the optimizer should use a parallel implementation
    OPT_LOCKED_FLAG = 4,  //!< if the OptimizationMode is locked from further updates
    OPT_ALLOCATED_FLAG = 6,  //!< if optimizer storage has been allocated
    OPT_INITIALIZED_FLAG = 7,  //!< if optimizer storage has been initialized
    OPT_DIRECT_LOGGING_FLAG = 9,  //!< if solver-native logging should be captured directly
    OPT_MIXED_INTEGER_FLAG = 10,  //!< if integer variables must be respected
    OPT_RELAXED_INTEGER_FLAG = 11,  //!< if integer variables may be relaxed to continuous values
    OPT_PRINT_RESIDUALS_FLAG = 28,  //!< debug print of constraint residuals
};

/** Common storage and callback interface for GridDyn optimization backends.
 *
 * Distributed GridOptObject instances contribute objective terms, bounds,
 * constraints, derivatives, and write-back operations. This class owns the
 * decision and constraint arrays passed to those callbacks. A solve backend
 * may stage a candidate in these arrays; writeBack commits an accepted result
 * to the physical GridDyn model.
 */
class OptimizerInterface: public HelperObject {
  public:
    vectData<double> linearObjective;  //!< sparse linear objective coefficients
    vectData<double> quadraticObjective;  //!< sparse diagonal quadratic objective coefficients
    MatrixDataSparse<double> linearConstraints;  //!< sparse explicitly linear constraints
    MatrixDataSparse<double> constraintJacobian;  //!< sparse constraint Jacobian entries

    // Optimizer-owned problem arrays.  OptimizationData points into these
    // arrays during callbacks, mirroring SolverInterface ownership plus
    // StateData views on the simulation side.
    std::vector<double> values;  //!< decision-variable vector
    std::vector<double> lowerBounds;  //!< decision-variable lower bounds
    std::vector<double> upperBounds;  //!< decision-variable upper bounds
    std::vector<double> constraintValues;  //!< evaluated constraint residuals/functions
    std::vector<double> constraintLowerBounds;  //!< constraint lower bounds
    std::vector<double> constraintUpperBounds;  //!< constraint upper bounds
    std::vector<double> gradient;  //!< objective gradient
    std::vector<double> variableType;  //!< variable type markers, 0 continuous, 1 integer, 2 binary
    std::vector<double> tolerances;  //!< variable scaling/tolerance hints
    std::vector<double> multipliers;  //!< optional constraint/objective multipliers
    std::vector<double> scratch1;  //!< callback scratch storage
    std::vector<double> scratch2;  //!< callback scratch storage

    OptimizationMode mode;  //!< optimization mode shared with the distributed model tree
    double rtol = 1e-6;  //!< solver relative solution tolerance
    bool sparse = false;  //!< whether the backend uses sparse matrix storage
    bool constantJacobian = false;  //!< whether a constant constraint Jacobian may be reused

  protected:
    std::string lastErrorString;  //!< string containing the last error
    void* mSolverMem = nullptr;  //!< backend-specific solver state
    bool mAllocated = false;  //!< whether problem arrays have been allocated
    bool mInitialized = false;  //!< flag indicating if these vectors have been initialized
    GridDynOptimization* mGridDynOptimization = nullptr;  //!< optimization model bound to this backend
    count_t mVariableCount = 0;  //!< number of decision variables
    count_t mConstraintCount = 0;  //!< number of constraint rows
    count_t mEvaluationCount = 0;  //!< total callback evaluation count
    count_t mObjectiveCallCount = 0;  //!< objective callback count
    count_t mGradientCallCount = 0;  //!< objective-gradient callback count
    count_t mConstraintCallCount = 0;  //!< constraint callback count
    count_t mJacobianCallCount = 0;  //!< constraint-Jacobian callback count
    count_t max_iterations = 10000;  //!< maximum backend iteration count
    count_t nnz = 0;  //!< nonzero count used to size sparse storage
    double solveTime = kNullVal;  //!< time of the latest solve or evaluation
    OptimizerPrintLevel printLevel = OptimizerPrintLevel::ERROR_TRAP;  //!< GridDyn log severity
    int optimizerPrintLevel = 1;  //!< backend-specific print-level value
    std::bitset<32> flags;  //!< optimizer options indexed by OptimizerFlags
    int lastErrorCode = 0;  //!< status from the most recent failed operation

  public:
    /** Creates an unbound optimizer with a registry name.
     * @param[in] optName name used to register or identify the optimizer
     */
    OptimizerInterface(std::string_view optName = "optim");

    /** Creates an optimizer bound to a GridDyn optimization model.
     * @param[in] gdo optimization model that supplies distributed callbacks
     * @param[in] oMode mode describing the problem being assembled
     */
    OptimizerInterface(GridDynOptimization* gdo, const OptimizationMode& oMode);

    /** Destroys the optimizer interface. */
    virtual ~OptimizerInterface() {}

    /** Gets mutable access to the decision-variable array. */
    virtual double* val_data() { return values.data(); }
    /** Gets read-only access to the decision-variable array. */
    virtual const double* val_data() const { return values.data(); }
    /** Gets mutable access to evaluated constraint values. */
    virtual double* constraint_data() { return constraintValues.data(); }
    /** Gets read-only access to evaluated constraint values. */
    virtual const double* constraint_data() const { return constraintValues.data(); }
    /** Gets mutable access to decision-variable lower bounds. */
    virtual double* lowerBoundData() { return lowerBounds.data(); }
    /** Gets read-only access to decision-variable lower bounds. */
    virtual const double* lowerBoundData() const { return lowerBounds.data(); }
    /** Gets mutable access to decision-variable upper bounds. */
    virtual double* upperBoundData() { return upperBounds.data(); }
    /** Gets read-only access to decision-variable upper bounds. */
    virtual const double* upperBoundData() const { return upperBounds.data(); }
    /** Gets mutable access to constraint lower bounds. */
    virtual double* constraintLowerBoundData() { return constraintLowerBounds.data(); }
    /** Gets read-only access to constraint lower bounds. */
    virtual const double* constraintLowerBoundData() const { return constraintLowerBounds.data(); }
    /** Gets mutable access to constraint upper bounds. */
    virtual double* constraintUpperBoundData() { return constraintUpperBounds.data(); }
    /** Gets read-only access to constraint upper bounds. */
    virtual const double* constraintUpperBoundData() const { return constraintUpperBounds.data(); }
    /** Gets mutable access to the objective gradient array. */
    virtual double* gradientData() { return gradient.data(); }
    /** Gets read-only access to the objective gradient array. */
    virtual const double* gradientData() const { return gradient.data(); }
    /** Gets mutable access to variable-type markers. */
    virtual double* variableTypeData() { return variableType.data(); }
    /** Gets read-only access to variable-type markers. */
    virtual const double* variableTypeData() const { return variableType.data(); }
    /** Gets mutable access to variable scaling and tolerance hints. */
    virtual double* toleranceData() { return tolerances.data(); }
    /** Gets read-only access to variable scaling and tolerance hints. */
    virtual const double* toleranceData() const { return tolerances.data(); }
    /** Gets mutable access to optional constraint/objective multipliers. */
    virtual double* multiplierData() { return multipliers.data(); }
    /** Gets read-only access to optional constraint/objective multipliers. */
    virtual const double* multiplierData() const { return multipliers.data(); }

    /** Allocates decision, constraint, derivative, and sparse callback storage.
     * @param[in] variableCount number of decision variables
     * @param[in] constraintCount number of constraint rows
     * @return backend status code
     */
    virtual int allocate(count_t variableCount, count_t constraintCount = 0);

    /** Initializes backend storage and evaluates mode-specific setup.
     * @param[in] initTime time passed to distributed model callbacks
     */
    virtual void initialize(double initTime);

    /** Rebuilds sparse Jacobian storage after the nonzero count changes. */
    virtual void sparseReInit();

    /** Runs the optimization object's first initialization stage. */
    virtual void dynObjectInitializeA(double /*t0*/) {}

    /** Creates a callback view over the optimizer-owned arrays.
     * @param[in] time callback evaluation time
     */
    OptimizationData makeOptimizationData(double time);

    /** Creates a callback view using a caller-supplied decision vector.
     * @param[in] time callback evaluation time
     * @param[in] values candidate decision-variable values
     */
    OptimizationData makeOptimizationData(double time, const double values[]);

    /** Loads the distributed model's initial decision vector. */
    virtual int loadInitialGuess(double time);

    /** Loads lower and upper bounds for decision variables. */
    virtual int loadVariableBounds(double time);

    /** Loads continuous, integer, or binary variable markers. */
    virtual int loadVariableTypes();

    /** Loads variable scaling and tolerance hints. */
    virtual int loadTolerances();

    /** Loads linear objective coefficients, optionally at candidate values. */
    virtual int loadLinearObjective(double time, const double candidateValues[] = nullptr);

    /** Loads diagonal quadratic objective coefficients. */
    virtual int loadQuadraticObjective(double time, const double candidateValues[] = nullptr);

    /** Loads explicitly linear constraint coefficients and bounds. */
    virtual int loadLinearConstraints(double time, const double candidateValues[] = nullptr);
    /** @brief write the current optimizer solution vector back into the source power-system model
     *
     * This is an explicit commit stage rather than a side effect of solve().
     * The separation lets callers inspect candidate optimizer results before
     * changing the physical GridDyn model, and provides a single hook for later
     * scheduler/time-marching dispatch integration.
     *
     * @param time the time associated with the solution commit; if omitted the
     * most recent optimizer solve/initialization time is used.
     * @return FUNCTION_EXECUTION_SUCCESS on success.
     */
    virtual int writeBack(double time = kNullVal);

    /** Commits the current solution using the writeBack implementation. */
    virtual int applySolution(double time = kNullVal) { return writeBack(time); }

    /** Evaluates the objective for a candidate decision vector. */
    virtual double objectiveFunction(double time, const double candidateValues[] = nullptr);

    /** Evaluates the objective gradient for a candidate decision vector. */
    virtual int gradientFunction(double time, const double candidateValues[], double grad[]);

    /** Evaluates nonlinear and linear constraint residuals. */
    virtual int
        constraintFunction(double time, const double candidateValues[], double constraints[]);

    /** Evaluates constraint-Jacobian entries into caller-provided storage. */
    virtual int constraintJacobianFunction(double time,
                                           const double candidateValues[],
                                           MatrixData<double>& matrixDataRef);

    /** Gets the optimizer-owned sparse constraint Jacobian. */
    virtual MatrixDataSparse<double>&
        constraintJacobianFunction(double time, const double candidateValues[] = nullptr);

    virtual double get(std::string_view param) const override;
    /** Sets a text-valued optimizer option.
     * @param[in] param option name
     * @param[in] val option value
     */
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual bool getFlag(std::string_view flag) const override;
    /** Runs the backend solve.
     * @return backend status; the base implementation returns -101 to indicate
     * that no solve backend is provided
     */
    virtual int solve(double /*tStop*/, double& /*tReturn*/) { return -101; }

    /** Allocates or resizes the sparse Jacobian entry array. */
    virtual void initializeJacArray(count_t size);

    /** Sets the expected number of nonzero Jacobian entries. */
    virtual void setMaxNonZeros(count_t nonZeroCount);

    /** Writes optimizer statistics to the GridDyn logger. */
    virtual void logSolverStats(int logLevel, bool iconly = false) const;

    /** Writes backend-specific error weights when supported. */
    virtual void logErrorWeights(int /*logLevel*/) {}

    /** Gets the number of decision variables. */
    count_t getSize() const { return mVariableCount; }

    /** Alias for getSize(). */
    count_t size() const { return mVariableCount; }

    /** Gets the number of constraint rows. */
    count_t constraintSize() const { return mConstraintCount; }

    /** Alias for constraintSize(). */
    count_t constraintCount() const { return mConstraintCount; }

    /** Gets the number of stored nonzero entries. */
    count_t nonZeros() const { return nnz; }

    /** Gets the optimization mode bound to this optimizer. */
    const OptimizationMode& getOptimizationMode() const { return mode; }

    /** Gets the time recorded for the latest optimizer operation. */
    double getSolverTime() const { return solveTime; }

    /** Alias for getSolverTime(). */
    double getOptimizationTime() const { return solveTime; }

    /** Prevents further changes to the optimization mode. */
    void lock() { flags.set(OPT_LOCKED_FLAG); }

    /** Sets the base index used by this object's optimization mode. */
    void setIndex(index_t newIndex) { mode.offsetIndex = newIndex; }

    /** Binds this optimizer to a model and mode.
     * @param[in] gdo optimization model that supplies distributed callbacks
     * @param[in] oMode mode describing the problem being assembled
     */
    virtual void setOptimizationData(GridDynOptimization* gdo, const OptimizationMode& oMode);

    /** Handles a backend flag callback and optionally logs an unsupported flag. */
    virtual int
        check_flag(void* flagvalue, std::string_view funcname, int opt, bool printError = true);

    /** Stores and logs an optimizer diagnostic.
     * @param[in] errorCode backend or callback status
     * @param[in] message diagnostic text
     */
    void logMessage(int errorCode, std::string_view message);

    /** Checks whether optimizer storage has been initialized. */
    bool isInitialized() const { return mInitialized; }

    /** Gets the status code from the most recent failed operation. */
    int getLastError() const { return lastErrorCode; }

    /** Gets the diagnostic text from the most recent failed operation. */
    const std::string& getLastErrorString() const { return lastErrorString; }

  protected:
    /** Gets the root distributed optimization object, if one is attached. */
    GridOptObject* rootOptimizationObject() const;
};

/** Dry-run optimizer used to exercise allocation and initialization paths. */
class BasicOptimizer: public OptimizerInterface {
  private:
  public:
    /** Creates an unbound basic optimizer.
     * @param[in] optName name used to register or identify the optimizer
     */
    explicit BasicOptimizer(std::string_view optName = "basic");

    /** Creates a basic optimizer bound to a model and mode. */
    BasicOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode);

    /** Allocates the base arrays and initializes dry-run storage. */
    int allocate(count_t variableCount, count_t constraintCount = 0) override;

    /** Initializes the optimization object during the first model stage. */
    void dynObjectInitializeA(double initTime) override;
};

/** Merit-order economic-dispatch optimizer for simple dispatch studies. */
class EconomicDispatchOptimizer: public OptimizerInterface {
  private:
    double dispatchImbalance = 0.0;

  public:
    /** Creates an unbound economic-dispatch optimizer. */
    explicit EconomicDispatchOptimizer(std::string_view optName = "dispatch");

    /** Creates an economic-dispatch optimizer bound to a model and mode. */
    EconomicDispatchOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode);

    /** Computes and stages a merit-order dispatch for the current problem. */
    int solve(double tStop, double& tReturn) override;

    /** Gets an economic-dispatch property by name. */
    double get(std::string_view param) const override;
};

/** Built-in optimizer that solves a materialized native DC problem. */
class NativeOptimizer: public OptimizerInterface {
  private:
    bool mProblemDataLoaded = false;
    bool mSolutionValid = false;
    std::uint64_t mProblemVersion = 0;
    NativeQpProblem mProblem;
    NativeSolveResult mLastSolveResult;

  protected:
    /**
     * Validate a backend candidate against the GridDyn callbacks and retain it
     * as the pending optimizer solution.
     *
     * Backends operate only on the immutable `NativeQpProblem` snapshot. This
     * common acceptance step verifies the returned objective, gradient,
     * affine constraints, Jacobian, and bounds against both the snapshot and
     * the live callbacks before exposing the candidate through `values`.
     *
     * @param time callback/solution time.
     * @param result backend result and candidate values.
     * @return success only when the candidate is suitable for `writeBack()`.
     */
    int acceptSolution(double time, NativeSolveResult result);

    /** Record a failed solve attempt when problem preparation did not finish. */
    void recordSolveFailure(std::string message);

    /** Name used in backend diagnostics and write-back errors. */
    virtual std::string_view backendName() const { return "native"; }

    /** Whether this backend accepts integer and binary variables in a linear model. */
    virtual bool supportsIntegerVariables() const { return false; }

  public:
    /** Construct a native optimizer with the named optimizer registration. */
    explicit NativeOptimizer(std::string_view optName = "native");

    /** Construct a native optimizer bound to a GridDyn optimization model. */
    NativeOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode);

    /** @brief assemble the current optimization problem into optimizer-owned storage
     *
     * This is the setup stage for the compact built-in solver. It invokes the
     * distributed objective, bound, constraint, residual, gradient, and
     * Jacobian callbacks, then copies their results into an owned,
     * solver-neutral snapshot. It deliberately stops before numerical solve
     * math so the same snapshot can later be passed to a different backend.
     *
     * @param time time at which GridDyn callbacks are evaluated.
     * @return FUNCTION_EXECUTION_SUCCESS when the complete snapshot is valid.
     */
    virtual int prepareProblemData(double time);

    /**
     * Return the last successfully materialized solver-neutral problem.
     * @return an immutable snapshot owned by this optimizer; it is unchanged
     *   until the next successful `prepareProblemData()` call.
     */
    const NativeQpProblem& problem() const { return mProblem; }
    /**
     * Return diagnostics for the most recent native solve attempt.
     * @return the native solver result, including status, candidate values, and
     *   numerical diagnostics.
     */
    const NativeSolveResult& lastSolveResult() const { return mLastSolveResult; }

    /**
     * Materialize, solve, and callback-validate the current DC problem.
     *
     * The candidate remains in optimizer-owned storage and is not committed to
     * physical GridDyn objects until `writeBack()` is called.
     *
     * @param tStop time at which callbacks are evaluated.
     * @param tReturn receives the time associated with the accepted result.
     * @return FUNCTION_EXECUTION_SUCCESS only for a validated optimal result.
     */
    int solve(double tStop, double& tReturn) override;

    /**
     * Commit the last validated native solution to the physical model.
     * @param time solution time; `kNullVal` uses the most recent solve time.
     * @return FUNCTION_EXECUTION_SUCCESS when the commit succeeds.
     */
    int writeBack(double time = kNullVal) override;

    /**
     * Return base optimizer properties plus native solver diagnostics.
     * @param param property name, such as `problem_loaded`, `solution_valid`,
     *   or `objective_value`.
     * @return requested value, or the base-class null value for unknown names.
     */
    double get(std::string_view param) const override;
};

std::shared_ptr<OptimizerInterface> makeOptimizer(GridDynOptimization* gdo,
                                                  const OptimizationMode& oMode);

std::shared_ptr<OptimizerInterface>
    makeOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode, std::string_view type);

std::shared_ptr<OptimizerInterface> makeOptimizer(std::string_view type);

}  // namespace griddyn
