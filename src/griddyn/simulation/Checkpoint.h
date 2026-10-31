/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

namespace griddyn {
class GridDynSimulation;

/** Parameters always include their associated runtime and solver state. */
enum class CheckpointLevel { STATE, PARAMETERS };

/** Save an accepted iterated-power-flow boundary to a JSON manifest and HDF5 payload.
 * Requires a compatible initialized model when loading. Unsupported model/event
 * persistence is reported explicitly. Dynamic restart is a separate profile.
 */
void saveCheckpoint(GridDynSimulation* simulation,
                    const std::string& fileName,
                    CheckpointLevel level = CheckpointLevel::PARAMETERS);

/** Restore the checkpoint's clock, supported component runtime and event queue.
 * Settings are also restored for a PARAMETERS checkpoint. All referenced files
 * and typed fields are read before model assignments begin. Edited parameters
 * are accepted without attempting to reconcile their physical consistency.
 */
void loadCheckpoint(GridDynSimulation* simulation, const std::string& fileName);
}  // namespace griddyn
