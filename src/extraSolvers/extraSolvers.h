/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
namespace griddyn {
/** @brief load the extra models specified by the subset indicator into the object factory
@param[in] subset specify a subset of the models to load
"all" or use default for all models
others will be added as the library grows
*/
void loadExtraSolvers(const std::string& subset = "");
}  // namespace griddyn
