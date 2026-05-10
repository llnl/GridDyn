/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GlobalWorkQueue.hpp"

#include "griddyn/griddyn-config.h"
#include <memory>

namespace griddyn {
const std::shared_ptr<gmlc::containers::WorkQueue>& getGlobalWorkQueue(int threads)
{
    static std::shared_ptr<gmlc::containers::WorkQueue> instance =
        std::make_shared<gmlc::containers::WorkQueue>(threads);
    return instance;
}
}  // namespace griddyn
