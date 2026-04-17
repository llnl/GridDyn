/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "extraSolvers.h"

#include "core/factoryTemplates.hpp"
#include "griddyn/solvers/solverInterface.h"
#ifdef ENABLE_BRAID
#    include "braid/braidInterface.h"
#endif
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
static std::vector<std::shared_ptr<classFactory<SolverInterface>>> extraFactories;

void loadExtraSolvers(const std::string& subset)
{
    if ((subset.empty()) || (subset == "braid")) {
#ifdef ENABLE_BRAID
        auto bfact = std::make_shared<childClassFactory<braid::braidSolver, SolverInterface>>(
            stringVec{"braid"});
        extraFactories.push_back(bfact);
#endif
    }
    //  auto b = std::make_shared<childTypeFactory<extra::txThermalModel, Relay>> ("relay",
    //  stringVec{"thermaltx"});
    // extraFactories.push_back (b);

    // auto c = std::make_shared<childTypeFactory<extra::txLifeSpan, Relay>> ("relay",
    // stringVec{"txaging", "txage"}); extraFactories.push_back (c);
}
}  // namespace griddyn
