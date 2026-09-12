/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "highsOptimizer.h"

#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringConversion.h"
#include <Highs.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
namespace {

    NativeSolveStatus nativeStatus(HighsModelStatus status)
    {
        switch (status) {
            case HighsModelStatus::kOptimal:
                return NativeSolveStatus::OPTIMAL;
            case HighsModelStatus::kInfeasible:
            case HighsModelStatus::kUnboundedOrInfeasible:
                return NativeSolveStatus::INFEASIBLE;
            case HighsModelStatus::kUnbounded:
                return NativeSolveStatus::UNBOUNDED;
            case HighsModelStatus::kIterationLimit:
            case HighsModelStatus::kTimeLimit:
            case HighsModelStatus::kSolutionLimit:
                return NativeSolveStatus::ITERATION_LIMIT;
            case HighsModelStatus::kNotset:
            case HighsModelStatus::kLoadError:
            case HighsModelStatus::kModelError:
            case HighsModelStatus::kPresolveError:
            case HighsModelStatus::kSolveError:
            case HighsModelStatus::kPostsolveError:
            case HighsModelStatus::kModelEmpty:
            case HighsModelStatus::kObjectiveBound:
            case HighsModelStatus::kObjectiveTarget:
            case HighsModelStatus::kUnknown:
            case HighsModelStatus::kInterrupt:
            case HighsModelStatus::kMemoryLimit:
            case HighsModelStatus::kHighsInterrupt:
                return NativeSolveStatus::NUMERICAL_FAILURE;
        }
        return NativeSolveStatus::NUMERICAL_FAILURE;
    }

    std::size_t positiveIterationCount(HighsInt value)
    {
        return (value > 0) ? static_cast<std::size_t>(value) : 0U;
    }

    bool setHighsOptions(Highs& highs, double tolerance, count_t maxIterations)
    {
        if (highs.setOptionValue("output_flag", false) == HighsStatus::kError ||
            highs.setOptionValue("presolve", "on") == HighsStatus::kError ||
            highs.setOptionValue("primal_feasibility_tolerance", tolerance) ==
                HighsStatus::kError ||
            highs.setOptionValue("dual_feasibility_tolerance", tolerance) == HighsStatus::kError) {
            return false;
        }
        if (maxIterations > 0) {
            const auto iterationLimit = static_cast<HighsInt>(maxIterations);
            if (highs.setOptionValue("simplex_iteration_limit", iterationLimit) ==
                    HighsStatus::kError ||
                highs.setOptionValue("ipm_iteration_limit", iterationLimit) ==
                    HighsStatus::kError) {
                return false;
            }
        }
        return true;
    }

    HighsScalingMode parseScalingMode(std::string_view param, std::string_view value)
    {
        const auto modeName = gmlc::utilities::convertToLowerCase(value);
        if ((modeName == "no_scaling") || (modeName == "none") || (modeName == "off")) {
            return HighsScalingMode::NO_SCALING;
        }
        if ((modeName == "scaling") || (modeName == "on")) {
            return HighsScalingMode::SCALING;
        }
        if ((modeName == "auto") || (modeName == "automatic")) {
            return HighsScalingMode::AUTO;
        }
        throw InvalidParameterValue(param);
    }

    struct HighsScalingData {
        bool mEnabled = false;
        std::vector<double> mRowScale;
        std::vector<double> mVariableScale;
    };

    double scaleFromMagnitude(double magnitude)
    {
        if (!(magnitude > 0.0)) {
            return 1.0;
        }
        constexpr double minimumScale = 1.0e-6;
        constexpr double maximumScale = 1.0e6;
        return (std::max)(minimumScale, (std::min)(maximumScale, 1.0 / magnitude));
    }

    HighsScalingData makeScalingData(const NativeQpProblem& problem, bool enabled)
    {
        HighsScalingData scaling;
        scaling.mEnabled = enabled;
        scaling.mRowScale.assign(problem.constraintCount, 1.0);
        scaling.mVariableScale.assign(problem.variableCount, 1.0);
        if (!enabled) {
            return scaling;
        }

        // Use x = D*z. Column scaling includes the diagonal Hessian so that both
        // the constraint matrix and the QP curvature remain on comparable scales.
        // The canonical GridDyn problem is not modified; D is undone on return.
        std::vector<double> columnMagnitude(problem.variableCount, 0.0);
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            for (std::size_t entry = problem.constraintMatrix.rowStarts[row];
                 entry < problem.constraintMatrix.rowStarts[row + 1U];
                 ++entry) {
                const auto column = problem.constraintMatrix.columnIndices[entry];
                columnMagnitude[column] =
                    (std::max)(columnMagnitude[column],
                               std::abs(problem.constraintMatrix.values[entry]));
            }
        }
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            if (problem.quadraticObjective[column] != 0.0) {
                columnMagnitude[column] =
                    (std::max)(columnMagnitude[column],
                               std::sqrt(std::abs(2.0 * problem.quadraticObjective[column])));
            }
            scaling.mVariableScale[column] = scaleFromMagnitude(columnMagnitude[column]);
        }

        // Scale each bounded row by the largest transformed coefficient. Bounds
        // are scaled by the same positive factor, preserving the feasible set.
        std::vector<double> rowMagnitude(problem.constraintCount, 0.0);
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            for (std::size_t entry = problem.constraintMatrix.rowStarts[row];
                 entry < problem.constraintMatrix.rowStarts[row + 1U];
                 ++entry) {
                const auto column = problem.constraintMatrix.columnIndices[entry];
                rowMagnitude[row] = (std::max)(rowMagnitude[row],
                                               std::abs(problem.constraintMatrix.values[entry] *
                                                        scaling.mVariableScale[column]));
            }
            scaling.mRowScale[row] = scaleFromMagnitude(rowMagnitude[row]);
        }
        return scaling;
    }

    double scaleFiniteBound(double value, double scale)
    {
        return std::isfinite(value) ? value / scale : value;
    }

    double scaleFiniteValue(double value, double scale)
    {
        return std::isfinite(value) ? value * scale : value;
    }

    HighsModel makeHighsModel(const NativeQpProblem& problem, const HighsScalingData& scaling)
    {
        HighsModel model;
        auto& linearProgram = model.lp_;
        linearProgram.num_col_ = static_cast<HighsInt>(problem.variableCount);
        linearProgram.num_row_ = static_cast<HighsInt>(problem.constraintCount);
        linearProgram.sense_ = ObjSense::kMinimize;
        linearProgram.offset_ = problem.objectiveConstant;
        linearProgram.col_cost_.resize(problem.variableCount);
        linearProgram.col_lower_.resize(problem.variableCount);
        linearProgram.col_upper_.resize(problem.variableCount);
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            const double variableScale = scaling.mVariableScale[column];
            linearProgram.col_cost_[column] = problem.linearObjective[column] * variableScale;
            linearProgram.col_lower_[column] =
                scaleFiniteBound(problem.variableLowerBounds[column], variableScale);
            linearProgram.col_upper_[column] =
                scaleFiniteBound(problem.variableUpperBounds[column], variableScale);
        }
        linearProgram.row_lower_.resize(problem.constraintCount);
        linearProgram.row_upper_.resize(problem.constraintCount);
        linearProgram.col_names_ = problem.variableNames;
        linearProgram.row_names_ = problem.constraintNames;

        // HiGHS stores A column-wise. The canonical snapshot is compressed by row
        // so this is one solver-boundary conversion, with no dense intermediate.
        auto& matrix = linearProgram.a_matrix_;
        matrix.format_ = MatrixFormat::kColwise;
        matrix.num_col_ = linearProgram.num_col_;
        matrix.num_row_ = linearProgram.num_row_;
        matrix.start_.clear();
        matrix.p_end_.clear();
        matrix.index_.clear();
        matrix.value_.clear();
        matrix.start_.assign(problem.variableCount + 1U, 0);
        for (std::size_t entry = 0; entry < problem.constraintMatrix.values.size(); ++entry) {
            const auto column = problem.constraintMatrix.columnIndices[entry];
            ++matrix.start_[column + 1U];
        }
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            matrix.start_[column + 1U] += matrix.start_[column];
        }
        matrix.index_.resize(problem.constraintMatrix.values.size());
        matrix.value_.resize(problem.constraintMatrix.values.size());
        auto nextEntry = matrix.start_;
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            for (std::size_t entry = problem.constraintMatrix.rowStarts[row];
                 entry < problem.constraintMatrix.rowStarts[row + 1U];
                 ++entry) {
                const auto column = problem.constraintMatrix.columnIndices[entry];
                const auto target = nextEntry[column]++;
                matrix.index_[target] = static_cast<HighsInt>(row);
                matrix.value_[target] = problem.constraintMatrix.values[entry] *
                    scaling.mRowScale[row] * scaling.mVariableScale[column];
            }
        }

        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            linearProgram.row_lower_[row] = scaleFiniteValue(
                problem.solverConstraintLowerBound(row), scaling.mRowScale[row]);
            linearProgram.row_upper_[row] = scaleFiniteValue(
                problem.solverConstraintUpperBound(row), scaling.mRowScale[row]);
        }

        bool hasQuadraticTerm = false;
        for (const double coefficient : problem.quadraticObjective) {
            hasQuadraticTerm = hasQuadraticTerm || (coefficient != 0.0);
        }
        if (hasQuadraticTerm) {
            // HiGHS uses 1/2*x'Q*x, while NativeQpProblem uses q[i]*x[i]^2.
            // Therefore the diagonal Hessian entries are 2*q[i]. Include every
            // diagonal, including zeros, as required by the triangular format.
            auto& hessian = model.hessian_;
            hessian.dim_ = static_cast<HighsInt>(problem.variableCount);
            hessian.format_ = HessianFormat::kTriangular;
            hessian.start_.clear();
            hessian.index_.clear();
            hessian.value_.clear();
            hessian.start_.reserve(problem.variableCount + 1U);
            for (std::size_t column = 0; column < problem.variableCount; ++column) {
                hessian.start_.push_back(static_cast<HighsInt>(hessian.value_.size()));
                hessian.index_.push_back(static_cast<HighsInt>(column));
                const double variableScale = scaling.mVariableScale[column];
                hessian.value_.push_back(2.0 * problem.quadraticObjective[column] * variableScale *
                                         variableScale);
            }
            hessian.start_.push_back(static_cast<HighsInt>(hessian.value_.size()));
        }
        return model;
    }

}  // namespace

HighsOptimizer::HighsOptimizer(std::string_view optName): NativeOptimizer(optName) {}

HighsOptimizer::HighsOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode):
    NativeOptimizer(gdo, oMode)
{
}

int HighsOptimizer::solve(double tStop, double& tReturn)
{
    tReturn = tStop;
    mScalingApplied = false;
    if (prepareProblemData(tStop) != FUNCTION_EXECUTION_SUCCESS) {
        recordSolveFailure(lastErrorString);
        return FUNCTION_EXECUTION_FAILURE;
    }

    NativeSolveResult result;
    Highs highs;
    const double tolerance = (std::isfinite(rtol) && (rtol > 0.0)) ? (std::max)(rtol, 1e-10) : 1e-8;
    if (!setHighsOptions(highs, tolerance, max_iterations)) {
        result.status = NativeSolveStatus::NUMERICAL_FAILURE;
        result.message = "could not configure HiGHS options";
        return acceptSolution(tStop, std::move(result));
    }

    // HiGHS' active-set QP solver has a conservative default nullspace limit
    // of about 4000 variables.  Large DC-OPF models contain thousands of bus
    // angle variables with no objective curvature, so the default can report
    // a solver error before it reaches a valid solution even though the model
    // is convex and feasible.  Size this guard from the actual model while
    // retaining HiGHS' own limit for smaller problems.
    const auto qpNullspaceLimit =
        static_cast<HighsInt>((std::max)(problem().variableCount, static_cast<std::size_t>(4000)));
    if (highs.setOptionValue("qp_nullspace_limit", qpNullspaceLimit) == HighsStatus::kError) {
        result.status = NativeSolveStatus::NUMERICAL_FAILURE;
        result.message = "could not configure the HiGHS QP nullspace limit";
        return acceptSolution(tStop, std::move(result));
    }

    const HighsScalingData scaling = makeScalingData(problem(), scalingRequestedForProblem());
    mScalingApplied = scaling.mEnabled;
    auto model = makeHighsModel(problem(), scaling);
    const auto passStatus = highs.passModel(std::move(model));
    if (passStatus == HighsStatus::kError) {
        result.status = NativeSolveStatus::NUMERICAL_FAILURE;
        result.message = "could not load the solver-neutral model into HiGHS";
        return acceptSolution(tStop, std::move(result));
    }

    const auto runStatus = highs.run();
    const auto modelStatus = highs.getModelStatus();
    result.status = nativeStatus(modelStatus);
    result.message = "HiGHS model status: " + highs.modelStatusToString(modelStatus);
    const auto& info = highs.getInfo();
    if (modelStatus != HighsModelStatus::kOptimal) {
        result.message += " (qp_iterations=" + std::to_string(info.qp_iteration_count) +
            ", primal_status=" + std::to_string(info.primal_solution_status) +
            ", dual_status=" + std::to_string(info.dual_solution_status) +
            ", max_primal_infeasibility=" + std::to_string(info.max_primal_infeasibility) +
            ", max_dual_infeasibility=" + std::to_string(info.max_dual_infeasibility) + ")";
    }
    result.iterationCount = positiveIterationCount(info.simplex_iteration_count) +
        positiveIterationCount(info.ipm_iteration_count) +
        positiveIterationCount(info.qp_iteration_count) +
        positiveIterationCount(info.pdlp_iteration_count) +
        positiveIterationCount(info.crossover_iteration_count);
    result.maximumStationarity = info.max_dual_infeasibility;
    result.maximumComplementarity = info.max_complementarity_violation;
    if ((runStatus == HighsStatus::kError) && result.successful()) {
        result.status = NativeSolveStatus::NUMERICAL_FAILURE;
        result.message = "HiGHS returned an error after reporting an optimal model";
    }
    if (result.successful()) {
        const auto& scaledValues = highs.getSolution().col_value;
        if (scaledValues.size() != problem().variableCount) {
            result.status = NativeSolveStatus::NUMERICAL_FAILURE;
            result.message = "HiGHS returned a primal solution with the wrong dimension";
        } else {
            result.values.resize(scaledValues.size());
            for (std::size_t column = 0; column < scaledValues.size(); ++column) {
                result.values[column] = scaledValues[column] * scaling.mVariableScale[column];
            }
        }
        if (result.successful() &&
            !std::all_of(result.values.begin(), result.values.end(), [](double value) {
                return std::isfinite(value);
            })) {
            result.status = NativeSolveStatus::NUMERICAL_FAILURE;
            result.message = "HiGHS returned an invalid primal solution";
        } else {
            // Use the canonical expression for the value checked against the
            // GridDyn objective callback. This also removes any backend-only
            // reporting roundoff from the common acceptance test.
            result.objectiveValue = problem().objectiveValue(result.values);
        }
    }
    return acceptSolution(tStop, std::move(result));
}

void HighsOptimizer::set(std::string_view param, std::string_view val)
{
    if ((param == "scaling") || (param == "scaling_mode")) {
        mScalingMode = parseScalingMode(param, val);
        return;
    }
    NativeOptimizer::set(param, val);
}

void HighsOptimizer::set(std::string_view param, double val)
{
    if ((param == "scaling") || (param == "scaling_mode")) {
        if (!std::isfinite(val) || (val < 0.0) || (val > 2.0) || (std::floor(val) != val)) {
            throw InvalidParameterValue(param);
        }
        mScalingMode = static_cast<HighsScalingMode>(static_cast<int>(val));
        return;
    }
    if ((param == "scaling_threshold") || (param == "scaling_variable_threshold")) {
        if (!std::isfinite(val) || (val < 1.0) || (std::floor(val) != val) ||
            (val > static_cast<double>(kCountMax))) {
            throw InvalidParameterValue(param);
        }
        mScalingVariableThreshold = static_cast<count_t>(val);
        return;
    }
    NativeOptimizer::set(param, val);
}

bool HighsOptimizer::scalingRequestedForProblem() const noexcept
{
    switch (mScalingMode) {
        case HighsScalingMode::NO_SCALING:
            return false;
        case HighsScalingMode::SCALING:
            return true;
        case HighsScalingMode::AUTO:
            return mVariableCount >= mScalingVariableThreshold;
    }
    return false;
}

double HighsOptimizer::get(std::string_view param) const
{
    if ((param == "highs_solver") || (param == "highs")) {
        return 1.0;
    }
    if ((param == "native_solver") || (param == "native")) {
        return 0.0;
    }
    if ((param == "scaling") || (param == "scaling_mode")) {
        return static_cast<double>(mScalingMode);
    }
    if ((param == "scaling_threshold") || (param == "scaling_variable_threshold")) {
        return static_cast<double>(mScalingVariableThreshold);
    }
    if ((param == "scaling_requested") || (param == "scaling_selected")) {
        return scalingRequestedForProblem() ? 1.0 : 0.0;
    }
    if (param == "scaling_applied") {
        return mScalingApplied ? 1.0 : 0.0;
    }
    return NativeOptimizer::get(param);
}

}  // namespace griddyn
