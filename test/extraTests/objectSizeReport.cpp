/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/Area.h"
#include "griddyn/Block.h"
#include "griddyn/Source.h"
#include "griddyn/events/Event.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/gridSubModel.h"
#include "griddyn/links/acLine.h"
#include "griddyn/loads/zipLoad.h"
#include "griddyn/primary/acBus.h"
#include "griddyn/simulation/diagnostics.h"
#include "griddyn/simulation/gridDynSimulationFileOps.h"
#include "griddyn/solvers/solverInterface.h"
#include <iostream>

#include <gtest/gtest.h>

using namespace griddyn;

TEST(ExtraSizeReportTests, ObjectSizeReport)
{
    std::cout << "solverOffset size=" << sizeof(solverOffsets) << '\n';
    std::cout << "offsetTableSize=" << sizeof(offsetTable) << '\n';
    std::cout << "solverModeSize=" << sizeof(solverMode) << '\n';
    std::cout << "coreTime size = " << sizeof(coreTime) << '\n';

    auto coreSize = sizeof(coreObject);
    std::cout << "core object size=" << coreSize << '\n';

    auto compSize = sizeof(gridComponent);
    std::cout << "gridComponent size=" << compSize << " adds " << compSize - coreSize << '\n';

    auto primSize = sizeof(gridPrimary);
    std::cout << "gridPrimary size=" << primSize << " adds " << primSize - compSize << '\n';

    auto secSize = sizeof(gridSecondary);
    std::cout << "gridSecondary size=" << secSize << " adds " << secSize - compSize << '\n';

    std::cout << "bus size=" << sizeof(gridBus) << " adds " << sizeof(gridBus) - primSize << '\n';
    std::cout << "acbus size=" << sizeof(acBus) << " adds " << sizeof(acBus) - sizeof(gridBus)
              << '\n';

    std::cout << "load size=" << sizeof(Load) << " adds " << sizeof(Load) - secSize << '\n';
    std::cout << "zipload size=" << sizeof(zipLoad) << " adds " << sizeof(zipLoad) - sizeof(Load)
              << '\n';

    std::cout << "Generator size=" << sizeof(Generator) << " adds " << sizeof(Generator) - secSize
              << '\n';
    std::cout << "dynamic Generator size=" << sizeof(DynamicGenerator) << " adds "
              << sizeof(DynamicGenerator) - sizeof(Generator) << '\n';

    std::cout << "Link size=" << sizeof(Link) << " adds " << sizeof(Link) - primSize << '\n';
    std::cout << "ac Link size=" << sizeof(acLine) << " adds " << sizeof(acLine) - sizeof(Link)
              << '\n';

    std::cout << "submodel size" << sizeof(gridSubModel) << "adds "
              << sizeof(gridSubModel) - compSize << '\n';

    std::cout << "Source size=" << sizeof(Source) << " adds "
              << sizeof(Source) - sizeof(gridSubModel) << '\n';

    std::cout << "Block size=" << sizeof(Block) << " adds " << sizeof(Block) - sizeof(gridSubModel)
              << '\n';
}
