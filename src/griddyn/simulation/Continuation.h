/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "units/units.hpp"
#include "utilities/indexTypes.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace griddyn {
class CoreObject;
class GridDynSimulation;
class Recorder;

/** A loading target for the constant-power portion of a named load. Omitted components stay fixed.
 */
struct ContinuationLoadTarget {
    std::string load;  //!< Load name or object path.
    std::optional<double> pMW;  //!< Active-power target in MW at lambda = 1.
    std::optional<double> qMvar;  //!< Reactive-power target in Mvar at lambda = 1.
};

/** Absolute active-power target for a named non-slack generator. */
struct ContinuationGeneratorTarget {
    std::string generator;  //!< Generator name or object path.
    double pMW;  //!< Active-power target in MW at lambda = 1.
};

/** Loading direction, stopping criterion and output selection for an AC continuation study. */
struct ContinuationRequest {
    std::optional<double> loadScale;  //!< Multiplier for eligible constant-power loads at lambda = 1.
    std::vector<ContinuationLoadTarget> loadTargets;  //!< Per-load targets, exclusive with loadScale.
    std::vector<ContinuationGeneratorTarget> generatorTargets;  //!< Non-slack dispatch targets.
    std::optional<double> stopLambda;  //!< Omit to stop at the first loading limit.
    std::vector<index_t> monitorBuses;  //!< Bus user IDs in the requested output order.
    bool enforceQLimits =
        false;  //!< Enforce generator Q limits on local and remote voltage groups.
};

/** Reason the continuation trace stopped. */
enum class ContinuationStatus {
    TARGET_REACHED,  //!< The requested lambda was reached on the traced branch.
    NOSE_FOUND,  //!< An untargeted trace found a smooth loading limit.
    NOSE_BEFORE_TARGET,  //!< A smooth loading limit was reached ahead of the requested lambda.
    Q_LIMIT_TURN,  //!< A binding generator reactive-power limit caused a turn.
    BASE_SOLVE_FAILED,  //!< Power-flow initialization or the base solve failed.
    NUMERICAL_FAILURE,  //!< The next continuation point could not be resolved.
    STEP_LIMIT  //!< The continuation step budget was exhausted.
};

/** One accepted point on the continuation branch. */
struct ContinuationPoint {
    double lambda;  //!< Loading coordinate, with zero at the base case.
    double residualNorm;  //!< Maximum absolute power-flow mismatch at this point.
    std::vector<double> voltages;  //!< Per unit, in monitorBuses order.
    std::vector<double> angles;  //!< Radians, in monitorBuses order.
};

/** Generator reactive-power bound encountered while tracing the branch. */
struct ContinuationQLimitEvent {
    index_t busId;  //!< Generator terminal bus user ID.
    std::string generator;  //!< Name of the generator that reached its bound.
    double lambda;  //!< Loading coordinate of the limit event.
    double qMvar;  //!< Binding reactive-power bound in Mvar.
    bool upper;  //!< True for the upper bound and false for the lower bound.
    index_t regulatedBusId;  //!< User ID of the local or remote voltage-controlled bus.
};

/** Accepted points and termination information, including partial traces on failure. */
struct ContinuationResult {
    ContinuationStatus status = ContinuationStatus::BASE_SOLVE_FAILED;  //!< Termination reason.
    std::vector<index_t> monitorBuses;  //!< User IDs defining voltage and angle vector order.
    std::vector<ContinuationPoint> points;  //!< Accepted points in trace order.
    std::optional<ContinuationPoint> nose;  //!< Refined smooth loading limit, when found.
    std::vector<ContinuationQLimitEvent> qLimitEvents;  //!< Reactive limit events in trace order.
    std::optional<double> targetLambda;  //!< Explicit stopping coordinate, when requested.

    /** Whether the requested target or loading limit was reached. */
    [[nodiscard]] bool successful() const noexcept
    {
        return status == ContinuationStatus::TARGET_REACHED ||
            status == ContinuationStatus::NOSE_FOUND ||
            (status == ContinuationStatus::Q_LIMIT_TURN && !targetLambda);
    }

    /** Confirmed loading limit, either a smooth nose or a turn at a Q limit. */
    [[nodiscard]] std::optional<double> limitingLambda() const noexcept
    {
        if (nose) {
            return nose->lambda;
        }
        if (status == ContinuationStatus::Q_LIMIT_TURN && !points.empty()) {
            return points.back().lambda;
        }
        return std::nullopt;
    }
};

class parameterSequence {
  public:
    CoreObject* m_target;
    std::string m_field;
    double m_startValue;
    double m_stepSize;
    units::unit m_unitType = units::defunit;

  protected:
    int m_currentStep = 0;
    CoreObject* m_obj = nullptr;

  public:
    parameterSequence();
    bool setTarget(CoreObject* gdo, const std::string var = "");
    void setValue(double start, double step) const;
    void step();
    void step(int stepNumber);
};

class ContinuationSequence {
  public:
    static int contCount;  // static variable counting the number of created lines
    std::string name;
    int id;
    std::vector<std::shared_ptr<parameterSequence>>
        SequenceList;  //!< vector storing sequence objects
    std::vector<std::shared_ptr<Recorder>> recordList;  //!< vector storing recorder objects
  protected:
    int m_currentStep = 0;

  public:
    ContinuationSequence();
    void step();
    void step(int StepNumber);
};

}  // namespace griddyn
