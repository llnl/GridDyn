/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gmlc/containers/WorkQueue.hpp"
#include <memory>

namespace griddyn {
/** get a copy of a pointer to a global work queue
the work queue itself is thread safe
*/
const std::shared_ptr<gmlc::containers::WorkQueue>& getGlobalWorkQueue(int threads = -1);
}  // namespace griddyn
