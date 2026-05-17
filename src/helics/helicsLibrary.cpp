/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsLibrary.h"

#include "core/factoryTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "fileInput/readerInfo.h"
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
        static const childClassFactory<helicsLib::HelicsCollector, collector> helicsCollectorFactory(
            stringVec{"helics"});
        static const childClassFactory<helicsLib::HelicsEvent, Event> helicsEventFactory(
            stringVec{"helics"});
        static const childClassFactory<helicsLib::HelicsCommunicator, Communicator>
            helicsCommunicatorFactory(stringVec{"helics"});
        static const childTypeFactory<helicsLib::HelicsSource, sources::rampSource>
            helicsSourceFactory("source", std::to_array<std::string_view>({"helics"}));
        static const childTypeFactory<helicsLib::HelicsLoad, loads::rampLoad> helicsLoadFactory(
            "load", "helics");
        static const typeFactory<helicsLib::HelicsCoordinator> helicsCoordinatorFactory(
            "extra", "helics");
        return true;
    }();
    (void)loaded;
}

void loadHelicsReaderInfoDefinitions(readerInfo& readerInformation)
{
    readerInformation.addTranslate("helics", "extra");
    // readerInformation.addTranslate("cosim", "helics");
}

}  // namespace griddyn
