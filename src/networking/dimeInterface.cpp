/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dimeInterface.h"

#include "core/factoryTemplates.hpp"
#include "core/objectFactory.hpp"
#include "dimeCollector.h"
#include "dimeCommunicator.h"
#include "fileInput/readerInfo.h"
#include <string>

namespace griddyn {
static childClassFactory<dimeLib::dimeCollector, collector>
    dimeFac(std::vector<std::string>{"dime"});

static childClassFactory<dimeLib::dimeCommunicator, Communicator>
    dimeComm(std::vector<std::string>{"dime"});

void loadDimeLibrary()
{
    static int loaded = 0;

    if (loaded == 0) {
        loaded = 1;
    }
}

void loadDimeReaderInfoDefinitions(readerInfo& ri)
{
    ri.addTranslate("dime", "extra");
}
}  // namespace griddyn
