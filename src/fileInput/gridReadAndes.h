/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

namespace griddyn {
class CoreObject;

/** Load the DC-model sections used by an ANDES JSON case.
 *
 * Returns false when the file is not an ANDES case, allowing callers to use
 * the normal GridDyn JSON reader for native files.
 */
bool loadAndesJson(CoreObject* parentObject, const std::string& fileName);
}  // namespace griddyn
