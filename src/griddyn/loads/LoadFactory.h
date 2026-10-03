/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <functional>
#include <memory>

namespace griddyn {
class GridLoad;

/** Build a replacement load from an existing load's steady-state data. */
using LoadTemplateFactory = std::function<std::unique_ptr<GridLoad>(const GridLoad&)>;
}  // namespace griddyn
