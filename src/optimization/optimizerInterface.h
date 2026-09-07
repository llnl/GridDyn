/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/HelperObject.h"
#include "griddyn/griddyn-config.h"
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

enum class OptimizerPrintLevel {
    DEBUG_PRINT = 2,
    ERROR_LOG = 1,
    ERROR_TRAP = 0,
};

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

    OptimizationMode mode;
    double rtol = 1e-6;  //!< solver relative solution tolerance
    bool sparse = false;
    bool constantJacobian = false;

  protected:
    std::string lastErrorString;  //!< string containing the last error
    void* mSolverMem = nullptr;
    bool mAllocated = false;
    bool mInitialized = false;  //!< flag indicating if these vectors have been initialized
    GridDynOptimization* mGridDynOptimization = nullptr;
    count_t mVariableCount = 0;
    count_t mConstraintCount = 0;
    count_t mEvaluationCount = 0;
    count_t mObjectiveCallCount = 0;
    count_t mGradientCallCount = 0;
    count_t mConstraintCallCount = 0;
    count_t mJacobianCallCount = 0;
    count_t max_iterations = 10000;
    count_t nnz = 0;
    double solveTime = kNullVal;
    OptimizerPrintLevel printLevel = OptimizerPrintLevel::ERROR_TRAP;
    int optimizerPrintLevel = 1;
    std::bitset<32> flags;
    int lastErrorCode = 0;

  public:
    OptimizerInterface(std::string_view optName = "optim");

    OptimizerInterface(GridDynOptimization* gdo, const OptimizationMode& oMode);
    virtual ~OptimizerInterface() {}
    virtual double* val_data() { return values.data(); }
    virtual const double* val_data() const { return values.data(); }
    virtual double* constraint_data() { return constraintValues.data(); }
    virtual const double* constraint_data() const { return constraintValues.data(); }
    virtual double* lowerBoundData() { return lowerBounds.data(); }
    virtual const double* lowerBoundData() const { return lowerBounds.data(); }
    virtual double* upperBoundData() { return upperBounds.data(); }
    virtual const double* upperBoundData() const { return upperBounds.data(); }
    virtual double* constraintLowerBoundData() { return constraintLowerBounds.data(); }
    virtual const double* constraintLowerBoundData() const { return constraintLowerBounds.data(); }
    virtual double* constraintUpperBoundData() { return constraintUpperBounds.data(); }
    virtual const double* constraintUpperBoundData() const { return constraintUpperBounds.data(); }
    virtual double* gradientData() { return gradient.data(); }
    virtual const double* gradientData() const { return gradient.data(); }
    virtual double* variableTypeData() { return variableType.data(); }
    virtual const double* variableTypeData() const { return variableType.data(); }
    virtual double* toleranceData() { return tolerances.data(); }
    virtual const double* toleranceData() const { return tolerances.data(); }
    virtual double* multiplierData() { return multipliers.data(); }
    virtual const double* multiplierData() const { return multipliers.data(); }

    virtual int allocate(count_t variableCount, count_t constraintCount = 0);
    virtual void initialize(double t0);
    virtual void sparseReInit();
    virtual void dynObjectInitializeA(double /*t0*/) {}

    OptimizationData makeOptimizationData(double time);
    OptimizationData makeOptimizationData(double time, const double values[]);

    virtual int loadInitialGuess(double time);
    virtual int loadVariableBounds(double time);
    virtual int loadVariableTypes();
    virtual int loadTolerances();
    virtual int loadLinearObjective(double time, const double candidateValues[] = nullptr);
    virtual int loadQuadraticObjective(double time, const double candidateValues[] = nullptr);
    virtual int loadLinearConstraints(double time, const double candidateValues[] = nullptr);
    virtual double objectiveFunction(double time, const double candidateValues[] = nullptr);
    virtual int gradientFunction(double time, const double candidateValues[], double grad[]);
    virtual int
        constraintFunction(double time, const double candidateValues[], double constraints[]);
    virtual int constraintJacobianFunction(double time,
                                           const double candidateValues[],
                                           MatrixData<double>& matrixDataRef);
    virtual MatrixDataSparse<double>&
        constraintJacobianFunction(double time, const double candidateValues[] = nullptr);

    virtual double get(std::string_view param) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual bool getFlag(std::string_view flag) const override;
    virtual int solve(double /*tStop*/, double& /*tReturn*/) { return -101; }
    virtual void initializeJacArray(count_t size);
    virtual void setMaxNonZeros(count_t nonZeroCount);
    virtual void logSolverStats(int logLevel, bool iconly = false) const;
    virtual void logErrorWeights(int /*logLevel*/) {}

    count_t getSize() const { return mVariableCount; }
    count_t size() const { return mVariableCount; }
    count_t constraintSize() const { return mConstraintCount; }
    count_t constraintCount() const { return mConstraintCount; }
    count_t nonZeros() const { return nnz; }
    const OptimizationMode& getOptimizationMode() const { return mode; }
    double getSolverTime() const { return solveTime; }
    double getOptimizationTime() const { return solveTime; }
    void lock() { flags.set(OPT_LOCKED_FLAG); }
    void setIndex(index_t newIndex) { mode.offsetIndex = newIndex; }
    virtual void setOptimizationData(GridDynOptimization* gdo, const OptimizationMode& oMode);
    virtual int
        check_flag(void* flagvalue, std::string_view funcname, int opt, bool printError = true);
    void logMessage(int errorCode, std::string_view message);
    bool isInitialized() const { return mInitialized; }
    int getLastError() const { return lastErrorCode; }
    const std::string& getLastErrorString() const { return lastErrorString; }

  protected:
    GridOptObject* rootOptimizationObject() const;
};

class BasicOptimizer: public OptimizerInterface {
  private:
  public:
    explicit BasicOptimizer(std::string_view optName = "basic");

    BasicOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode);

    int allocate(count_t variableCount, count_t constraintCount = 0) override;
    void dynObjectInitializeA(double t0) override;
};

std::shared_ptr<OptimizerInterface> makeOptimizer(GridDynOptimization* gdo,
                                                  const OptimizationMode& oMode);

std::shared_ptr<OptimizerInterface> makeOptimizer(std::string_view type);

}  // namespace griddyn
