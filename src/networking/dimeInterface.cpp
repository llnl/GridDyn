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
void loadDimeLibrary()
{
    static const bool loaded = []() {
        static childClassFactory<dimeLib::DimeCollector, collector> dimeFactory(
            std::vector<std::string>{"dime"});
        static childClassFactory<dimeLib::DimeCommunicator, Communicator> dimeCommunicatorFactory(
            std::vector<std::string>{"dime"});
        return true;
    }();
    (void)loaded;
}

void loadDimeReaderInfoDefinitions(readerInfo& readerInformation)
{
    readerInformation.addTranslate("dime", "extra");
}
}  // namespace griddyn
