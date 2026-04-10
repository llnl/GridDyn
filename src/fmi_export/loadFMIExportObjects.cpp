/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
static childClassFactory<fmi::fmiCollector, collector>
    fmiColFac(std::vector<std::string>{"fmioutput", "fmicollector"});

static childClassFactoryArg<fmi::fmiEvent, Event, fmi::fmiEvent::fmiEventType>
    fmiIn(std::vector<std::string>{"fmiinput", "fmievent"}, fmi::fmiEvent::fmiEventType::input);

static childClassFactoryArg<fmi::fmiEvent, Event, fmi::fmiEvent::fmiEventType>
    fmiParam(std::vector<std::string>{"fmiparam", "fmiparameter"},
             fmi::fmiEvent::fmiEventType::parameter);
// static childClassFactory<dimeCommunicator, Communicator>
// dimeComm(std::vector<std::string>{"dime"});

static typeFactory<fmi::fmiCoordinator> coord("extra", std::vector<std::string>{"fmi", "fmicoord"});

void loadFMIExportObjects() {}

void loadFmiExportReaderInfoDefinitions(readerInfo& ri)
{
    ri.addTranslate("fmi", "extra");
    ri.addTranslate("fmicoord", "extra");
    ri.addTranslate("fmiparam", "event");
    ri.addTranslate("fmiinput", "event");
    ri.addTranslate("fmioutput", "collector");
}

}  // namespace griddyn
