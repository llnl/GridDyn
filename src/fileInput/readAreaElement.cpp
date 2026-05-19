/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "griddyn/GridArea.h"
#include "readElement.h"
#include "readerHelper.h"
#include <cstdio>
#include <string>

namespace griddyn {
namespace {
    const IgnoreListType& areaIgnoreElements()
    {
        static const auto* ignoreElements =
            new IgnoreListType{"agc", "reserve", "reservedispatch", "dispatch"};
        return *ignoreElements;
    }
}  // namespace
static const char areaComponentName[] = "area";
GridArea* readGridAreaElement(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation,
                      CoreObject* searchObject)
{
    auto riScope = readerInformation.newScope();

    // boiler plate code to setup the object from references or new object
    GridArea* areaObject = ElementReaderSetup(
        element, static_cast<GridArea*>(nullptr), areaComponentName, readerInformation, searchObject);

    loadElementInformation(
        areaObject, element, areaComponentName, readerInformation, areaIgnoreElements());

    LEVELPRINT(READER_NORMAL_PRINT, "loaded GridArea " << areaObject->getName());

    readerInformation.closeScope(riScope);
    return areaObject;
}

}  // namespace griddyn
