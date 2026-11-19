/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "readerHelper.h"

#include "fileInput.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include <filesystem>

// library for printf debug statements

#include <cmath>
namespace griddyn {

CoreObject* getReaderLinkParent(CoreObject* parentObject, const Link* link)
{
    if ((link == nullptr) || (link->terminalCount() != 2)) {
        return parentObject;
    }

    auto* bus1 = link->getBus(1);
    auto* bus2 = link->getBus(2);
    auto* area1 = (bus1 != nullptr) ? dynamic_cast<GridArea*>(bus1->getParent()) : nullptr;
    auto* area2 = (bus2 != nullptr) ? dynamic_cast<GridArea*>(bus2->getParent()) : nullptr;
    if ((area1 == nullptr) || (area1 != area2)) {
        return parentObject;
    }

    for (auto* ancestor = area1; ancestor != nullptr;
         ancestor = dynamic_cast<GridArea*>(ancestor->getParent())) {
        if (ancestor == parentObject) {
            return area1;
        }
    }
    return parentObject;
}

void processParamString(GridParameter& param, ReaderInfo& readerInformation)
{
    if (!param.stringType) {
        return;
    }
    const double interpretedValue = interpretString(param.strVal, readerInformation);
    if (!std::isnan(interpretedValue)) {
        param.value = interpretedValue;
        param.stringType = false;
    } else {
        // can't be interpreted as a number so do a last check for string redefinitions
        param.strVal = readerInformation.checkDefines(param.strVal);
    }
}

}  // namespace griddyn
