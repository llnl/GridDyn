/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreDefinitions.hpp"

namespace griddyn {
class CoreObject;

namespace detail {
    /** Load one normalized DYR-style model record using the existing model loaders.
     *
     * @return true when the model type is recognized; model-specific validation
     *         errors are reported by throwing InvalidParameterValue.
     */
    bool loadDyrModelRecord(CoreObject* parentObject,
                            stringVec& lineTokens,
                            bool disableStabilizers,
                            count_t& zeroGainStabilizers);
}  // namespace detail
}  // namespace griddyn
