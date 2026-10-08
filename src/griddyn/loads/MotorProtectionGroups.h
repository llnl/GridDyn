/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreDefinitions.hpp"
#include <array>
#include <cstddef>

namespace griddyn::loads {

/** Two cumulative undervoltage trip and delayed reconnection groups.
 *
 * The four root functions per group detect trip-threshold crossings, trip-delay
 * expiry, reconnection-threshold crossings, and reconnection-delay expiry.  A
 * group fraction is cumulative, as in the WECC CMPLDW three-phase motor model.
 */
class MotorProtectionGroups {
  public:
    static constexpr std::size_t stageCount = 2;
    static constexpr std::size_t rootsPerStage = 4;
    static constexpr std::size_t rootCount = stageCount * rootsPerStage;

    void setStage(std::size_t stage,
                  double tripVoltage,
                  double tripDelay,
                  double tripFraction,
                  double reconnectVoltage,
                  double reconnectDelay);

    void initialize(CoreTime time, double voltage);
    void rootTest(CoreTime time, double voltage, double roots[rootCount]) const;
    bool rootTrigger(std::size_t root, CoreTime time, double voltage);

    double onlineFraction() const;
    bool tripped(std::size_t stage) const;

  private:
    struct Stage {
        double tripVoltage = 2.0;
        double tripDelay = 0.0;
        double tripFraction = 0.0;
        double reconnectVoltage = 2.0;
        double reconnectDelay = 0.0;
        CoreTime tripStart = timeZero;
        CoreTime reconnectStart = timeZero;
        bool tripTimerActive = false;
        bool reconnectTimerActive = false;
        bool isTripped = false;
    };

    std::array<Stage, stageCount> stages{};
};

}  // namespace griddyn::loads
