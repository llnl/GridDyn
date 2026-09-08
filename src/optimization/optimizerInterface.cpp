/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "optimizerInterface.h"

#include "core/CoreExceptions.h"
#include "core/FactoryTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gridDynOpt.h"
#include "gridOptObjects.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
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
    if (loadQuadraticObjective(time) != FUNCTION_EXECUTION_SUCCESS) {
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

    solveTime = time;
    mProblemDataLoaded = true;
    return FUNCTION_EXECUTION_SUCCESS;
}

int NativeOptimizer::solve(double tStop, double& tReturn)
{
    if (prepareProblemData(tStop) != FUNCTION_EXECUTION_SUCCESS) {
        return FUNCTION_EXECUTION_FAILURE;
    }
    tReturn = tStop;
    logMessage(FUNCTION_EXECUTION_FAILURE,
               "native optimizer problem assembly is implemented; solve math is not implemented");
    return FUNCTION_EXECUTION_FAILURE;
}

double NativeOptimizer::get(std::string_view param) const
{
    if ((param == "problem_loaded") || (param == "problem_assembled")) {
        return mProblemDataLoaded ? 1.0 : 0.0;
    }
    if ((param == "native_solver") || (param == "native")) {
        return 1.0;
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
