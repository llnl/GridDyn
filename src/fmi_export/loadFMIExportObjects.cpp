/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "loadFMIExportObjects.h"

#include "core/factoryTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "fileInput/readerInfo.h"
#include "fmiCollector.h"
#include "fmiCoordinator.h"
#include "fmiEvent.h"
#include <string>
#include <vector>

namespace griddyn {
namespace {
    void registerFmiExportFactories()
    {
        static const childClassFactory<fmi::fmiCollector, collector> fmiCollectorFactory(
            std::vector<std::string>{"fmioutput", "fmicollector"});

        static const childClassFactoryArg<fmi::fmiEvent, Event, fmi::fmiEvent::FmiEventType>
            fmiInputFactory(std::vector<std::string>{"fmiinput", "fmievent"},
                            fmi::fmiEvent::FmiEventType::INPUT);

        static const childClassFactoryArg<fmi::fmiEvent, Event, fmi::fmiEvent::FmiEventType>
            fmiParameterFactory(std::vector<std::string>{"fmiparam", "fmiparameter"},
                                fmi::fmiEvent::FmiEventType::PARAMETER);

        static const typeFactory<fmi::fmiCoordinator> fmiCoordinatorFactory(
            "extra", std::vector<std::string>{"fmi", "fmicoord"});

        static_cast<void>(fmiCollectorFactory);
        static_cast<void>(fmiInputFactory);
        static_cast<void>(fmiParameterFactory);
        static_cast<void>(fmiCoordinatorFactory);
    }
}  // namespace

void loadFMIExportObjects()
{
    registerFmiExportFactories();
}

void loadFmiExportReaderInfoDefinitions(readerInfo& readerInformation)
{
    registerFmiExportFactories();
    readerInformation.addTranslate("fmi", "extra");
    readerInformation.addTranslate("fmicoord", "extra");
    readerInformation.addTranslate("fmiparam", "event");
    readerInformation.addTranslate("fmiinput", "event");
    readerInformation.addTranslate("fmioutput", "collector");
}

}  // namespace griddyn
