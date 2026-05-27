/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/Block.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridSubModel.h"
#include "griddyn/Source.h"
#include "griddyn/events/Event.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/simulation/Diagnostics.h"
#include "griddyn/simulation/GridDynSimulationFileOps.h"
#include "griddyn/solvers/SolverInterface.h"
#include <gtest/gtest.h>
#include <iostream>

using namespace griddyn;

TEST(ExtraSizeReportTests, ObjectSizeReport)
{
    std::cout << "solverOffset size=" << sizeof(solverOffsets) << '\n';
    std::cout << "offsetTableSize=" << sizeof(OffsetTable) << '\n';
    std::cout << "solverModeSize=" << sizeof(solverMode) << '\n';
    std::cout << "coreTime size = " << sizeof(coreTime) << '\n';

    auto coreSize = sizeof(CoreObject);
    std::cout << "core object size=" << coreSize << '\n';

    auto compSize = sizeof(GridComponent);
    std::cout << "GridComponent size=" << compSize << " adds " << compSize - coreSize << '\n';

    auto primSize = sizeof(gridPrimary);
    std::cout << "gridPrimary size=" << primSize << " adds " << primSize - compSize << '\n';

    auto secSize = sizeof(gridSecondary);
    std::cout << "gridSecondary size=" << secSize << " adds " << secSize - compSize << '\n';

    std::cout << "bus size=" << sizeof(GridBus) << " adds " << sizeof(GridBus) - primSize << '\n';
    std::cout << "acbus size=" << sizeof(AcBus) << " adds " << sizeof(AcBus) - sizeof(GridBus)
              << '\n';

    std::cout << "load size=" << sizeof(GridLoad) << " adds " << sizeof(GridLoad) - secSize << '\n';
    std::cout << "zipload size=" << sizeof(ZipLoad) << " adds "
              << sizeof(ZipLoad) - sizeof(GridLoad) << '\n';

    std::cout << "Generator size=" << sizeof(Generator) << " adds " << sizeof(Generator) - secSize
              << '\n';
    std::cout << "dynamic Generator size=" << sizeof(DynamicGenerator) << " adds "
              << sizeof(DynamicGenerator) - sizeof(Generator) << '\n';

    std::cout << "Link size=" << sizeof(Link) << " adds " << sizeof(Link) - primSize << '\n';
    std::cout << "ac Link size=" << sizeof(AcLine) << " adds " << sizeof(AcLine) - sizeof(Link)
              << '\n';

    std::cout << "submodel size" << sizeof(GridSubModel) << "adds "
              << sizeof(GridSubModel) - compSize << '\n';

    std::cout << "Source size=" << sizeof(Source) << " adds "
              << sizeof(Source) - sizeof(GridSubModel) << '\n';

    std::cout << "Block size=" << sizeof(GridBlock) << " adds "
              << sizeof(GridBlock) - sizeof(GridSubModel) << '\n';

    std::cout << "Area size=" << sizeof(GridArea) << " adds " << sizeof(GridArea) - primSize
              << '\n';
}
