/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "extraModels.h"

#include "core/ObjectFactoryTemplates.hpp"
#include "TxLifeSpan.h"
#include "TxThermalModel.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
static std::vector<std::shared_ptr<ObjectFactory>> gExtraModelFactories;

void loadExtraModels(const std::string& /*subset*/)
{
    auto thermalModelFactory =
        std::make_shared<ChildTypeFactory<extra::TxThermalModel, Relay>>("relay",
                                                                         stringVec{"thermaltx"});
    gExtraModelFactories.push_back(thermalModelFactory);

    auto lifeSpanFactory =
        std::make_shared<ChildTypeFactory<extra::TxLifeSpan, Relay>>("relay",
                                                                     stringVec{"txaging", "txage"});
    gExtraModelFactories.push_back(lifeSpanFactory);
}
}  // namespace griddyn
