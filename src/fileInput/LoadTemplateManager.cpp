/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LoadTemplateManager.h"

#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/Load.h"
#include "griddyn/loads/LoadTemplateAdapters.h"
#include "core/CoreExceptions.h"
#include <limits>
#include <utility>

namespace griddyn {
void LoadTemplateManager::setTemplate(LoadTemplateScope scope,
                                      index_t selector,
                                      LoadTemplateFactory factory)
{
    if (!factory) {
        throw InvalidParameterValue("load template factory must not be empty");
    }
    switch (scope) {
        case LoadTemplateScope::System:
            systemTemplate = std::move(factory);
            break;
        case LoadTemplateScope::Area:
            areaTemplates[selector] = std::move(factory);
            break;
        case LoadTemplateScope::Zone:
            if (selector > static_cast<index_t>(std::numeric_limits<int>::max())) {
                throw InvalidParameterValue(
                    "zone load template selector exceeds the supported range");
            }
            zoneTemplates[static_cast<int>(selector)] = std::move(factory);
            break;
        case LoadTemplateScope::Bus:
            busTemplates[selector] = std::move(factory);
            break;
    }
}

const LoadTemplateFactory* LoadTemplateManager::findTemplate(const GridBus* bus,
                                                             const GridArea* rootArea) const
{
    if (bus != nullptr) {
        const auto busTemplate = busTemplates.find(bus->getUserID());
        if (busTemplate != busTemplates.end()) {
            return &busTemplate->second;
        }
        const auto zoneTemplate = zoneTemplates.find(bus->zone);
        if (zoneTemplate != zoneTemplates.end()) {
            return &zoneTemplate->second;
        }
        const auto* area = dynamic_cast<const GridArea*>(bus->getParent());
        if ((area != nullptr) && (area != rootArea)) {
            const auto areaTemplate = areaTemplates.find(area->getUserID());
            if (areaTemplate != areaTemplates.end()) {
                return &areaTemplate->second;
            }
        }
    }
    return systemTemplate ? &systemTemplate : nullptr;
}

std::unique_ptr<GridLoad> LoadTemplateManager::makeReplacement(const GridLoad& load,
                                                               const GridArea* rootArea) const
{
    if (load.isFixedShunt() || !loads::supportsCharacteristicReplacement(load)) {
        return {};
    }
    const auto* factory = findTemplate(load.getBus(), rootArea);
    return (factory == nullptr) ? std::unique_ptr<GridLoad>{} : (*factory)(load);
}
}  // namespace griddyn
