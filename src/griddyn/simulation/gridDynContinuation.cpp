/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../GridDynSimulation.h"
#include "../Load.h"
#include "../events/EventQueue.h"
#include "../generators/DynamicGenerator.h"
#include "../loads/ZipLoad.h"
#include "../primary/AcBus.h"
#include "core/CoreExceptions.h"
#include "utilities/MatrixDataSparse.hpp"
#include <nvector/nvector_serial.h>
#include <sunlinsol/sunlinsol_dense.h>
#include <sunmatrix/sunmatrix_dense.h>
#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#    include <sunmatrix/sunmatrix_sparse.h>
#endif
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

namespace griddyn {
namespace {

    using Vector = std::vector<double>;

    constexpr double residualTolerance = 1e-8;
    constexpr double noseArcTolerance = 1e-5;
    constexpr double reactiveLimitTolerance = 1e-8;
    constexpr double reactiveEventTolerance = 1e-7;
    constexpr double minimumStep = 1e-5;
    constexpr double maximumStep = 0.15;
    constexpr int maximumSteps = 500;
    constexpr int maximumIterations = 12;

    double maxAbs(const Vector& values)
    {
        double norm = 0.0;
        for (const auto value : values) {
            if (!std::isfinite(value)) {
                return std::numeric_limits<double>::infinity();
            }
            norm = (std::max)(norm, std::abs(value));
        }
        return norm;
    }

    double dot(const Vector& first, const Vector& second)
    {
        return std::inner_product(first.begin(), first.end(), second.begin(), 0.0);
    }

    /** Owns the matrix and direct factorization of the whole bordered system. */
    class BorderedSolver {
      public:
        BorderedSolver(index_t size, [[maybe_unused]] bool dense): mDimension(size)
        {
#ifdef GRIDDYN_ENABLE_KLU
            mDense = dense;
#endif
            if (SUNContext_Create(SUN_COMM_NULL, &mContext) == 0) {
                mRhs = N_VNew_Serial(mDimension, mContext);
                mSolution = N_VNew_Serial(mDimension, mContext);
            }
        }

        ~BorderedSolver()
        {
            if (mSolver != nullptr) {
                SUNLinSolFree(mSolver);
            }
            if (mMatrix != nullptr) {
                SUNMatDestroy(mMatrix);
            }
            if (mSolution != nullptr) {
                N_VDestroy(mSolution);
            }
            if (mRhs != nullptr) {
                N_VDestroy(mRhs);
            }
            if (mContext != nullptr) {
                SUNContext_Free(&mContext);
            }
        }

        BorderedSolver(const BorderedSolver&) = delete;
        BorderedSolver& operator=(const BorderedSolver&) = delete;

        bool solve(MatrixDataSparse<>& entries, Vector& values)
        {
            if (mRhs == nullptr || mSolution == nullptr || !assemble(entries)) {
                return false;
            }
            std::copy(values.begin(), values.end(), N_VGetArrayPointer(mRhs));
            if (SUNLinSolSetup(mSolver, mMatrix) != 0 ||
                SUNLinSolSolve(mSolver, mMatrix, mSolution, mRhs, 0.0) != 0) {
                return false;
            }
            std::copy_n(N_VGetArrayPointer(mSolution), mDimension, values.begin());
            return std::isfinite(maxAbs(values));
        }

      private:
        bool assemble(MatrixDataSparse<>& entries)
        {
#ifdef GRIDDYN_ENABLE_KLU
            if (!mDense) {
                return assembleSparse(entries);
            }
#endif
            if (mMatrix == nullptr) {
                mMatrix = SUNDenseMatrix(mDimension, mDimension, mContext);
                if (mMatrix == nullptr) {
                    return false;
                }
                mSolver = SUNLinSol_Dense(mSolution, mMatrix, mContext);
                if (mSolver == nullptr || SUNLinSolInitialize(mSolver) != 0) {
                    return false;
                }
            }
            if (mSolver == nullptr || SUNMatZero(mMatrix) != 0) {
                return false;
            }
            for (const auto& entry : entries) {
                SM_ELEMENT_D(mMatrix, entry.row, entry.col) += entry.data;
            }
            return true;
        }

#ifdef GRIDDYN_ENABLE_KLU
        bool assembleSparse(MatrixDataSparse<>& entries)
        {
            entries.sortIndex(SparseOrdering::COLUMN_ORDERED);
            entries.compact();
            if (mMatrix == nullptr) {
                mMatrix =
                    SUNSparseMatrix(mDimension, mDimension, entries.size(), CSC_MAT, mContext);
                if (mMatrix == nullptr) {
                    return false;
                }
                mSolver = SUNLinSol_KLU(mSolution, mMatrix, mContext);
                if (mSolver == nullptr || SUNLinSolInitialize(mSolver) != 0) {
                    return false;
                }
                SUNLinSol_KLUGetCommon(mSolver)->halt_if_singular = 1;
            } else {
                if (mSolver == nullptr ||
                    (entries.size() > SM_NNZ_S(mMatrix) &&
                     SUNSparseMatrix_Reallocate(mMatrix, entries.size()) != 0) ||
                    SUNLinSol_KLUReInit(mSolver, mMatrix, entries.size(), SUNKLU_REINIT_PARTIAL) !=
                        0) {
                    return false;
                }
            }
            index_t position = 0;
            index_t column = 0;
            SM_INDEXPTRS_S(mMatrix)[0] = 0;
            for (const auto& entry : entries) {
                while (column < entry.col) {
                    SM_INDEXPTRS_S(mMatrix)[++column] = position;
                }
                SM_INDEXVALS_S(mMatrix)[position] = entry.row;
                SM_DATA_S(mMatrix)[position++] = entry.data;
            }
            while (column < mDimension) {
                SM_INDEXPTRS_S(mMatrix)[++column] = position;
            }
            return true;
        }

        bool mDense = false;
#endif
        index_t mDimension;
        SUNContext mContext = nullptr;
        SUNMatrix mMatrix = nullptr;
        SUNLinearSolver mSolver = nullptr;
        N_Vector mRhs = nullptr;
        N_Vector mSolution = nullptr;
    };

    bool isScalable(const GridLoad& load)
    {
        return load.isConnected() && !load.isFixedShunt() &&
            (typeid(load) == typeid(GridLoad) || typeid(load) == typeid(ZipLoad));
    }

    template<class Device, class GetDevice>
    Device* findTarget(GridDynSimulation& sim,
                       const std::vector<GridBus*>& buses,
                       const std::string& name,
                       GetDevice getDevice)
    {
        if (name.contains(':')) {
            return dynamic_cast<Device*>(sim.find(name));
        }
        Device* matched = nullptr;
        for (const auto* bus : buses) {
            index_t deviceIndex = 0;
            for (auto* device = (bus->*getDevice)(deviceIndex); device != nullptr;
                 device = (bus->*getDevice)(++deviceIndex)) {
                if (device->getName() == name) {
                    if (matched != nullptr) {
                        throw InvalidParameterValue("ambiguous continuation target " + name);
                    }
                    matched = device;
                }
            }
        }
        return matched;
    }

    struct LoadingChange {
        CoreObject* mObject;
        std::string_view mField;
        units::unit mUnit;
        double mBase;
        double mDelta;

        void apply(double lambda) const { mObject->set(mField, mBase + (lambda * mDelta), mUnit); }
    };

    struct ReactiveControl {
        AcBus* mRegulatedBus;
        Generator* mGenerator;
        double mMinimum;
        double mMaximum;
        std::optional<bool> mHeldAtUpper;
        double mBindingLambda = 0.0;
        index_t mOffset = kNullLocation;

        [[nodiscard]] double required(const Vector& state) const { return -state[mOffset]; }
    };

    struct ReactiveLimit {
        std::size_t mIndex;
        bool mUpper;
    };

    ReactiveControl makeReactiveControl(AcBus& bus, GridSecondary& controller)
    {
        auto* generator = dynamic_cast<Generator*>(&controller);
        if (generator == nullptr || generator->checkFlag(Generator::USE_CAPABILITY_CURVE) ||
            (typeid(*generator) != typeid(Generator) &&
             typeid(*generator) != typeid(DynamicGenerator))) {
            throw InvalidParameterValue(
                "continuation Q limits require standard generators with fixed bounds at bus " +
                bus.getName());
        }
        const double minimum = generator->getQmin();
        const double maximum = generator->getQmax();
        const double fraction = generator->get("vcontrolfrac");
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum ||
            !std::isfinite(fraction) || fraction < 0.0) {
            throw InvalidParameterValue(
                "invalid continuation reactive limits or participation at bus " + bus.getName());
        }
        return {.mRegulatedBus = &bus,
                .mGenerator = generator,
                .mMinimum = minimum,
                .mMaximum = maximum,
                .mHeldAtUpper = std::nullopt};
    }

    std::vector<ReactiveControl> reactiveControls(const std::vector<GridBus*>& buses)
    {
        std::vector<ReactiveControl> controls;
        for (auto* bus : buses) {
            if (!bus->isEnabled() || !bus->isConnected() ||
                bus->getType() == GridBus::BusType::SLK) {
                continue;
            }
            auto& regulated = static_cast<AcBus&>(*bus);
            const auto devices = regulated.voltageControlObjects();
            if (bus->getType() != GridBus::BusType::PV &&
                !bus->checkFlag(INDIRECT_VOLTAGE_CONTROL) &&
                std::ranges::none_of(devices, [](const auto* device) {
                    return device->checkFlag(INDIRECT_VOLTAGE_CONTROL);
                })) {
                continue;
            }
            if (std::cmp_not_equal(regulated.voltageControlCount(), devices.size()) ||
                regulated.checkFlag(AcBus::SLAVE_BUS) || regulated.checkFlag(AcBus::MASTER_BUS)) {
                throw InvalidParameterValue(
                    "continuation Q limits require generator-only voltage control at bus " +
                    regulated.getName());
            }
            const bool useUniformWeights = std::ranges::none_of(devices, [](const auto* device) {
                return device->isConnected() && device->isEnabled() &&
                    device->get("vcontrolfrac") > 0.0;
            });
            for (auto* device : devices) {
                if (!device->isEnabled() || !device->isConnected()) {
                    continue;
                }
                auto control = makeReactiveControl(regulated, *device);
                auto& generator = *control.mGenerator;
                if (!generator.checkFlag(INDIRECT_VOLTAGE_CONTROL)) {
                    generator.configureIndirectVoltageControl(&regulated);
                }
                if (useUniformWeights) {
                    generator.set("vcontrolfrac", 1.0);
                }
                controls.push_back(control);
            }
            if (bus->getType() == GridBus::BusType::PV && !devices.empty()) {
                // The generator equations now hold voltage; the bus enforces reactive balance.
                bus->set("type", "pq");
            }
        }
        return controls;
    }

    class ContinuationStudy {
      public:
        ContinuationStudy(GridDynSimulation& simulation,
                          const SolverMode& mode,
                          index_t size,
                          std::vector<LoadingChange> changes,
                          std::vector<const GridBus*> monitors,
                          std::vector<ReactiveControl> controls):
            mSim(simulation), mSolverMode(mode), mTime(simulation.getSimulationTime()),
            mStateCount(size), mLoading(std::move(changes)), mMonitorBuses(std::move(monitors)),
            mReactiveControls(std::move(controls)),
            mSolver(size + 1, simulation.isFlagSet(DENSE_SOLVER)), mLower(size), mUpper(size),
            mResidualWork(size)
        {
        }

        double residual(const Vector& state)
        {
            if (!std::isfinite(maxAbs(state)) || !evaluate(state, state.back(), mResidualWork)) {
                return std::numeric_limits<double>::infinity();
            }
            return maxAbs(mResidualWork);
        }

        bool tangent(const Vector& state, const Vector& previous, Vector& direction)
        {
            direction.assign(mStateCount + 1, 0.0);
            direction.back() = 1.0;
            if (!solve(state, previous, direction)) {
                return false;
            }
            const double length = std::sqrt(dot(direction, direction));
            if (!std::isfinite(length) || length == 0.0) {
                return false;
            }
            for (auto& value : direction) {
                value /= length;
            }
            return true;
        }

        int correct(Vector& state, const Vector& anchor, const Vector& normal)
        {
            Vector correction(mStateCount + 1);
            for (int iteration = 0; iteration <= maximumIterations; ++iteration) {
                const double norm = residual(state);
                const double constraint = dot(normal, state) - dot(normal, anchor);
                if (!std::isfinite(norm) || !std::isfinite(constraint)) {
                    break;
                }
                if (norm <= residualTolerance && std::abs(constraint) <= residualTolerance) {
                    return iteration;
                }
                if (iteration == maximumIterations) {
                    break;
                }
                std::ranges::transform(mResidualWork, correction.begin(), std::negate<double>{});
                correction.back() = -constraint;
                if (!solve(state, normal, correction)) {
                    break;
                }
                std::ranges::transform(state, correction, state.begin(), std::plus<double>{});
            }
            return -1;
        }

        ContinuationPoint point(const Vector& state)
        {
            ContinuationPoint sample{.lambda = state.back(),
                                     .residualNorm = residual(state),
                                     .voltages = {},
                                     .angles = {}};
            for (const auto* bus : mMonitorBuses) {
                sample.voltages.push_back(bus->getVoltage(state.data(), mSolverMode));
                sample.angles.push_back(bus->getAngle(state.data(), mSolverMode));
            }
            return sample;
        }

        bool hasEffectiveLoading(const Vector& state)
        {
            if (!evaluate(state, 0.0, mLower) || !evaluate(state, 1.0, mUpper)) {
                return false;
            }
            std::ranges::transform(mUpper, mLower, mResidualWork.begin(), std::minus<double>{});
            const double effect = maxAbs(mResidualWork);
            return std::isfinite(effect) && effect > residualTolerance;
        }

        [[nodiscard]] std::optional<ReactiveLimit> violatedLimit(const Vector& state) const
        {
            for (std::size_t index = 0; index < mReactiveControls.size(); ++index) {
                const auto& control = mReactiveControls[index];
                if (!control.mHeldAtUpper) {
                    const double required = control.required(state);
                    if (required > control.mMaximum + reactiveLimitTolerance) {
                        return ReactiveLimit{.mIndex = index, .mUpper = true};
                    }
                    if (required < control.mMinimum - reactiveLimitTolerance) {
                        return ReactiveLimit{.mIndex = index, .mUpper = false};
                    }
                }
            }
            return std::nullopt;
        }

        ContinuationQLimitEvent bindLimit(const ReactiveLimit& limit, double lambda)
        {
            auto& control = mReactiveControls[limit.mIndex];
            const double bound = limit.mUpper ? control.mMaximum : control.mMinimum;
            const double qMvar =
                control.mGenerator->get(limit.mUpper ? "qmax" : "qmin", units::MVAR);
            control.mGenerator->fixPowerFlowReactivePower(bound);
            control.mHeldAtUpper = limit.mUpper;
            control.mBindingLambda = lambda;
            return {.busId = control.mGenerator->getParent()->getUserID(),
                    .generator = control.mGenerator->getName(),
                    .lambda = lambda,
                    .qMvar = qMvar,
                    .upper = limit.mUpper,
                    .regulatedBusId = control.mRegulatedBus->getUserID()};
        }

        [[nodiscard]] bool pointsIntoVoltageControlRegion(double lambda,
                                                          const Vector& direction) const
        {
            return std::ranges::any_of(mReactiveControls, [&](const auto& control) {
                if (!control.mHeldAtUpper || control.mBindingLambda != lambda ||
                    control.mRegulatedBus->indirectVoltageControlReference() != nullptr) {
                    return false;
                }
                const double slope =
                    control.mRegulatedBus->getVoltage(direction.data(), mSolverMode);
                // The admissible saturated branch is below the setpoint at Qmax and
                // above it at Qmin. At a corner the unconstrained tangent can point
                // back into the region where the generator should still regulate.
                return (*control.mHeldAtUpper ? slope : -slope) > reactiveLimitTolerance;
            });
        }

      private:
        bool evaluate(const Vector& state, double lambda, Vector& mismatch)
        {
            for (const auto& change : mLoading) {
                change.apply(lambda);
            }
            std::ranges::fill(mismatch, 0.0);
            return mSim.residualFunction(mTime,
                                         state.data(),
                                         nullptr,
                                         mismatch.data(),
                                         mSolverMode) == FUNCTION_EXECUTION_SUCCESS;
        }

        bool solve(const Vector& state, const Vector& normal, Vector& rhs)
        {
            // Refresh the residual at the actual iterate before evaluating its Jacobian.
            const double lambda = state.back();
            const double increment = 1e-4 * (std::max)(1.0, std::abs(lambda));
            if (!evaluate(state, lambda - increment, mLower) ||
                !evaluate(state, lambda + increment, mUpper) ||
                !evaluate(state, lambda, mResidualWork) ||
                mSim.jacobianFunction(mTime, state.data(), nullptr, mJacobian, 0.0, mSolverMode) !=
                    FUNCTION_EXECUTION_SUCCESS) {
                return false;
            }
            for (index_t row = 0; row < mStateCount; ++row) {
                mJacobian.assign(row, mStateCount, (mUpper[row] - mLower[row]) / (2.0 * increment));
            }
            for (index_t column = 0; column <= mStateCount; ++column) {
                mJacobian.assign(mStateCount, column, normal[column]);
            }
            if (!std::ranges::all_of(mJacobian, [this](const auto& entry) {
                    return entry.row >= 0 && entry.row <= mStateCount && entry.col >= 0 &&
                        entry.col <= mStateCount && std::isfinite(entry.data);
                })) {
                return false;
            }
            return mSolver.solve(mJacobian, rhs);
        }

        GridDynSimulation& mSim;
        const SolverMode& mSolverMode;
        CoreTime mTime;
        index_t mStateCount;
        std::vector<LoadingChange> mLoading;
        std::vector<const GridBus*> mMonitorBuses;
        std::vector<ReactiveControl> mReactiveControls;
        BorderedSolver mSolver;
        Vector mLower;
        Vector mUpper;
        Vector mResidualWork;
        MatrixDataSparse<> mJacobian;
    };

    std::optional<Vector>
        refineNose(ContinuationStudy& study, Vector left, Vector right, Vector leftTangent)
    {
        for (int iteration = 0; iteration < 40; ++iteration) {
            Vector chord(left.size());
            std::ranges::transform(right, left, chord.begin(), std::minus<double>{});
            const double distance = std::sqrt(dot(chord, chord));
            Vector middle(left.size());
            std::ranges::transform(left, right, middle.begin(), [](double lower, double upper) {
                return std::midpoint(lower, upper);
            });
            const auto anchor = middle;
            if (study.correct(middle, anchor, leftTangent) < 0) {
                return std::nullopt;
            }
            if (distance <= noseArcTolerance) {
                return middle;
            }
            Vector tangent;
            if (!study.tangent(middle, leftTangent, tangent)) {
                return std::nullopt;
            }
            if (tangent.back() >= 0.0) {
                left = std::move(middle);
                leftTangent = std::move(tangent);
            } else {
                right = std::move(middle);
            }
        }
        return std::nullopt;
    }

    bool correctAtLambda(ContinuationStudy& study, Vector& state)
    {
        Vector normal(state.size(), 0.0);
        normal.back() = 1.0;
        const auto anchor = state;
        if (study.correct(state, anchor, normal) < 0) {
            return false;
        }
        state.back() = anchor.back();
        return study.residual(state) <= residualTolerance;
    }

    std::optional<Vector> refineTarget(ContinuationStudy& study,
                                       const Vector& left,
                                       const Vector& right,
                                       double target)
    {
        Vector endpoint(left.size());
        const double fraction = (target - left.back()) / (right.back() - left.back());
        std::ranges::transform(left,
                               right,
                               endpoint.begin(),
                               [fraction](double lower, double upper) {
                                   return std::lerp(lower, upper, fraction);
                               });
        endpoint.back() = target;
        if (!correctAtLambda(study, endpoint)) {
            return std::nullopt;
        }
        return endpoint;
    }

    std::optional<Vector> refineReactiveEvent(ContinuationStudy& study,
                                              Vector left,
                                              Vector right,
                                              const Vector& tangent)
    {
        // Search in the existing step's arclength plane, before changing any bus equations.
        for (int iteration = 0; iteration < 40; ++iteration) {
            if (dot(tangent, right) - dot(tangent, left) <= reactiveEventTolerance) {
                return right;
            }
            Vector anchor(left.size());
            std::ranges::transform(left, right, anchor.begin(), [](double lower, double upper) {
                return std::midpoint(lower, upper);
            });
            auto middle = anchor;
            if (study.correct(middle, anchor, tangent) < 0) {
                return std::nullopt;
            }
            if (study.violatedLimit(middle)) {
                right = std::move(middle);
            } else {
                left = std::move(middle);
            }
        }
        return std::nullopt;
    }

    bool enforceAtPoint(ContinuationStudy& study, Vector& state, ContinuationResult& result)
    {
        std::vector<ContinuationQLimitEvent> events;
        while (const auto limit = study.violatedLimit(state)) {
            events.push_back(study.bindLimit(*limit, state.back()));
            if (!correctAtLambda(study, state)) {
                return false;
            }
        }
        std::ranges::move(events, std::back_inserter(result.qLimitEvents));
        return true;
    }

    void trace(ContinuationStudy& study, Vector state, ContinuationResult& result)
    {
        const auto& target = result.targetLambda;
        result.status = ContinuationStatus::NUMERICAL_FAILURE;
        if (!correctAtLambda(study, state) || !enforceAtPoint(study, state, result)) {
            result.status = ContinuationStatus::BASE_SOLVE_FAILED;
            return;
        }
        result.points.push_back(study.point(state));
        if (target && *target == 0.0) {
            result.status = ContinuationStatus::TARGET_REACHED;
            return;
        }
        if (!study.hasEffectiveLoading(state)) {
            throw InvalidParameterValue(
                "continuation target does not change the AC power-flow equations");
        }
        Vector normal(state.size(), 0.0);
        normal.back() = 1.0;
        Vector tangent;
        if (!study.tangent(state, normal, tangent)) {
            return;
        }
        double step = 0.05;
        for (int accepted = 0; accepted < maximumSteps;) {
            Vector predictor(state.size());
            std::ranges::transform(state,
                                   tangent,
                                   predictor.begin(),
                                   [step](double value, double slope) {
                                       return value + (step * slope);
                                   });
            auto candidate = predictor;
            Vector nextTangent;
            const int iterations = study.correct(candidate, predictor, tangent);
            if (iterations < 0 || !study.tangent(candidate, tangent, nextTangent)) {
                step *= 0.5;
                if (step < minimumStep) {
                    return;
                }
                continue;
            }

            std::optional<ContinuationStatus> endpointStatus;
            if (nextTangent.back() <= 0.0) {
                auto nose = refineNose(study, state, candidate, tangent);
                if (!nose) {
                    return;
                }
                candidate = std::move(*nose);
                endpointStatus = target ? ContinuationStatus::NOSE_BEFORE_TARGET :
                                          ContinuationStatus::NOSE_FOUND;
            }
            if (target && candidate.back() >= *target) {
                auto exact = refineTarget(study, state, candidate, *target);
                if (!exact) {
                    return;
                }
                candidate = std::move(*exact);
                endpointStatus = ContinuationStatus::TARGET_REACHED;
            }

            if (study.violatedLimit(candidate)) {
                auto crossing = refineReactiveEvent(study, state, candidate, tangent);
                if (!crossing || !enforceAtPoint(study, *crossing, result)) {
                    return;
                }
                result.points.push_back(study.point(*crossing));
                if (target && crossing->back() == *target) {
                    result.status = ContinuationStatus::TARGET_REACHED;
                    return;
                }
                if (!study.tangent(*crossing, tangent, nextTangent)) {
                    return;
                }
                if (nextTangent.back() <= 0.0 ||
                    study.pointsIntoVoltageControlRegion(crossing->back(), nextTangent)) {
                    result.status = ContinuationStatus::Q_LIMIT_TURN;
                    return;
                }
                state = std::move(*crossing);
                tangent = std::move(nextTangent);
                step = (std::min)(step, 0.05);
                ++accepted;
                continue;
            }
            if (endpointStatus) {
                result.status = *endpointStatus;
                result.points.push_back(study.point(candidate));
                if (*endpointStatus != ContinuationStatus::TARGET_REACHED) {
                    result.nose = result.points.back();
                }
                return;
            }
            result.points.push_back(study.point(candidate));
            state = std::move(candidate);
            tangent = std::move(nextTangent);
            ++accepted;
            step = iterations <= 3 ? (std::min)(maximumStep, step * 1.3) : step;
        }
        result.status = ContinuationStatus::STEP_LIMIT;
    }

}  // namespace

ContinuationResult GridDynSimulation::continuationPowerFlow(double loadScale) const
{
    ContinuationRequest request;
    request.loadScale = loadScale;
    return continuationPowerFlow(request);
}

ContinuationResult
    GridDynSimulation::continuationPowerFlow(const ContinuationRequest& request) const
{
    if (request.loadScale &&
        (!std::isfinite(*request.loadScale) || *request.loadScale < 0.0 ||
         !request.loadTargets.empty())) {
        throw InvalidParameterValue(
            "continuation load_scale must be finite, nonnegative, and used without load_targets");
    }
    if (request.stopLambda && (!std::isfinite(*request.stopLambda) || *request.stopLambda < 0.0)) {
        throw InvalidParameterValue("continuation stop lambda must be finite and nonnegative");
    }

    auto working = std::unique_ptr<GridDynSimulation>(static_cast<GridDynSimulation*>(clone()));
    auto& sim = *working;
    std::vector<GridBus*> buses;
    sim.getBusVector(buses);
    const auto referenceCount = std::ranges::count_if(buses, [](const auto* bus) {
        return bus->isEnabled() && bus->isConnected() &&
            (bus->getType() == GridBus::BusType::SLK || bus->getType() == GridBus::BusType::AFIX);
    });
    if (referenceCount != 1 || std::ranges::any_of(buses, [](const auto* bus) {
            return bus->isEnabled() && dynamic_cast<const AcBus*>(bus) == nullptr;
        })) {
        throw InvalidParameterValue("continuation requires an AC network with one angle reference");
    }
    std::vector<const GridBus*> monitors;
    for (const auto busId : request.monitorBuses) {
        const auto* bus = dynamic_cast<const GridBus*>(sim.findByUserID("bus", busId));
        if (bus == nullptr || !bus->isEnabled() ||
            std::ranges::find(monitors, bus) != monitors.end()) {
            throw InvalidParameterValue("invalid or duplicate continuation monitor bus " +
                                        std::to_string(busId));
        }
        monitors.push_back(bus);
    }

    sim.controlFlags.set(NO_POWERFLOW_ADJUSTMENTS);
    sim.controlFlags.set(NO_POWERFLOW_ERROR_RECOVERY);
    if (request.enforceQLimits) {
        // Start from free voltage controllers. The trace decides which limits bind at lambda 0.
        sim.controlFlags.set(IGNORE_REACTIVE_POWER_LIMITS);
    }
    sim.controlFlags.reset(POWER_ADJUST_ENABLED);
    sim.controlFlags.reset(SAVE_POWER_FLOW_DATA);
    sim.controlFlags.reset(SAVE_POWER_FLOW_INPUT_DATA);
    sim.EvQ = std::make_unique<EventQueue>();
    ContinuationResult result;
    result.monitorBuses = request.monitorBuses;
    result.targetLambda = request.stopLambda;
    std::vector<ReactiveControl> controls;
    if (sim.initializePowerFlow(sim.getSimulationTime(), [&] {
            if (request.enforceQLimits && isAC(*sim.defPowerFlowMode)) {
                controls = reactiveControls(buses);
            }
        }) != FUNCTION_EXECUTION_SUCCESS) {
        return result;
    }
    if (sim.networkCount != 1) {
        throw InvalidParameterValue("continuation requires one connected AC island");
    }
    const auto& mode = *sim.defPowerFlowMode;
    if (!isPowerFlow(mode) || !isAlgebraicOnly(mode) || !isAC(mode)) {
        throw InvalidParameterValue("continuation requires an algebraic AC power-flow mode");
    }
    if (sim.powerflow() != FUNCTION_EXECUTION_SUCCESS) {
        return result;
    }
    auto state = sim.getState(mode);
    if (state.empty()) {
        throw InvalidParameterValue("continuation requires a nonempty AC power-flow state");
    }
    const auto stateCount = static_cast<index_t>(state.size());
    for (auto& control : controls) {
        control.mOffset = control.mGenerator->getOffsets(mode).algOffset;
        if (control.mOffset < 0 || control.mOffset >= stateCount) {
            throw InvalidParameterValue("continuation has no Q state for generator " +
                                        control.mGenerator->getName());
        }
    }

    std::vector<LoadingChange> changes;
    const auto addLoad =
        [&changes](GridLoad& load, const char* field, units::unit unit, double target) {
            const double base = load.get(field, unit);
            changes.push_back({.mObject = &load,
                               .mField = field,
                               .mUnit = unit,
                               .mBase = base,
                               .mDelta = target - base});
        };
    if (request.loadScale) {
        for (const auto* bus : buses) {
            if (!bus->isEnabled()) {
                continue;
            }
            index_t loadIndex = 0;
            for (auto* load = bus->getLoad(loadIndex); load != nullptr;
                 load = bus->getLoad(++loadIndex)) {
                if (isScalable(*load)) {
                    addLoad(*load, "p", units::MW, load->get("p", units::MW) * *request.loadScale);
                    addLoad(*load,
                            "q",
                            units::MVAR,
                            load->get("q", units::MVAR) * *request.loadScale);
                }
            }
        }
    }
    std::unordered_set<const CoreObject*> targets;
    for (const auto& target : request.loadTargets) {
        auto* load = findTarget<GridLoad>(sim, buses, target.load, &GridBus::getLoad);
        if (load == nullptr || !isScalable(*load) || !targets.insert(load).second ||
            (!target.pMW && !target.qMvar) || !std::isfinite(target.pMW.value_or(0.0)) ||
            !std::isfinite(target.qMvar.value_or(0.0))) {
            throw InvalidParameterValue("invalid or duplicate continuation load target " +
                                        target.load);
        }
        if (target.pMW) {
            addLoad(*load, "p", units::MW, *target.pMW);
        }
        if (target.qMvar) {
            addLoad(*load, "q", units::MVAR, *target.qMvar);
        }
    }
    for (const auto& target : request.generatorTargets) {
        auto* generator = findTarget<Generator>(sim, buses, target.generator, &GridBus::getGen);
        const auto* bus =
            generator == nullptr ? nullptr : dynamic_cast<const GridBus*>(generator->getParent());
        if (bus == nullptr || !generator->isConnected() ||
            bus->getType() == GridBus::BusType::SLK || bus->getType() == GridBus::BusType::AFIX ||
            !targets.insert(generator).second || !std::isfinite(target.pMW)) {
            throw InvalidParameterValue("invalid or duplicate continuation generator target " +
                                        target.generator);
        }
        const double baseReal = -generator->get("p", units::MW);
        changes.push_back({.mObject = generator,
                           .mField = "p",
                           .mUnit = units::MW,
                           .mBase = baseReal,
                           .mDelta = target.pMW - baseReal});
    }
    if (changes.empty() || !std::ranges::all_of(changes, [](const auto& change) {
            return std::isfinite(change.mBase) && std::isfinite(change.mDelta);
        })) {
        throw InvalidParameterValue("continuation requires finite loading targets");
    }
    state.push_back(0.0);
    ContinuationStudy study(
        sim, mode, stateCount, std::move(changes), std::move(monitors), std::move(controls));
    trace(study, std::move(state), result);
    return result;
}

}  // namespace griddyn
