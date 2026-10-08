/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "MotorProtectionGroups.h"

#include "core/CoreExceptions.h"
#include <algorithm>
#include <cmath>

namespace griddyn::loads {

void MotorProtectionGroups::setStage(std::size_t stage,
                                     double tripVoltage,
                                     double tripDelay,
                                     double tripFraction,
                                     double reconnectVoltage,
                                     double reconnectDelay)
{
    if (stage >= stageCount) {
        throw InvalidParameterValue("motor protection stage must be 0 or 1");
    }
    if (!std::isfinite(tripVoltage) || !std::isfinite(reconnectVoltage) ||
        !std::isfinite(tripDelay) || !std::isfinite(reconnectDelay) ||
        !std::isfinite(tripFraction) || (tripVoltage < 0.0) || (reconnectVoltage < 0.0) ||
        (tripDelay < 0.0) || (reconnectDelay < 0.0) || (tripFraction < 0.0) ||
        (tripFraction > 1.0)) {
        throw InvalidParameterValue("invalid motor protection group setting");
    }

    auto& protectionStage = stages[stage];
    protectionStage.tripVoltage = tripVoltage;
    protectionStage.tripDelay = tripDelay;
    protectionStage.tripFraction = tripFraction;
    protectionStage.reconnectVoltage = reconnectVoltage;
    protectionStage.reconnectDelay = reconnectDelay;
    protectionStage.tripStart = timeZero;
    protectionStage.reconnectStart = timeZero;
    protectionStage.tripTimerActive = false;
    protectionStage.reconnectTimerActive = false;
    protectionStage.isTripped = false;
}

void MotorProtectionGroups::initialize(CoreTime time, double voltage)
{
    for (auto& stage : stages) {
        stage.tripTimerActive = false;
        stage.reconnectTimerActive = false;
        stage.isTripped = false;
        if ((stage.tripFraction > 0.0) && (stage.tripVoltage < 2.0) &&
            (voltage < stage.tripVoltage)) {
            if (stage.tripDelay == 0.0) {
                stage.isTripped = true;
            } else {
                stage.tripTimerActive = true;
                stage.tripStart = time;
            }
        }
    }
}

void MotorProtectionGroups::rootTest(CoreTime time, double voltage, double roots[rootCount]) const
{
    for (std::size_t stageIndex = 0; stageIndex < stageCount; ++stageIndex) {
        const auto& stage = stages[stageIndex];
        const std::size_t offset = stageIndex * rootsPerStage;
        const bool tripEnabled = (stage.tripFraction > 0.0) && (stage.tripVoltage < 2.0);
        const bool reconnectEnabled = tripEnabled && (stage.reconnectVoltage < 2.0);
        roots[offset] = (tripEnabled && !stage.isTripped) ? voltage - stage.tripVoltage : 1.0;
        roots[offset + 1] = (tripEnabled && stage.tripTimerActive) ?
            static_cast<double>(time - stage.tripStart) - stage.tripDelay :
            1.0;
        roots[offset + 2] = (reconnectEnabled && stage.isTripped) ?
            voltage - stage.reconnectVoltage :
            1.0;
        roots[offset + 3] = (reconnectEnabled && stage.reconnectTimerActive) ?
            static_cast<double>(time - stage.reconnectStart) - stage.reconnectDelay :
            1.0;
    }
}

bool MotorProtectionGroups::rootTrigger(std::size_t root, CoreTime time, double voltage)
{
    if (root >= rootCount) {
        return false;
    }
    const auto previousOnlineFraction = onlineFraction();
    auto& stage = stages[root / rootsPerStage];
    const bool tripEnabled = (stage.tripFraction > 0.0) && (stage.tripVoltage < 2.0);
    const bool reconnectEnabled = tripEnabled && (stage.reconnectVoltage < 2.0);
    if (!tripEnabled || (((root % rootsPerStage) >= 2) && !reconnectEnabled)) {
        return false;
    }

    switch (root % rootsPerStage) {
        case 0:
            if (!stage.isTripped) {
                if (voltage < stage.tripVoltage) {
                    stage.tripStart = time;
                    stage.tripTimerActive = true;
                    if (stage.tripDelay == 0.0) {
                        stage.tripTimerActive = false;
                        stage.isTripped = true;
                    }
                } else {
                    stage.tripTimerActive = false;
                }
            }
            break;
        case 1:
            if (stage.tripTimerActive && !stage.isTripped &&
                (voltage < stage.tripVoltage) &&
                (static_cast<double>(time - stage.tripStart) >= stage.tripDelay)) {
                stage.tripTimerActive = false;
                stage.isTripped = true;
            }
            break;
        case 2:
            if (stage.isTripped) {
                if (voltage > stage.reconnectVoltage) {
                    stage.reconnectStart = time;
                    stage.reconnectTimerActive = true;
                    if (stage.reconnectDelay == 0.0) {
                        stage.reconnectTimerActive = false;
                        stage.isTripped = false;
                    }
                } else {
                    stage.reconnectTimerActive = false;
                }
            }
            break;
        case 3:
            if (stage.reconnectTimerActive && stage.isTripped &&
                (voltage > stage.reconnectVoltage) &&
                (static_cast<double>(time - stage.reconnectStart) >= stage.reconnectDelay)) {
                stage.reconnectTimerActive = false;
                stage.isTripped = false;
            }
            break;
        default:
            break;
    }
    return std::abs(previousOnlineFraction - onlineFraction()) > 1e-12;
}

double MotorProtectionGroups::onlineFraction() const
{
    double trippedFraction = 0.0;
    for (const auto& stage : stages) {
        if (stage.isTripped) {
            trippedFraction = std::max(trippedFraction, stage.tripFraction);
        }
    }
    return 1.0 - trippedFraction;
}

bool MotorProtectionGroups::tripped(std::size_t stage) const
{
    return (stage < stageCount) ? stages[stage].isTripped : false;
}

}  // namespace griddyn::loads
