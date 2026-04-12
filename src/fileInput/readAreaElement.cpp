/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
    Area* area = ElementReaderSetup(
        element, static_cast<Area*>(nullptr), areaComponentName, ri, searchObject);

    loadElementInformation(area, element, areaComponentName, ri, areaIgnoreElements);

    LEVELPRINT(READER_NORMAL_PRINT, "loaded Area " << area->getName());

    ri.closeScope(riScope);
    return area;
}

}  // namespace griddyn
