/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <climits>
#include <vector>

// SGS make this an enum, #defines are bad.  Moved into class
#define MODELSPECTAG 1
#define VOLTAGESTEPTAG 2
#define CURRENTTAG 3
#define STOPTAG 4

namespace griddyn {
enum class messageTags {
    model_spec = 1,
    voltage_step = 2,
    current = 3,
    stop = 4,
};
/**
 * Voltage message sent from transmission to distribution.
 */

typedef struct {
    double real[3];
    double imag[3];
} ThreePhaseValue;

struct Vmessage {
    ThreePhaseValue voltage[3];  // LEB: Start with 3 for now...grow later
    int numThreePhaseVoltage;
    unsigned int deltaTime;
};
typedef struct Vmessage VoltageMessage;

/**
 * Current message sent from distribution to transmission.
 */
struct Cmessage {
    ThreePhaseValue current[3];
    int numThreePhaseCurrent;
};
typedef struct Cmessage CurrentMessage;

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
