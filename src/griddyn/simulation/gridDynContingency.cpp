/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../GridBus.h"
#include "../GridDynSimulation.h"
#include "../Link.h"
#include "../events/Event.h"
#include "../loads/ZipLoad.h"
#include "../simulation/Diagnostics.h"
#include "Contingency.h"
#include "GridDynSimulationFileOps.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/GlobalWorkQueue.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
static void buildBusContingencies(GridDynSimulation* gds,
                                  std::vector<std::shared_ptr<Contingency>>& contList,
                                  const extraContingencyInfo& info,
                                  int skip);
static void buildLineContingencies(GridDynSimulation* gds,
                                   std::vector<std::shared_ptr<Contingency>>& contList,
                                   const extraContingencyInfo& info,
                                   int skip);
static void buildGenContingencies(GridDynSimulation* gds,
                                  std::vector<std::shared_ptr<Contingency>>& contList,
                                  const extraContingencyInfo& info,
                                  int skip);
static void buildLoadContingencies(GridDynSimulation* gds,
                                   std::vector<std::shared_ptr<Contingency>>& contList,
                                   const extraContingencyInfo& info,
                                   int skip);

static void addContingency(GridDynSimulation* gds,
                           std::vector<std::shared_ptr<Contingency>>& contList,
                           std::shared_ptr<Event>& newEvent,
                           const extraContingencyInfo& info);

static void addContingencyIfUnique(std::vector<std::shared_ptr<Contingency>>& contList,
                                   const Contingency& contingency1,
                                   const Contingency& contingency2,
                                   bool simplified);

// NOLINTNEXTLINE(misc-no-recursion)
size_t buildContingencyList(GridDynSimulation* gds,
                            ContingencyMode cmode,
                            std::vector<std::shared_ptr<Contingency>>& contList,
                            const extraContingencyInfo& info,
                            int skip)
{
    auto cnt = contList.size();
    switch (cmode) {
        case ContingencyMode::N_1:  // N-1 contingencies
        {
            auto contingencies =
                buildContingencyList(gds, ContingencyMode::LINE, contList, info, skip);
            skip = (std::max)(skip - static_cast<int>(contingencies), 0);

            contingencies = buildContingencyList(gds, ContingencyMode::GEN, contList, info, skip);
            skip = (std::max)(skip - static_cast<int>(contingencies), 0);
            buildContingencyList(gds, ContingencyMode::LOAD, contList, info, skip);
            break;
        }
        case ContingencyMode::N_1_1:  // N-1-1 contingencies
        {
            const auto nMinusOneContingencies = buildContingencyList(gds, "N-1", info);
            extraContingencyInfo build(info);
            build.stage = 1;
            contList.reserve(nMinusOneContingencies.size() * nMinusOneContingencies.size());
            for (const auto& cont : nMinusOneContingencies) {
                build.baseCont = cont->clone();
                buildContingencyList(gds, ContingencyMode::N_1, contList, build);
            }
        } break;
        case ContingencyMode::N_2:
        case ContingencyMode::N_2_LINE: {
            const auto base =
                buildContingencyList(gds, cmode == ContingencyMode::N_2 ? "N-1" : "line", info);
            for (std::size_t first = 0; first < base.size(); ++first) {
                for (std::size_t second = first + 1; second < base.size(); ++second) {
                    if (skip > 0) {
                        --skip;
                        continue;
                    }
                    addContingencyIfUnique(contList, *base[first], *base[second], info.simplified);
                }
            }
        } break;
        case ContingencyMode::N_3_LINE:  // N-3 line contingencies
        {
            const auto lineContingencies = buildContingencyList(gds, "line", info);
            const auto nMinusTwoLineContingencies = buildContingencyList(gds, "N-2-LINE", info);
            extraContingencyInfo build(info);
            build.stage = 0;
            contList.reserve(nMinusTwoLineContingencies.size() * lineContingencies.size() / 2);
            for (const auto& cont2 : nMinusTwoLineContingencies) {
                for (const auto& cont1 : lineContingencies) {
                    addContingencyIfUnique(contList, *cont2, *cont1, info.simplified);
                }
            }
            if (skip > 0) {
                if (static_cast<size_t>(skip) >= contList.size()) {
                    contList.clear();
                } else {
                    contList.erase(contList.begin(), contList.begin() + skip);
                }
            }
        } break;
        case ContingencyMode::BUS:  // bus contingencies --disabling each bus for a contingency
        {
            buildBusContingencies(gds, contList, info, skip);
        } break;
        case ContingencyMode::LINE:  // Disabling each line
        {
            buildLineContingencies(gds, contList, info, skip);
        } break;
        case ContingencyMode::LOAD:  // Disabling each load
        {
            buildLoadContingencies(gds, contList, info, skip);
        } break;
        case ContingencyMode::GEN:  // disabling each generator
        {
            buildGenContingencies(gds, contList, info, skip);
        } break;
        case ContingencyMode::CUSTOM:
        case ContingencyMode::UNKNOWN:
        default:
            break;
    }

    return static_cast<index_t>(contList.size() - cnt);
}

// NOLINTNEXTLINE(misc-no-recursion)
std::vector<std::shared_ptr<Contingency>> buildContingencyList(GridDynSimulation* gds,
                                                               const std::string& contMode,
                                                               const extraContingencyInfo& info,
                                                               int skip)
{
    const ContingencyMode cmode = getContingencyMode(contMode);
    std::vector<std::shared_ptr<Contingency>> contList;
    buildContingencyList(gds, cmode, contList, info, skip);

    return contList;
}

void runContingencyAnalysis(std::vector<std::shared_ptr<Contingency>>& contList,
                            const std::string& output,
                            int count1,
                            int count2)
{
    // With one range operand, select the first count1 scenarios.
    // With two, start at the zero-based count1 and select exactly count2.
    std::vector<std::shared_ptr<Contingency>> selected;
    std::ranges::copy_if(contList, std::back_inserter(selected), [](const auto& entry) {
        return entry != nullptr;
    });
    const auto start = static_cast<std::size_t>(count2 > 0 ? count1 : 0);
    const auto count = static_cast<std::size_t>(count2 > 0 ? count2 : std::max(count1, 0));
    if (count1 < -1 || count2 < -1 || start >= selected.size() || count > selected.size() - start) {
        throw InvalidParameterValue("contingency range");
    }
    if (count != 0) {
        selected.resize(start + count);
    }
    selected.erase(selected.begin(), selected.begin() + static_cast<std::ptrdiff_t>(start));

    if (output.starts_with("database:")) {
        throw InvalidParameterValue("contingency database output");
    }
    constexpr std::string_view filePrefix = "file:/";
    const auto destination =
        output.starts_with(filePrefix) ? output.substr(filePrefix.size()) : output;
    if (destination.empty() && !output.empty()) {
        throw InvalidParameterValue("contingency output file");
    }

    const auto& wqI = getGlobalWorkQueue();
    for (const auto& contingency : selected) {
        wqI->addWorkBlock(contingency);
    }
    for (const auto& contingency : selected) {
        contingency->wait();
    }
    saveContingencyOutput(selected, destination);
}

void buildBusContingencies(GridDynSimulation* gds,
                           std::vector<std::shared_ptr<Contingency>>& contList,
                           const extraContingencyInfo& info,
                           int skip)
{
    std::vector<GridBus*> buses;
    gds->getBusVector(buses);
    const size_t startSize = contList.size();
    contList.reserve(startSize + buses.size());
    int busIndex{0};
    for (auto& bus : buses) {
        if (bus->isConnected()) {
            if (busIndex++ < skip) {
                continue;
            }
            std::shared_ptr<Event> event = std::make_shared<Event>();
            event->setTarget(bus, "enabled");
            event->setValue(0.0);
            addContingency(gds, contList, event, info);
        }
    }
}

void buildLineContingencies(GridDynSimulation* gds,
                            std::vector<std::shared_ptr<Contingency>>& contList,
                            const extraContingencyInfo& info,
                            int skip)
{
    std::vector<Link*> links;
    gds->getLinkVector(links);
    const size_t startSize = contList.size();
    int lineIndex{0};
    contList.reserve(startSize + links.size());
    for (auto& lnk : links) {
        if (lnk->isConnected()) {
            if (lineIndex++ < skip) {
                continue;
            }
            std::shared_ptr<Event> event = std::make_shared<Event>();
            event->setTarget(lnk, "connected");
            event->setValue(0.0);
            addContingency(gds, contList, event, info);
        }
    }
}

void buildLoadContingencies(GridDynSimulation* gds,
                            std::vector<std::shared_ptr<Contingency>>& contList,
                            const extraContingencyInfo& info,
                            int skip)
{
    std::vector<GridBus*> buses;
    gds->getBusVector(buses);
    const size_t startSize = contList.size();
    contList.reserve(startSize + buses.size());
    int loadIndex{0};
    for (auto& bus : buses) {
        if (bus->isConnected()) {
            index_t loadOffset = 0;
            auto* load = bus->getLoad(0);
            while (load != nullptr) {
                if (loadIndex++ < skip) {
                    ++loadOffset;
                    load = bus->getLoad(loadOffset);
                    continue;
                }
                auto event = std::make_shared<Event>();
                event->setTarget(load, "connected");
                event->setValue(0.0);
                addContingency(gds, contList, event, info);
                ++loadOffset;
                load = bus->getLoad(loadOffset);
            }
        }
    }
}

void buildGenContingencies(GridDynSimulation* gds,
                           std::vector<std::shared_ptr<Contingency>>& contList,
                           const extraContingencyInfo& info,
                           int skip)
{
    std::vector<GridBus*> buses;
    gds->getBusVector(buses);
    const size_t startSize = contList.size();
    contList.reserve(startSize + buses.size());
    int genIndex{0};
    for (auto& bus : buses) {
        if (bus->isConnected()) {
            index_t genOffset = 0;
            auto* gen = bus->getGen(0);
            while (gen != nullptr) {
                if (genIndex++ < skip) {
                    ++genOffset;
                    gen = bus->getGen(genOffset);
                    continue;
                }
                auto event = std::make_shared<Event>();
                event->setTarget(gen, "connected");
                event->setValue(0.0);
                addContingency(gds, contList, event, info);
                ++genOffset;
                gen = bus->getGen(genOffset);
            }
        }
    }
}

void addContingency(GridDynSimulation* gds,
                    std::vector<std::shared_ptr<Contingency>>& contList,
                    std::shared_ptr<Event>& newEvent,
                    const extraContingencyInfo& info)
{
    if (info.baseCont) {
        auto cont = info.baseCont->clone();
        cont->add(newEvent, info.stage);
        contList.push_back(std::move(cont));
    } else {
        if (info.stage == 0) {
            contList.push_back(std::make_shared<Contingency>(gds, newEvent));
        } else {
            auto cont = std::make_shared<Contingency>(gds);
            cont->add(newEvent, info.stage);
            contList.push_back(std::move(cont));
        }
    }
    if (info.simplified) {
        contList.back()->simplifiedOutput = info.simplified;
    }
}

void addContingencyIfUnique(std::vector<std::shared_ptr<Contingency>>& contList,
                            const Contingency& contingency1,
                            const Contingency& contingency2,
                            bool simplified)
{
    auto cont = contingency1.clone();
    if (cont->mergeIfUnique(contingency2)) {
        cont->simplifiedOutput = simplified;
        contList.push_back(std::move(cont));
    }
}

}  // namespace griddyn
