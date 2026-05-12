/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "griddyn/Area.h"
#include "readElement.h"
#include "readerHelper.h"
#include <cstdio>
#include <string>

namespace griddyn {
static const IgnoreListType areaIgnoreElements{"agc", "reserve", "reservedispatch", "dispatch"};
static const char areaComponentName[] = "area";
Area* readAreaElement(std::shared_ptr<readerElement>& element,
                      readerInfo& ri,
                      coreObject* searchObject)
{
    auto riScope = ri.newScope();

    // boiler plate code to setup the object from references or new object
    Area* areaObject = ElementReaderSetup(
        element, static_cast<Area*>(nullptr), areaComponentName, ri, searchObject);

    loadElementInformation(areaObject, element, areaComponentName, ri, areaIgnoreElements);

    LEVELPRINT(READER_NORMAL_PRINT, "loaded Area " << areaObject->getName());

    ri.closeScope(riScope);
    return areaObject;
}

}  // namespace griddyn
