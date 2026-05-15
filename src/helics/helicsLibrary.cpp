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
namespace helicsLib {
    static childClassFactory<helicsCollector, collector> helicsCollFac(stringVec{"helics"});

    static childClassFactory<helicsEvent, Event> helicsEventFac(stringVec{"helics"});

    static childClassFactory<helicsCommunicator, Communicator> hcomms(stringVec{"helics"});

    static childTypeFactory<helicsSource, sources::rampSource> fnsrc(
        "source", std::to_array<std::string_view>({"helics"}));
    static childTypeFactory<helicsLoad, loads::rampLoad> fnld("load", "helics");

    // the factory for the coordinator
    static typeFactory<helicsCoordinator> cbuild("extra", "helics");
}  // namespace helicsLib

void loadHELICSLibrary()
{
    static int loaded = 0;

    if (loaded == 0) {
        loaded = 1;
    }
}

void loadHelicsReaderInfoDefinitions(readerInfo& ri)
{
    ri.addTranslate("helics", "extra");
    // ri.addTranslate("cosim", "helics");
}

}  // namespace griddyn
