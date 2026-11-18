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
    std::string load;
    std::optional<double> pMW;
    std::optional<double> qMvar;
};

/** Absolute active-power target for a named non-slack generator. */
struct ContinuationGeneratorTarget {
    std::string generator;
    double pMW;
};

struct ContinuationRequest {
    std::optional<double> loadScale;
    std::vector<ContinuationLoadTarget> loadTargets;
    std::vector<ContinuationGeneratorTarget> generatorTargets;
    std::optional<double> stopLambda;  //!< Omit to stop at the first loading limit.
    std::vector<index_t> monitorBuses;  //!< Bus user IDs in the requested output order.
    bool enforceQLimits =
        false;  //!< Enforce generator Q limits on local and remote voltage groups.
};

enum class ContinuationStatus {
    TARGET_REACHED,
    NOSE_FOUND,
    NOSE_BEFORE_TARGET,
    Q_LIMIT_TURN,
    BASE_SOLVE_FAILED,
    NUMERICAL_FAILURE,
    STEP_LIMIT
};

struct ContinuationPoint {
    double lambda;
    double residualNorm;
    std::vector<double> voltages;  //!< Per unit, in monitorBuses order.
    std::vector<double> angles;  //!< Radians, in monitorBuses order.
};

struct ContinuationQLimitEvent {
    index_t busId;  //!< Generator terminal bus user ID.
    std::string generator;
    double lambda;
    double qMvar;
    bool upper;
    index_t regulatedBusId;
};

struct ContinuationResult {
    ContinuationStatus status = ContinuationStatus::BASE_SOLVE_FAILED;
    std::vector<index_t> monitorBuses;
    std::vector<ContinuationPoint> points;
    std::optional<ContinuationPoint> nose;
    std::vector<ContinuationQLimitEvent> qLimitEvents;
    std::optional<double> targetLambda;

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
