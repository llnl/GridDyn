/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <climits>
#include <vector>

namespace griddyn {
enum class MessageTags {
    MODEL_SPEC = 1,
    VOLTAGE_STEP = 2,
    CURRENT = 3,
    STOP = 4,
};
/**
 * Voltage message sent from transmission to distribution.
 */

struct ThreePhaseValue {
    double real[3];
    double imag[3];
};

struct VoltageMessage {
    ThreePhaseValue voltage[3];  // LEB: Start with 3 for now...grow later
    int numThreePhaseVoltage;
    unsigned int deltaTime;
};

/**
 * Current message sent from distribution to transmission.
 */
struct CurrentMessage {
    ThreePhaseValue current[3];
    int numThreePhaseCurrent;
};

/**
 * Command line message to initialize a new distribution system model.
 */

#ifndef PATH_MAX
#    define PATH_MAX 128
#endif

typedef struct {
    char arguments[PATH_MAX * 4];
} CommandLineMessage;

}  // namespace griddyn
