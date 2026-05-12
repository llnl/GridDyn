/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "extraModels.h"

#include "core/objectFactoryTemplates.hpp"
#include "txLifeSpan.h"
#include "txThermalModel.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
static std::vector<std::shared_ptr<objectFactory>> extraModelFactories;

void loadExtraModels(const std::string& /*subset*/)
{
    auto thermalModelFactory =
        std::make_shared<childTypeFactory<extra::txThermalModel, Relay>>("relay",
                                                                         stringVec{"thermaltx"});
    extraModelFactories.push_back(thermalModelFactory);

    auto lifeSpanFactory =
        std::make_shared<childTypeFactory<extra::txLifeSpan, Relay>>("relay",
                                                                     stringVec{"txaging", "txage"});
    extraModelFactories.push_back(lifeSpanFactory);
}
}  // namespace griddyn
