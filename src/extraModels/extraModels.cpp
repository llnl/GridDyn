/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
static std::vector<std::shared_ptr<objectFactory>> extraFactories;

void loadExtraModels(const std::string& /*subset*/)
{
    auto b =
        std::make_shared<childTypeFactory<extra::txThermalModel, Relay>>("relay",
                                                                         stringVec{"thermaltx"});
    extraFactories.push_back(b);

    auto c =
        std::make_shared<childTypeFactory<extra::txLifeSpan, Relay>>("relay",
                                                                     stringVec{"txaging", "txage"});
    extraFactories.push_back(c);
}
}  // namespace griddyn
