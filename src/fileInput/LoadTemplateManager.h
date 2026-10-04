/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreDefinitions.hpp"
#include "griddyn/loads/LoadFactory.h"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace griddyn {
class GridArea;
class GridBus;

/** The network scope at which a load record applies. */
enum class LoadTemplateScope : std::uint8_t { System, Area, Zone, Bus };

/** Store load records while a reader resolves their most-specific network scope. */
class LoadTemplateManager {
  private:
    LoadTemplateFactory systemTemplate;
    std::unordered_map<index_t, LoadTemplateFactory> areaTemplates;
    std::unordered_map<int, LoadTemplateFactory> zoneTemplates;
    std::unordered_map<index_t, LoadTemplateFactory> busTemplates;

    const LoadTemplateFactory* findTemplate(const GridBus* bus, const GridArea* rootArea) const;

  public:
    void setTemplate(LoadTemplateScope scope, index_t selector, LoadTemplateFactory factory);
    std::unique_ptr<GridLoad> makeReplacement(const GridLoad& load, const GridArea* rootArea) const;
};
}  // namespace griddyn
