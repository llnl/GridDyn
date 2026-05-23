/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsLibrary.h"

#include "core/FactoryTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "fileInput/ReaderInfo.h"
#include "griddyn/griddyn-config.h"
#include "helicsCollector.h"
#include "helicsCommunicator.h"
#include "helicsCoordinator.h"
#include "helicsEvent.h"
#include "helicsLoad.h"
#include "helicsSource.h"
// #include "helics.hpp"

namespace griddyn {
void loadHelicsLibrary()
{
    static const bool loaded = []() {
        static const ChildClassFactory<helicsLib::HelicsCollector, collector>
            helicsCollectorFactory(stringVec{"helics"});
        static const ChildClassFactory<helicsLib::HelicsEvent, Event> helicsEventFactory(
            stringVec{"helics"});
        static const ChildClassFactory<helicsLib::HelicsCommunicator, Communicator>
            helicsCommunicatorFactory(stringVec{"helics"});
        static const ChildTypeFactory<helicsLib::HelicsSource, sources::rampSource>
            helicsSourceFactory("source", std::to_array<std::string_view>({"helics"}));
        static const ChildTypeFactory<helicsLib::HelicsLoad, loads::RampLoad> helicsLoadFactory(
            "load", "helics");
        static const TypeFactory<helicsLib::HelicsCoordinator> helicsCoordinatorFactory("extra",
                                                                                        "helics");
        return true;
    }();
    (void)loaded;
}

void loadHelicsReaderInfoDefinitions(ReaderInfo& ReaderInformation)
{
    ReaderInformation.addTranslate("helics", "extra");
    // ReaderInformation.addTranslate("cosim", "helics");
}

}  // namespace griddyn
