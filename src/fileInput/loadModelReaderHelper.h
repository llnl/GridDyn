/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "LoadTemplateManager.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Load.h"
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace griddyn {
/** Diagnose dynamic records read before their static network. */
inline void warnIfStaticNetworkMissing(CoreObject* parentObject,
                                       std::string_view modelFileType,
                                       std::string_view fileName)
{
    const auto* simulation = dynamic_cast<const GridDynSimulation*>(parentObject->getRoot());
    if ((simulation != nullptr) && (simulation->getInt("totalbuscount") == 0)) {
        parentObject->log(parentObject,
                          PrintLevel::WARNING,
                          std::string{modelFileType} +
                              " model file loaded before a static network; load a SAVE, EPC, "
                              "RAW, M, or PY case first: " +
                              std::string{fileName});
    }
}

/** Apply load-model records collected by a file reader to the network it loaded. */
inline void applyLoadTemplatesFromReader(GridDynSimulation& simulation,
                                         const LoadTemplateManager& templates)
{
    std::vector<GridBus*> buses;
    simulation.getBusVector(buses);
    std::vector<std::tuple<GridBus*, GridLoad*, std::unique_ptr<GridLoad>>> replacements;
    for (auto* bus : buses) {
        for (index_t index = 0; bus->getLoad(index) != nullptr; ++index) {
            auto* load = bus->getLoad(index);
            auto replacement = templates.makeReplacement(*load, &simulation);
            if (replacement) {
                replacements.emplace_back(bus, load, std::move(replacement));
            }
        }
    }
    for (auto& [bus, oldLoad, replacement] : replacements) {
        auto* newLoad = replacement.get();
        bus->replaceLoad(oldLoad, newLoad);
        std::ignore = replacement.release();
    }
}
}  // namespace griddyn
