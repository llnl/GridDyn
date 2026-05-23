/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dimeInterface.h"

#include "core/FactoryTemplates.hpp"
#include "core/ObjectFactory.hpp"
#include "dimeCollector.h"
#include "dimeCommunicator.h"
#include "fileInput/ReaderInfo.h"
#include <string>
#include <vector>

namespace griddyn {
void loadDimeLibrary()
{
    static const bool loaded = []() {
        static const ChildClassFactory<dimeLib::DimeCollector, collector> dimeFactory(
            std::vector<std::string>{"dime"});
        static const ChildClassFactory<dimeLib::DimeCommunicator, Communicator>
            dimeCommunicatorFactory(std::vector<std::string>{"dime"});
        return true;
    }();
    (void)loaded;
}

void loadDimeReaderInfoDefinitions(ReaderInfo& readerInformation)
{
    readerInformation.addTranslate("dime", "extra");
}
}  // namespace griddyn
