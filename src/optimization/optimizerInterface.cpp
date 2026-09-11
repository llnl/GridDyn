/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "optimizerInterface.h"

#ifdef GRIDDYN_ENABLE_HIGHS
#include "highsOptimizer.h"
#endif

#include "core/CoreExceptions.h"
#include "core/FactoryTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gridDynOpt.h"
#include "gridOptObjects.h"
#include "models/gridAreaOpt.h"
#include "models/gridBusOpt.h"
#include "models/gridGenOpt.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
static ChildClassFactory<BasicOptimizer, OptimizerInterface> gBasicFac(stringVec{"basic"});
static ChildClassFactory<EconomicDispatchOptimizer, OptimizerInterface>
    gDispatchFac(stringVec{"dispatch", "stack", "pricestack", "economic"});
static ChildClassFactory<NativeOptimizer, OptimizerInterface>
    gNativeFac(stringVec{"native", "compact", "dense", "nativeqp", "qp"});
#ifdef GRIDDYN_ENABLE_HIGHS
static ChildClassFactory<HighsOptimizer, OptimizerInterface> gHighsFac(
    stringVec{"highs", "highsqp", "highs_optimizer"});
#endif

namespace {
    constexpr double kFiniteBoundLimit = kBigNum * 0.5;

    struct DispatchVariable {
        index_t mIndex = kNullLocation;
        double mLower = 0.0;
        double mUpper = 0.0;
        double mLinearCost = 0.0;
        double mQuadraticCost = 0.0;
    };

    double finiteLower(double value)
    {
        return (std::isfinite(value) && (value > -kFiniteBoundLimit)) ? value : 0.0;
    }

    double finiteUpper(double value)
    {
        return (std::isfinite(value) && (value < kFiniteBoundLimit)) ? value : kBigNum;
    }

    double marginalCostAtLower(const DispatchVariable& dispatchVariable)
    {
        return dispatchVariable.mLinearCost +
            (2.0 * dispatchVariable.mQuadraticCost * dispatchVariable.mLower);
    }

    double nativeLowerBound(double value)
    {
        if (std::isinf(value)) {
            return (value < 0.0) ? -std::numeric_limits<double>::infinity() : value;
        }
        return (value <= -kFiniteBoundLimit) ? -std::numeric_limits<double>::infinity() : value;
    }

    double nativeUpperBound(double value)
    {
        if (std::isinf(value)) {
            return (value > 0.0) ? std::numeric_limits<double>::infinity() : value;
        }
        return (value >= kFiniteBoundLimit) ? std::numeric_limits<double>::infinity() : value;
    }

    bool nativeNearlyEqual(double lhs, double rhs)
    {
        constexpr double comparisonTolerance = 1e-8;
        if (lhs == rhs) {
            return true;
        }
        if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
            return false;
        }
        return std::abs(lhs - rhs) <=
            comparisonTolerance * (1.0 + (std::max)(std::abs(lhs), std::abs(rhs)));
    }

    std::vector<double> nativeValidationPoint(const NativeQpProblem& problem)
    {
        auto point = problem.initialValues;
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            const double lower = problem.variableLowerBounds[column];
            const double upper = problem.variableUpperBounds[column];
            if (nativeNearlyEqual(lower, upper)) {
                continue;
            }
            const double step = 0.03125 * (1.0 + std::abs(point[column]));
            if (!std::isfinite(upper) || (point[column] + step <= upper)) {
                point[column] += step;
            } else if (!std::isfinite(lower) || (point[column] - step >= lower)) {
                point[column] -= step;
            }
        }
        return point;
    }

    bool loadSparseMatrix(const MatrixDataSparse<double>& source,
                          std::size_t rowCount,
                          std::size_t columnCount,
                          NativeSparseMatrix& destination,
                          std::string& error)
    {
        struct SparseEntry {
            std::size_t row = 0;
            std::size_t column = 0;
            double value = 0.0;
        };

        std::vector<SparseEntry> entries;
        entries.reserve(source.size());
        for (const auto& element : source) {
            if ((element.row < 0) || (element.col < 0) ||
                std::cmp_greater_equal(element.row, rowCount) ||
                std::cmp_greater_equal(element.col, columnCount)) {
                error = "sparse matrix entry is outside the problem dimensions";
                return false;
            }
            if (!std::isfinite(element.data)) {
                error = "sparse matrix contains a non-finite coefficient";
                return false;
            }
            entries.push_back({static_cast<std::size_t>(element.row),
                              static_cast<std::size_t>(element.col),
                              element.data});
        }

        std::sort(entries.begin(), entries.end(), [](const SparseEntry& lhs, const SparseEntry& rhs) {
            return (lhs.row < rhs.row) ||
                ((lhs.row == rhs.row) && (lhs.column < rhs.column));
        });

        std::size_t canonicalEntryCount = 0;
        for (const auto& entry : entries) {
            if ((canonicalEntryCount > 0) &&
                (entries[canonicalEntryCount - 1].row == entry.row) &&
                (entries[canonicalEntryCount - 1].column == entry.column)) {
                entries[canonicalEntryCount - 1].value += entry.value;
                if (!std::isfinite(entries[canonicalEntryCount - 1].value)) {
                    error = "sparse matrix duplicate coefficients overflowed";
                    return false;
                }
            } else {
                entries[canonicalEntryCount++] = entry;
            }
        }
        entries.resize(canonicalEntryCount);

        destination.setDimensions(rowCount, columnCount);
        destination.rowStarts.assign(rowCount + 1, 0);
        for (const auto& entry : entries) {
            if (entry.value != 0.0) {
                ++destination.rowStarts[entry.row + 1];
            }
        }
        for (std::size_t row = 0; row < rowCount; ++row) {
            destination.rowStarts[row + 1] += destination.rowStarts[row];
        }
        destination.columnIndices.reserve(destination.rowStarts.back());
        destination.values.reserve(destination.rowStarts.back());
        for (const auto& entry : entries) {
            if (entry.value != 0.0) {
                destination.columnIndices.push_back(entry.column);
                destination.values.push_back(entry.value);
            }
        }
        return true;
    }

    bool sparseNearlyEqual(const NativeSparseMatrix& lhs, const NativeSparseMatrix& rhs)
    {
        if ((lhs.rowCount != rhs.rowCount) || (lhs.columnCount != rhs.columnCount)) {
            return false;
        }
        for (std::size_t row = 0; row < lhs.rowCount; ++row) {
            std::size_t lhsEntry = lhs.rowStarts[row];
            std::size_t rhsEntry = rhs.rowStarts[row];
            const auto lhsEnd = lhs.rowStarts[row + 1];
            const auto rhsEnd = rhs.rowStarts[row + 1];
            while ((lhsEntry < lhsEnd) || (rhsEntry < rhsEnd)) {
                const auto lhsColumn = (lhsEntry < lhsEnd) ?
                    lhs.columnIndices[lhsEntry] : lhs.columnCount;
                const auto rhsColumn = (rhsEntry < rhsEnd) ?
                    rhs.columnIndices[rhsEntry] : rhs.columnCount;
                if (lhsColumn == rhsColumn) {
                    if (!nativeNearlyEqual(lhs.values[lhsEntry], rhs.values[rhsEntry])) {
                        return false;
                    }
                    ++lhsEntry;
                    ++rhsEntry;
                } else if (lhsColumn < rhsColumn) {
                    if (!nativeNearlyEqual(lhs.values[lhsEntry], 0.0)) {
                        return false;
                    }
                    ++lhsEntry;
                } else {
                    if (!nativeNearlyEqual(0.0, rhs.values[rhsEntry])) {
                        return false;
                    }
                    ++rhsEntry;
                }
            }
        }
        return true;
    }

    bool hasPiecewiseLinearCost(const GridOptObject* root)
    {
        if (root == nullptr) {
            return false;
        }
        std::vector<const GridOptObject*> pending{root};
        while (!pending.empty()) {
            const auto* object = pending.back();
            pending.pop_back();
            if (const auto* generator = dynamic_cast<const GridGenOpt*>(object);
                generator != nullptr) {
                if (generator->optFlags[GridGenOpt::PIECEWISE_LINEAR_COST]) {
                    return true;
                }
                continue;
            }
            if (const auto* area = dynamic_cast<const GridAreaOpt*>(object); area != nullptr) {
                for (index_t index = 0;; ++index) {
                    const auto* child = area->getArea(index);
                    if (child == nullptr) {
                        break;
                    }
                    pending.push_back(child);
                }
                for (index_t index = 0;; ++index) {
                    const auto* child = area->getBus(index);
                    if (child == nullptr) {
                        break;
                    }
                    pending.push_back(child);
                }
                continue;
            }
            if (const auto* bus = dynamic_cast<const GridBusOpt*>(object); bus != nullptr) {
                for (index_t index = 0;; ++index) {
                    const auto* child = bus->getGen(index);
                    if (child == nullptr) {
                        break;
                    }
                    pending.push_back(child);
                }
            }
        }
        return false;
    }

    const char* nativeSolveStatusName(NativeSolveStatus status)
    {
        switch (status) {
            case NativeSolveStatus::OPTIMAL:
                return "optimal";
            case NativeSolveStatus::INFEASIBLE:
                return "infeasible";
            case NativeSolveStatus::UNBOUNDED:
                return "unbounded";
            case NativeSolveStatus::SINGULAR:
                return "singular";
            case NativeSolveStatus::NUMERICAL_FAILURE:
                return "numerical_failure";
            case NativeSolveStatus::UNSUPPORTED:
                return "unsupported";
            case NativeSolveStatus::NONCONVEX:
                return "nonconvex";
            case NativeSolveStatus::ITERATION_LIMIT:
                return "iteration_limit";
        }
        return "unknown";
    }
}  // namespace

OptimizerInterface::OptimizerInterface(std::string_view optName): HelperObject(std::string{optName})
{
}

OptimizerInterface::OptimizerInterface(GridDynOptimization* gdo, const OptimizationMode& oMode):
    HelperObject("optim"), mode(oMode), mGridDynOptimization(gdo)
{
}

void OptimizerInterface::setOptimizationData(GridDynOptimization* gdo,
                                             const OptimizationMode& oMode)
{
    mode = oMode;
    if (gdo != nullptr) {
        mGridDynOptimization = gdo;
    }
}

int OptimizerInterface::allocate(count_t variableCount, count_t constraintCount)
{
    if ((variableCount == mVariableCount) && (constraintCount == mConstraintCount) && mAllocated) {
        return FUNCTION_EXECUTION_SUCCESS;
    }

    mVariableCount = variableCount;
    mConstraintCount = constraintCount;

    values.assign(variableCount, 0.0);
    lowerBounds.assign(variableCount, -kBigNum);
    upperBounds.assign(variableCount, kBigNum);
    gradient.assign(variableCount, 0.0);
    variableType.assign(variableCount, CONTINUOUS_OBJECTIVE_VARIABLE);
    tolerances.assign(variableCount, rtol);
    multipliers.assign(constraintCount, 0.0);
    scratch1.assign((std::max)(variableCount, constraintCount), 0.0);
    scratch2.assign((std::max)(variableCount, constraintCount), 0.0);

    constraintValues.assign(constraintCount, 0.0);
    constraintLowerBounds.assign(constraintCount, 0.0);
    constraintUpperBounds.assign(constraintCount, 0.0);

    linearObjective.reset();
    quadraticObjective.reset();
    linearConstraints.clear();
    constraintJacobian.clear();

    mInitialized = false;
    mAllocated = true;
    flags.set(OPT_INITIALIZED_FLAG, false);
    flags.set(OPT_ALLOCATED_FLAG, true);
    return FUNCTION_EXECUTION_SUCCESS;
}

void OptimizerInterface::initialize(double initTime)
{
    auto* root = rootOptimizationObject();
    if ((root != nullptr) && !mAllocated) {
        allocate(root->objSize(mode), root->constraintSize(mode));
    }
    if (!mAllocated) {
        logMessage(FUNCTION_EXECUTION_FAILURE, "optimizer initialize called before allocation");
        return;
    }
    loadInitialGuess(initTime);
    loadVariableBounds(initTime);
    loadVariableTypes();
    loadTolerances();
    loadQuadraticObjective(initTime);
    loadLinearConstraints(initTime);
    solveTime = initTime;
    mInitialized = true;
    flags.set(OPT_INITIALIZED_FLAG, true);
}

void OptimizerInterface::sparseReInit()
{
    constraintJacobian.clear();
    linearConstraints.clear();
}

OptimizationData OptimizerInterface::makeOptimizationData(double time)
{
    OptimizationData optimizationData(
        time, values.data(), ++mEvaluationCount, mVariableCount, mConstraintCount);
    optimizationData.multiplier = multipliers.data();
    optimizationData.scratch1 = scratch1.data();
    optimizationData.scratch2 = scratch2.data();
    return optimizationData;
}

OptimizationData OptimizerInterface::makeOptimizationData(double time,
                                                          const double candidateValues[])
{
    OptimizationData optimizationData(
        time, candidateValues, ++mEvaluationCount, mVariableCount, mConstraintCount);
    optimizationData.multiplier = multipliers.data();
    optimizationData.scratch1 = scratch1.data();
    optimizationData.scratch2 = scratch2.data();
    return optimizationData;
}

int OptimizerInterface::loadInitialGuess(double time)
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || !mAllocated) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    std::fill(values.begin(), values.end(), 0.0);
    root->guessState(time, values.data(), mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::loadVariableBounds(double time)
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || !mAllocated) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    std::fill(lowerBounds.begin(), lowerBounds.end(), -kBigNum);
    std::fill(upperBounds.begin(), upperBounds.end(), kBigNum);
    root->valueBounds(time, upperBounds.data(), lowerBounds.data(), mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::loadVariableTypes()
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || !mAllocated) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    std::fill(variableType.begin(), variableType.end(), CONTINUOUS_OBJECTIVE_VARIABLE);
    root->getVariableType(variableType.data(), mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::loadTolerances()
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || !mAllocated) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    std::fill(tolerances.begin(), tolerances.end(), rtol);
    root->getTols(tolerances.data(), mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::loadLinearObjective(double time, const double candidateValues[])
{
    auto* root = rootOptimizationObject();
    if (root == nullptr) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    linearObjective.reset();
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    root->linearObj(optimizationData, linearObjective, mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::loadQuadraticObjective(double time, const double candidateValues[])
{
    auto* root = rootOptimizationObject();
    if (root == nullptr) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    linearObjective.reset();
    quadraticObjective.reset();
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    root->quadraticObj(optimizationData, linearObjective, quadraticObjective, mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::loadLinearConstraints(double time, const double candidateValues[])
{
    auto* root = rootOptimizationObject();
    if (root == nullptr) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    std::fill(constraintLowerBounds.begin(), constraintLowerBounds.end(), 0.0);
    std::fill(constraintUpperBounds.begin(), constraintUpperBounds.end(), 0.0);
    linearConstraints.clear();
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    root->getConstraints(optimizationData,
                         linearConstraints,
                         constraintUpperBounds.data(),
                         constraintLowerBounds.data(),
                         mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::writeBack(double time)
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || !mAllocated || values.empty()) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    const double commitTime = (time != kNullVal) ? time : solveTime;
    const auto optimizationData = makeOptimizationData(commitTime);
    root->setValues(optimizationData, mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

double OptimizerInterface::objectiveFunction(double time, const double candidateValues[])
{
    auto* root = rootOptimizationObject();
    if (root == nullptr) {
        return kNullVal;
    }
    ++mObjectiveCallCount;
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    return root->objValue(optimizationData, mode);
}

int OptimizerInterface::gradientFunction(double time, const double candidateValues[], double grad[])
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || (grad == nullptr)) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    ++mGradientCallCount;
    std::fill(grad, grad + mVariableCount, 0.0);
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    root->gradient(optimizationData, grad, mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::constraintFunction(double time,
                                           const double candidateValues[],
                                           double constraints[])
{
    auto* root = rootOptimizationObject();
    if ((root == nullptr) || (constraints == nullptr)) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    ++mConstraintCallCount;
    std::fill(constraints, constraints + mConstraintCount, 0.0);
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    root->constraintValue(optimizationData, constraints, mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

int OptimizerInterface::constraintJacobianFunction(double time,
                                                   const double candidateValues[],
                                                   MatrixData<double>& matrixDataRef)
{
    auto* root = rootOptimizationObject();
    if (root == nullptr) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    ++mJacobianCallCount;
    matrixDataRef.clear();
    const auto optimizationData = (candidateValues == nullptr) ?
        makeOptimizationData(time) :
        makeOptimizationData(time, candidateValues);
    root->constraintJacobianElements(optimizationData, matrixDataRef, mode);
    return FUNCTION_EXECUTION_SUCCESS;
}

MatrixDataSparse<double>&
    OptimizerInterface::constraintJacobianFunction(double time, const double candidateValues[])
{
    constraintJacobianFunction(time, candidateValues, constraintJacobian);
    return constraintJacobian;
}

void OptimizerInterface::initializeJacArray(count_t size)
{
    setMaxNonZeros(size);
}

void OptimizerInterface::setMaxNonZeros(count_t nonZeroCount)
{
    nnz = nonZeroCount;
    constraintJacobian.reserve(nonZeroCount);
    linearConstraints.reserve(nonZeroCount);
}

static const std::map<std::string_view, int, std::less<std::string_view>> OPTIMIZER_FLAG_MAP{
    {"directlogging", OPT_DIRECT_LOGGING_FLAG},
    {"solver_log", OPT_DIRECT_LOGGING_FLAG},
    {"parallel", OPT_PARALLEL_FLAG},
    {"serial", -OPT_PARALLEL_FLAG},
    {"locked", OPT_LOCKED_FLAG},
    {"allocated", OPT_ALLOCATED_FLAG},
    {"initialized", OPT_INITIALIZED_FLAG},
    {"constantjacobian", OPT_CONSTANT_JACOBIAN_FLAG},
    {"mip", OPT_MIXED_INTEGER_FLAG},
    {"mixed_integer", OPT_MIXED_INTEGER_FLAG},
    {"relaxed", OPT_RELAXED_INTEGER_FLAG},
    {"relaxed_integer", OPT_RELAXED_INTEGER_FLAG},
    {"print_resid", OPT_PRINT_RESIDUALS_FLAG},
    {"print_residuals", OPT_PRINT_RESIDUALS_FLAG}};

double OptimizerInterface::get(std::string_view param) const
{
    if ((param == "tolerance") || (param == "rtol")) {
        return rtol;
    }
    if ((param == "size") || (param == "variables") || (param == "variable_count")) {
        return static_cast<double>(mVariableCount);
    }
    if (param == "integer_variables") {
        return static_cast<double>(
            std::count(variableType.begin(), variableType.end(), INTEGER_OBJECTIVE_VARIABLE));
    }
    if (param == "binary_variables") {
        return static_cast<double>(
            std::count(variableType.begin(), variableType.end(), BINARY_OBJECTIVE_VARIABLE));
    }
    if ((param == "constraints") || (param == "constraint_count")) {
        return static_cast<double>(mConstraintCount);
    }
    if ((param == "nonzeros") || (param == "nnz")) {
        return static_cast<double>(nnz);
    }
    if (param == "index") {
        return static_cast<double>(mode.offsetIndex);
    }
    if (param == "maxiterations") {
        return static_cast<double>(max_iterations);
    }
    if (param == "objective_calls") {
        return static_cast<double>(mObjectiveCallCount);
    }
    if (param == "gradient_calls") {
        return static_cast<double>(mGradientCallCount);
    }
    if (param == "constraint_calls") {
        return static_cast<double>(mConstraintCallCount);
    }
    if (param == "jacobian_calls") {
        return static_cast<double>(mJacobianCallCount);
    }
    return HelperObject::get(param);
}

void OptimizerInterface::set(std::string_view param, std::string_view val)
{
    if (param == "flags") {
        setMultipleFlags(this, val);
    } else if ((param == "optimizer_log") || (param == "solver_log")) {
        setFlag("directlogging", true);
    } else if (param == "mode") {
        const auto modeName = gmlc::utilities::convertToLowerCase(val);
        if ((modeName == "dc") || (modeName == "dcopf")) {
            mode.flowMode = FlowModel::DC;
        } else if ((modeName == "ac") || (modeName == "acopf")) {
            mode.flowMode = FlowModel::AC;
        } else if (modeName == "transport") {
            mode.flowMode = FlowModel::TRANSPORT;
        } else {
            HelperObject::set(param, val);
        }
    } else {
        HelperObject::set(param, val);
    }
}

void OptimizerInterface::set(std::string_view param, double val)
{
    if ((param == "tolerance") || (param == "rtol")) {
        rtol = val;
    } else if (param == "index") {
        mode.offsetIndex = static_cast<index_t>(val);
    } else if (param == "maxiterations") {
        max_iterations = static_cast<count_t>(val);
    } else if ((param == "dense") || (param == "sparse") || (param == "constantjacobian")) {
        setFlag(param, val > 0.1);
    } else {
        HelperObject::set(param, val);
    }
}

void OptimizerInterface::setFlag(std::string_view flag, bool val)
{
    if (flag == "dense") {
        flags.set(OPT_DENSE_FLAG, val);
        sparse = !val;
        return;
    }
    if (flag == "sparse") {
        flags.set(OPT_DENSE_FLAG, !val);
        sparse = val;
        return;
    }

    const auto foundFlag = OPTIMIZER_FLAG_MAP.find(flag);
    const int flagIndex = (foundFlag != OPTIMIZER_FLAG_MAP.end()) ? foundFlag->second : -60;
    if (flagIndex > -32) {
        if (flagIndex > 0) {
            flags.set(flagIndex, val);
        } else {
            flags.set(-flagIndex, !val);
        }
        sparse = !flags[OPT_DENSE_FLAG];
        constantJacobian = flags[OPT_CONSTANT_JACOBIAN_FLAG];
        return;
    }

    if (flag == "debug") {
        printLevel = OptimizerPrintLevel::DEBUG_PRINT;
    } else if (flag == "trap") {
        printLevel = OptimizerPrintLevel::ERROR_TRAP;
    } else if (flag == "error") {
        printLevel = OptimizerPrintLevel::ERROR_LOG;
    } else {
        HelperObject::setFlag(flag, val);
    }
}

bool OptimizerInterface::getFlag(std::string_view flag) const
{
    if (flag == "dense") {
        return flags[OPT_DENSE_FLAG];
    }
    if (flag == "sparse") {
        return !flags[OPT_DENSE_FLAG];
    }

    const auto foundFlag = OPTIMIZER_FLAG_MAP.find(flag);
    const int flagIndex = (foundFlag != OPTIMIZER_FLAG_MAP.end()) ? foundFlag->second : -60;
    if (flagIndex > -32) {
        if (flagIndex > 0) {
            return flags[flagIndex];
        }
        return !flags[-flagIndex];
    }
    return HelperObject::getFlag(flag);
}

void OptimizerInterface::logSolverStats(int logLevel, bool /*iconly*/) const
{
    if (logLevel > 0) {
        std::cout << "Optimizer " << getName() << ": variables=" << mVariableCount
                  << ", constraints=" << mConstraintCount
                  << ", objective calls=" << mObjectiveCallCount
                  << ", gradient calls=" << mGradientCallCount
                  << ", constraint calls=" << mConstraintCallCount
                  << ", jacobian calls=" << mJacobianCallCount << '\n';
    }
}

int OptimizerInterface::check_flag(void* flagvalue,
                                   std::string_view funcname,
                                   int opt,
                                   bool printError)
{
    // Check if SUNDIALS function returned nullptr pointer - no memory allocated
    if (opt == 0 && flagvalue == nullptr) {
        if (printError) {
            logMessage(1, std::string{funcname} + " failed - returned nullptr pointer");
        }
        return 1;
    }
    if (opt == 1) {
        // Check if flag < 0
        auto* errflag = reinterpret_cast<int*>(flagvalue);
        if (*errflag < 0) {
            if (printError) {
                logMessage(1,
                           std::string{funcname} +
                               " failed with flag = " + std::to_string(*errflag));
            }
            return 1;
        }
    } else if (opt == 2 && flagvalue == nullptr) {
        // Check if function returned nullptr pointer - no memory allocated
        if (printError) {
            logMessage(1, std::string{funcname} + " failed MEMORY_ERROR- returned nullptr pointer");
        }
        return 1;
    }
    return 0;
}

void OptimizerInterface::logMessage(int errorCode, std::string_view message)
{
    if ((errorCode > 0) && (printLevel == OptimizerPrintLevel::DEBUG_PRINT) &&
        (mGridDynOptimization != nullptr)) {
        logging::logTo(mGridDynOptimization, mGridDynOptimization, PrintLevel::DEBUG, message);
    }
    if (errorCode != 0) {
        lastErrorCode = errorCode;
        lastErrorString = message;
        if ((printLevel == OptimizerPrintLevel::ERROR_LOG) && (mGridDynOptimization != nullptr)) {
            logging::logTo(mGridDynOptimization,
                           mGridDynOptimization,
                           PrintLevel::WARNING,
                           message);
        }
    }
}

GridOptObject* OptimizerInterface::rootOptimizationObject() const
{
    return (mGridDynOptimization != nullptr) ? mGridDynOptimization->getOptimizationObject() :
                                               nullptr;
}

BasicOptimizer::BasicOptimizer(std::string_view optName): OptimizerInterface(optName) {}

BasicOptimizer::BasicOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode):
    OptimizerInterface(gdo, oMode)
{
}

int BasicOptimizer::allocate(count_t variableCount, count_t constraintCount)
{
    return OptimizerInterface::allocate(variableCount, constraintCount);
}

void BasicOptimizer::dynObjectInitializeA(double /*t0*/)
{
    if (!mAllocated) {
        //  return (-2);
    }
    mInitialized = true;
    flags.set(OPT_INITIALIZED_FLAG, true);
    // return FUNCTION_EXECUTION_SUCCESS;
}

EconomicDispatchOptimizer::EconomicDispatchOptimizer(std::string_view optName):
    OptimizerInterface(optName)
{
}

EconomicDispatchOptimizer::EconomicDispatchOptimizer(GridDynOptimization* gdo,
                                                     const OptimizationMode& oMode):
    OptimizerInterface(gdo, oMode)
{
}

int EconomicDispatchOptimizer::solve(double tStop, double& tReturn)
{
    if (!mInitialized) {
        initialize(tStop);
    }
    if (!mInitialized) {
        logMessage(FUNCTION_EXECUTION_FAILURE, "economic dispatch optimizer is not initialized");
        return FUNCTION_EXECUTION_FAILURE;
    }

    loadQuadraticObjective(tStop);
    linearObjective.sortIndex();
    linearObjective.compact();
    quadraticObjective.sortIndex();
    quadraticObjective.compact();

    constraintJacobian.clear();
    constraintJacobianFunction(tStop, values.data(), constraintJacobian);
    constraintJacobian.sortIndex();
    constraintJacobian.compact();

    std::vector<bool> hasConstraintParticipation(values.size(), false);
    for (const auto& element : constraintJacobian) {
        if ((element.col >= 0) && std::cmp_less(element.col, hasConstraintParticipation.size()) &&
            (std::abs(element.data) > 0.0)) {
            hasConstraintParticipation[static_cast<std::size_t>(element.col)] = true;
        }
    }

    std::vector<DispatchVariable> dispatchVariables;
    dispatchVariables.reserve(values.size());
    for (std::size_t variableIndex = 0; variableIndex < values.size(); ++variableIndex) {
        const double lower = finiteLower(lowerBounds[variableIndex]);
        const double upper = finiteUpper(upperBounds[variableIndex]);
        if ((upper <= lower) || !hasConstraintParticipation[variableIndex]) {
            continue;
        }
        const double linearCost = linearObjective.at(static_cast<index_t>(variableIndex));
        const double quadraticCost = quadraticObjective.at(static_cast<index_t>(variableIndex));
        if ((linearCost == 0.0) && (quadraticCost == 0.0)) {
            continue;
        }
        dispatchVariables.push_back(DispatchVariable{.mIndex = static_cast<index_t>(variableIndex),
                                                     .mLower = lower,
                                                     .mUpper = upper,
                                                     .mLinearCost = linearCost,
                                                     .mQuadraticCost = quadraticCost});
    }

    if (dispatchVariables.empty()) {
        logMessage(FUNCTION_EXECUTION_FAILURE,
                   "economic dispatch found no bounded costed dispatch variables");
        return FUNCTION_EXECUTION_FAILURE;
    }

    auto candidateValues = values;
    for (const auto& dispatchVariable : dispatchVariables) {
        candidateValues[dispatchVariable.mIndex] = dispatchVariable.mLower;
    }

    if (constraintFunction(tStop, candidateValues.data(), constraintValues.data()) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }

    double requiredAdditionalDispatch = 0.0;
    for (std::size_t constraintIndex = 0; constraintIndex < constraintValues.size();
         ++constraintIndex) {
        if (std::abs(constraintUpperBounds[constraintIndex] -
                     constraintLowerBounds[constraintIndex]) <= rtol) {
            requiredAdditionalDispatch -= constraintValues[constraintIndex];
        }
    }

    if (requiredAdditionalDispatch < -rtol) {
        dispatchImbalance = requiredAdditionalDispatch;
        logMessage(FUNCTION_EXECUTION_FAILURE,
                   "economic dispatch minimum generation exceeds estimated demand");
        return FUNCTION_EXECUTION_FAILURE;
    }

    std::sort(dispatchVariables.begin(),
              dispatchVariables.end(),
              [](const DispatchVariable& variableA, const DispatchVariable& variableB) {
                  return marginalCostAtLower(variableA) < marginalCostAtLower(variableB);
              });

    for (const auto& dispatchVariable : dispatchVariables) {
        if (requiredAdditionalDispatch <= rtol) {
            break;
        }
        const double availableDispatch = dispatchVariable.mUpper - dispatchVariable.mLower;
        const double dispatchChange = (std::min)(availableDispatch, requiredAdditionalDispatch);
        candidateValues[dispatchVariable.mIndex] += dispatchChange;
        requiredAdditionalDispatch -= dispatchChange;
    }

    dispatchImbalance = requiredAdditionalDispatch;
    if (requiredAdditionalDispatch > rtol) {
        logMessage(FUNCTION_EXECUTION_FAILURE,
                   "economic dispatch could not meet demand within generator bounds");
        return FUNCTION_EXECUTION_FAILURE;
    }

    values = std::move(candidateValues);
    constraintFunction(tStop, values.data(), constraintValues.data());
    std::fill(gradient.begin(), gradient.end(), 0.0);
    gradientFunction(tStop, values.data(), gradient.data());
    solveTime = tStop;
    tReturn = tStop;
    lastErrorCode = FUNCTION_EXECUTION_SUCCESS;
    lastErrorString.clear();
    return FUNCTION_EXECUTION_SUCCESS;
}

double EconomicDispatchOptimizer::get(std::string_view param) const
{
    if ((param == "dispatch_imbalance") || (param == "imbalance")) {
        return dispatchImbalance;
    }
    return OptimizerInterface::get(param);
}

NativeOptimizer::NativeOptimizer(std::string_view optName): OptimizerInterface(optName)
{
    setFlag("dense", true);
}

NativeOptimizer::NativeOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode):
    OptimizerInterface(gdo, oMode)
{
    setFlag("dense", true);
}

int NativeOptimizer::prepareProblemData(double time)
{
    mProblemDataLoaded = false;
    mSolutionValid = false;
    mLastSolveResult = NativeSolveResult{};
    const auto reject = [this](const std::string& message) {
        logMessage(FUNCTION_EXECUTION_FAILURE, message);
        return FUNCTION_EXECUTION_FAILURE;
    };

    if (mode.flowMode != FlowModel::DC) {
        return reject("native optimizer supports only the continuous DC flow model");
    }
    if (mode.linMode == LinearityMode::NONLINEAR) {
        return reject("native optimizer does not support nonlinear optimization modes");
    }
    if ((mode.linMode != LinearityMode::LINEAR) && (mode.linMode != LinearityMode::QUADRATIC)) {
        return reject("native optimizer received an unknown linearity mode");
    }
    if (hasPiecewiseLinearCost(rootOptimizationObject())) {
        return reject("native optimizer does not support piecewise-linear generator costs");
    }

    if (!mInitialized) {
        initialize(time);
    }
    if (!mInitialized) {
        logMessage(FUNCTION_EXECUTION_FAILURE, "native optimizer is not initialized");
        return FUNCTION_EXECUTION_FAILURE;
    }

    if (loadVariableBounds(time) != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    if (loadVariableTypes() != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    if (loadTolerances() != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    quadraticObjective.reset();
    if (mode.linMode == LinearityMode::LINEAR) {
        if (loadLinearObjective(time) != FUNCTION_EXECUTION_SUCCESS) {
            return FUNCTION_EXECUTION_FAILURE;
        }
    } else if (loadQuadraticObjective(time) != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    linearObjective.sortIndex();
    linearObjective.compact();
    quadraticObjective.sortIndex();
    quadraticObjective.compact();

    if (loadLinearConstraints(time) != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    linearConstraints.sortIndex();
    linearConstraints.compact();

    if (constraintFunction(time, values.data(), constraintValues.data()) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    if (gradientFunction(time, values.data(), gradient.data()) != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }

    constraintJacobian.clear();
    if (constraintJacobianFunction(time, values.data(), constraintJacobian) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    constraintJacobian.sortIndex();
    constraintJacobian.compact();

    NativeQpProblem candidate;
    candidate.modelVersion = mProblemVersion + 1;
    candidate.mode = mode;
    candidate.variableCount = static_cast<std::size_t>(mVariableCount);
    candidate.constraintCount = static_cast<std::size_t>(mConstraintCount);
    candidate.initialValues = values;
    candidate.variableLowerBounds.resize(candidate.variableCount);
    candidate.variableUpperBounds.resize(candidate.variableCount);
    candidate.linearObjective.assign(candidate.variableCount, 0.0);
    candidate.quadraticObjective.assign(candidate.variableCount, 0.0);
    candidate.variableTypes = variableType;
    candidate.tolerances = tolerances;
    candidate.initialGradient = gradient;
    candidate.initialConstraintValues = constraintValues;

    for (std::size_t column = 0; column < candidate.variableCount; ++column) {
        candidate.variableLowerBounds[column] = nativeLowerBound(lowerBounds[column]);
        candidate.variableUpperBounds[column] = nativeUpperBound(upperBounds[column]);
        candidate.linearObjective[column] = linearObjective.at(static_cast<index_t>(column));
        if (mode.linMode == LinearityMode::QUADRATIC) {
            candidate.quadraticObjective[column] =
                quadraticObjective.at(static_cast<index_t>(column));
        }
    }

    candidate.constraintLowerBounds.resize(candidate.constraintCount);
    candidate.constraintUpperBounds.resize(candidate.constraintCount);
    candidate.constraintOffsets.assign(candidate.constraintCount, 0.0);
    candidate.constraintMatrix.setDimensions(candidate.constraintCount, candidate.variableCount);
    std::vector<bool> explicitRows(candidate.constraintCount, false);
    for (const auto& element : linearConstraints) {
        if ((element.row < 0) || (element.col < 0) ||
            std::cmp_greater_equal(element.row, candidate.constraintCount) ||
            std::cmp_greater_equal(element.col, candidate.variableCount)) {
            return reject(
                "native optimizer received an explicit linear row outside the problem dimensions");
        }
        explicitRows[static_cast<std::size_t>(element.row)] = true;
    }
    for (std::size_t row = 0; row < candidate.constraintCount; ++row) {
        candidate.constraintLowerBounds[row] = nativeLowerBound(constraintLowerBounds[row]);
        candidate.constraintUpperBounds[row] = nativeUpperBound(constraintUpperBounds[row]);
    }

    std::string matrixError;
    if (!loadSparseMatrix(constraintJacobian,
                          candidate.constraintCount,
                          candidate.variableCount,
                          candidate.constraintMatrix,
                          matrixError)) {
        reject("native optimizer could not materialize the callback Jacobian: " + matrixError);
        return FUNCTION_EXECUTION_FAILURE;
    }

    for (std::size_t row = 0; row < candidate.constraintCount; ++row) {
        const double rowAtInitial = candidate.constraintMatrix.rowDot(row, values);
        candidate.constraintOffsets[row] = constraintValues[row] - rowAtInitial;
        if (explicitRows[row]) {
            // The legacy explicit-row callback exports bounds on A*x, while
            // the canonical callback bounds apply to A*x + offset.
            candidate.constraintLowerBounds[row] += candidate.constraintOffsets[row];
            candidate.constraintUpperBounds[row] += candidate.constraintOffsets[row];
        }
    }

    std::vector<double> zeroValues(candidate.variableCount, 0.0);
    candidate.objectiveConstant = objectiveFunction(time, zeroValues.data());
    if (!std::isfinite(candidate.objectiveConstant)) {
        return reject("native optimizer received a non-finite objective constant");
    }

    candidate.variableNames.resize(candidate.variableCount);
    for (std::size_t column = 0; column < candidate.variableCount; ++column) {
        candidate.variableNames[column] = "x[" + std::to_string(column) + "]";
    }
    if (auto* root = rootOptimizationObject(); root != nullptr) {
        stringVec objectiveNames;
        root->getObjectiveNames(objectiveNames, mode);
        for (std::size_t column = 0;
             (column < candidate.variableCount) && (column < objectiveNames.size());
             ++column) {
            if (!objectiveNames[column].empty()) {
                candidate.variableNames[column] = objectiveNames[column];
            }
        }
    }
    candidate.constraintNames.resize(candidate.constraintCount);
    for (std::size_t row = 0; row < candidate.constraintCount; ++row) {
        candidate.constraintNames[row] = "constraint[" + std::to_string(row) + "]";
    }

    std::string validationError;
    if (!candidate.validate(&validationError)) {
        return reject("native optimizer problem validation failed: " + validationError);
    }

    for (std::size_t column = 0; column < candidate.variableCount; ++column) {
        if (candidate.variableTypes[column] != CONTINUOUS_OBJECTIVE_VARIABLE) {
            candidate.classification = NativeProblemClass::UNSUPPORTED_INTEGER;
            return reject("native optimizer supports continuous variables only");
        }
    }

    bool hasQuadraticTerm = false;
    for (const auto quadraticCoefficient : candidate.quadraticObjective) {
        if (quadraticCoefficient < -1e-12) {
            candidate.classification = NativeProblemClass::NONCONVEX;
            return reject("native optimizer requires a convex diagonal quadratic objective");
        }
        hasQuadraticTerm = hasQuadraticTerm || (std::abs(quadraticCoefficient) > 1e-12);
    }
    candidate.classification = hasQuadraticTerm ?
        NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC :
        NativeProblemClass::SUPPORTED_LINEAR;

    const auto validationPoint = nativeValidationPoint(candidate);
    std::vector<double> validationConstraints(candidate.constraintCount, 0.0);
    if (constraintFunction(time, validationPoint.data(), validationConstraints.data()) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return reject("native optimizer could not evaluate constraints at its validation point");
    }
    std::vector<double> validationGradient(candidate.variableCount, 0.0);
    if (gradientFunction(time, validationPoint.data(), validationGradient.data()) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return reject(
            "native optimizer could not evaluate the objective gradient at its validation point");
    }
    const double validationObjective = objectiveFunction(time, validationPoint.data());
    if (!std::isfinite(validationObjective)) {
        return reject("native optimizer received a non-finite objective at its validation point");
    }
    if (!nativeNearlyEqual(candidate.objectiveValue(candidate.initialValues),
                           objectiveFunction(time, candidate.initialValues.data()))) {
        return reject(
            "native optimizer objective coefficients do not match the objective callback");
    }
    if (!nativeNearlyEqual(candidate.objectiveValue(validationPoint), validationObjective)) {
        return reject("native optimizer objective is not a linear or diagonal quadratic function");
    }
    const auto initialGradient = candidate.objectiveGradient(candidate.initialValues);
    const auto pointGradient = candidate.objectiveGradient(validationPoint);
    for (std::size_t column = 0; column < candidate.variableCount; ++column) {
        if (!nativeNearlyEqual(initialGradient[column], candidate.initialGradient[column]) ||
            !nativeNearlyEqual(pointGradient[column], validationGradient[column])) {
            return reject("native optimizer objective gradient does not match its coefficients");
        }
    }

    for (std::size_t row = 0; row < candidate.constraintCount; ++row) {
        if (!nativeNearlyEqual(candidate.constraintValue(row, candidate.initialValues),
                               candidate.initialConstraintValues[row]) ||
            !nativeNearlyEqual(candidate.constraintValue(row, validationPoint),
                               validationConstraints[row])) {
            return reject(
                "native optimizer constraint callback is not affine in the materialized variables");
        }
    }

    MatrixDataSparse<double> pointJacobian;
    if (constraintJacobianFunction(time, validationPoint.data(), pointJacobian) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return reject(
            "native optimizer could not evaluate the constraint Jacobian at its validation point");
    }
    NativeSparseMatrix validationJacobian;
    matrixError.clear();
    if (!loadSparseMatrix(pointJacobian,
                          candidate.constraintCount,
                          candidate.variableCount,
                          validationJacobian,
                          matrixError)) {
        reject("native optimizer could not materialize the validation Jacobian: " + matrixError);
        return FUNCTION_EXECUTION_FAILURE;
    }
    if (!sparseNearlyEqual(candidate.constraintMatrix, validationJacobian)) {
        return reject("native optimizer constraint Jacobian is not constant");
    }

    for (const auto& element : linearConstraints) {
        if ((element.row < 0) || (element.col < 0) ||
            std::cmp_greater_equal(element.row, candidate.constraintCount) ||
            std::cmp_greater_equal(element.col, candidate.variableCount)) {
            return reject(
                "native optimizer received an explicit linear row outside the problem dimensions");
        }
        if (!std::isfinite(element.data)) {
            return reject("native optimizer received a non-finite explicit linear coefficient");
        }
        const auto row = static_cast<std::size_t>(element.row);
        const auto column = static_cast<std::size_t>(element.col);
        if (!nativeNearlyEqual(element.data, candidate.constraintMatrix.coefficient(row, column))) {
            return reject(
                "native optimizer explicit linear rows disagree with the callback Jacobian");
        }
    }
    for (std::size_t row = 0; row < candidate.constraintCount; ++row) {
        if (!explicitRows[row]) {
            continue;
        }
        const double explicitLower = nativeLowerBound(constraintLowerBounds[row]);
        const double explicitUpper = nativeUpperBound(constraintUpperBounds[row]);
        if (!nativeNearlyEqual(explicitLower, candidate.solverConstraintLowerBound(row)) ||
            !nativeNearlyEqual(explicitUpper, candidate.solverConstraintUpperBound(row))) {
            return reject(
                "native optimizer explicit row bounds disagree with the affine callback row");
        }
    }

    candidate.valid = true;
    candidate.validationError.clear();
    mProblemVersion = candidate.modelVersion;
    mProblem = std::move(candidate);

    solveTime = time;
    mProblemDataLoaded = true;
    return FUNCTION_EXECUTION_SUCCESS;
}

void NativeOptimizer::recordSolveFailure(std::string message)
{
    mSolutionValid = false;
    mLastSolveResult = NativeSolveResult{};
    mLastSolveResult.status = NativeSolveStatus::NUMERICAL_FAILURE;
    mLastSolveResult.message = std::move(message);
}

int NativeOptimizer::acceptSolution(double time, NativeSolveResult result)
{
    mSolutionValid = false;
    mLastSolveResult = std::move(result);
    const auto solverPrefix = std::string{backendName()} + " optimizer";
    if (!mLastSolveResult.successful()) {
        logMessage(FUNCTION_EXECUTION_FAILURE,
                   solverPrefix + " solve failed with status " +
                       nativeSolveStatusName(mLastSolveResult.status) + ": " +
                       mLastSolveResult.message);
        return FUNCTION_EXECUTION_FAILURE;
    }

    NativeDenseSolverOptions options;
    options.maxIterations = (max_iterations > 0) ? static_cast<std::size_t>(max_iterations) : 1U;
    options.feasibilityTolerance = rtol;
    options.optimalityTolerance = rtol;
    const auto rejectCandidate = [this, &solverPrefix](const std::string& detail) {
        mSolutionValid = false;
        mLastSolveResult.status = NativeSolveStatus::NUMERICAL_FAILURE;
        mLastSolveResult.message = solverPrefix + " " + detail;
        logMessage(FUNCTION_EXECUTION_FAILURE, mLastSolveResult.message);
        return FUNCTION_EXECUTION_FAILURE;
    };
    const auto& candidate = mLastSolveResult.values;
    if (candidate.size() != mProblem.variableCount ||
        !std::all_of(candidate.begin(), candidate.end(), [](double value) {
            return std::isfinite(value);
        })) {
        return rejectCandidate("returned an invalid candidate vector");
    }

    const double callbackObjective = objectiveFunction(time, candidate.data());
    if (!std::isfinite(callbackObjective) ||
        !nativeNearlyEqual(callbackObjective, mLastSolveResult.objectiveValue) ||
        !nativeNearlyEqual(callbackObjective, mProblem.objectiveValue(candidate))) {
        return rejectCandidate("candidate objective failed callback validation");
    }

    std::vector<double> callbackGradient(mProblem.variableCount, 0.0);
    if (gradientFunction(time, candidate.data(), callbackGradient.data()) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return rejectCandidate("could not validate the candidate gradient");
    }
    const auto nativeGradient = mProblem.objectiveGradient(candidate);
    for (std::size_t column = 0; column < mProblem.variableCount; ++column) {
        if (!std::isfinite(callbackGradient[column]) ||
            !nativeNearlyEqual(callbackGradient[column], nativeGradient[column])) {
            return rejectCandidate("candidate gradient failed callback validation");
        }
    }

    std::vector<double> callbackConstraints(mProblem.constraintCount, 0.0);
    if (constraintFunction(time, candidate.data(), callbackConstraints.data()) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return rejectCandidate("could not validate the candidate constraints");
    }
    double maximumConstraintViolation = 0.0;
    for (std::size_t row = 0; row < mProblem.constraintCount; ++row) {
        const double nativeValue = mProblem.constraintValue(row, candidate);
        if (!std::isfinite(callbackConstraints[row]) ||
            !nativeNearlyEqual(callbackConstraints[row], nativeValue)) {
            return rejectCandidate("candidate constraint failed callback validation");
        }
        const double lower = mProblem.constraintLowerBounds[row];
        const double upper = mProblem.constraintUpperBounds[row];
        if (std::isfinite(lower)) {
            maximumConstraintViolation =
                (std::max)(maximumConstraintViolation, lower - callbackConstraints[row]);
        }
        if (std::isfinite(upper)) {
            maximumConstraintViolation =
                (std::max)(maximumConstraintViolation, callbackConstraints[row] - upper);
        }
    }
    maximumConstraintViolation = (std::max)(0.0, maximumConstraintViolation);
    if (maximumConstraintViolation > options.feasibilityTolerance) {
        return rejectCandidate("candidate violates a constraint bound");
    }

    double maximumBoundViolation = 0.0;
    for (std::size_t column = 0; column < mProblem.variableCount; ++column) {
        const double value = candidate[column];
        const double lower = mProblem.variableLowerBounds[column];
        const double upper = mProblem.variableUpperBounds[column];
        if (std::isfinite(lower)) {
            maximumBoundViolation = (std::max)(maximumBoundViolation, lower - value);
        }
        if (std::isfinite(upper)) {
            maximumBoundViolation = (std::max)(maximumBoundViolation, value - upper);
        }
    }
    maximumBoundViolation = (std::max)(0.0, maximumBoundViolation);
    if (maximumBoundViolation > options.feasibilityTolerance) {
        return rejectCandidate("candidate violates a variable bound");
    }

    MatrixDataSparse<double> callbackJacobian;
    if (constraintJacobianFunction(time, candidate.data(), callbackJacobian) !=
        FUNCTION_EXECUTION_SUCCESS) {
        return rejectCandidate("could not validate the candidate Jacobian");
    }
    NativeSparseMatrix callbackSparseJacobian;
    std::string matrixError;
    if (!loadSparseMatrix(callbackJacobian,
                          mProblem.constraintCount,
                          mProblem.variableCount,
                          callbackSparseJacobian,
                          matrixError)) {
        return rejectCandidate("candidate Jacobian has invalid entries: " + matrixError);
    }
    if (!sparseNearlyEqual(callbackSparseJacobian, mProblem.constraintMatrix)) {
        return rejectCandidate("candidate Jacobian failed callback validation");
    }

    // Do not expose a candidate as solved until every callback and bound check
    // has passed. A numerically feasible backend vector is not enough if the
    // physical model's callback representation has drifted.
    mLastSolveResult.maximumConstraintViolation = maximumConstraintViolation;
    mLastSolveResult.maximumBoundViolation = maximumBoundViolation;
    values = candidate;
    gradient = std::move(callbackGradient);
    constraintValues = std::move(callbackConstraints);
    constraintJacobian = std::move(callbackJacobian);
    mSolutionValid = true;
    solveTime = time;
    lastErrorCode = FUNCTION_EXECUTION_SUCCESS;
    lastErrorString.clear();
    return FUNCTION_EXECUTION_SUCCESS;
}

int NativeOptimizer::solve(double tStop, double& tReturn)
{
    tReturn = tStop;
    if (prepareProblemData(tStop) != FUNCTION_EXECUTION_SUCCESS) {
        recordSolveFailure(lastErrorString);
        return FUNCTION_EXECUTION_FAILURE;
    }

    NativeDenseSolverOptions options;
    options.maxIterations = (max_iterations > 0) ? static_cast<std::size_t>(max_iterations) : 1U;
    options.feasibilityTolerance = rtol;
    options.optimalityTolerance = rtol;
    // NativeDenseSolver works from the immutable solver-neutral snapshot. It
    // performs scaling, presolve, Phase-I, and the active-set KKT iterations;
    // this layer remains responsible for validating the resulting vector
    // against the original GridDyn callbacks.
    return acceptSolution(tStop, NativeDenseSolver::solve(mProblem, options));
}

int NativeOptimizer::writeBack(double time)
{
    if (!mSolutionValid || !mLastSolveResult.successful()) {
        logMessage(FUNCTION_EXECUTION_FAILURE,
                   std::string{backendName()} +
                       " optimizer writeBack requires a validated optimal solution");
        return FUNCTION_EXECUTION_FAILURE;
    }
    return OptimizerInterface::writeBack(time);
}

double NativeOptimizer::get(std::string_view param) const
{
    if ((param == "problem_loaded") || (param == "problem_assembled")) {
        return mProblemDataLoaded ? 1.0 : 0.0;
    }
    if ((param == "native_solver") || (param == "native")) {
        return 1.0;
    }
    if ((param == "solution_valid") || (param == "optimal")) {
        return mSolutionValid ? 1.0 : 0.0;
    }
    if ((param == "solve_status") || (param == "status")) {
        return static_cast<double>(mLastSolveResult.status);
    }
    if ((param == "objective") || (param == "objective_value")) {
        return mLastSolveResult.objectiveValue;
    }
    if ((param == "primal_violation") || (param == "constraint_violation")) {
        return mLastSolveResult.maximumConstraintViolation;
    }
    if (param == "bound_violation") {
        return mLastSolveResult.maximumBoundViolation;
    }
    if (param == "stationarity") {
        return mLastSolveResult.maximumStationarity;
    }
    if (param == "complementarity") {
        return mLastSolveResult.maximumComplementarity;
    }
    if ((param == "active_set_size") || (param == "active_constraints")) {
        return static_cast<double>(mLastSolveResult.activeSetSize);
    }
    if ((param == "iterations") || (param == "iteration_count")) {
        return static_cast<double>(mLastSolveResult.iterationCount);
    }
    return OptimizerInterface::get(param);
}

std::shared_ptr<OptimizerInterface> makeOptimizer(GridDynOptimization* gdo,
                                                  const OptimizationMode& oMode)
{
    return makeOptimizer(gdo, oMode, "basic");
}

std::shared_ptr<OptimizerInterface>
    makeOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode, std::string_view type)
{
    auto optimizer = makeOptimizer(type);
    if (optimizer == nullptr) {
        optimizer = std::make_shared<BasicOptimizer>();
    }
    optimizer->setOptimizationData(gdo, oMode);
    return optimizer;
}

std::shared_ptr<OptimizerInterface> makeOptimizer(std::string_view type)
{
    return CoreClassFactory<OptimizerInterface>::instance()->createObject(type);
}

}  // namespace griddyn
