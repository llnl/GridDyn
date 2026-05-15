/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../Link.h"
#include "../events/Event.h"
#include "../gridBus.h"
#include "../gridDynSimulation.h"
#include "../loads/zipLoad.h"
#include "../simulation/diagnostics.h"
#include "contingency.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridDynSimulationFileOps.h"
#include "utilities/GlobalWorkQueue.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
static void buildBusContingencies(gridDynSimulation* gds,
                                  std::vector<std::shared_ptr<Contingency>>& contList,
                                  const extraContingencyInfo& info,
                                  int skip);
static void buildLineContingencies(gridDynSimulation* gds,
                                   std::vector<std::shared_ptr<Contingency>>& contList,
                                   const extraContingencyInfo& info,
                                   int skip);
static void buildGenContingencies(gridDynSimulation* gds,
                                  std::vector<std::shared_ptr<Contingency>>& contList,
                                  const extraContingencyInfo& info,
                                  int skip);
static void buildLoadContingencies(gridDynSimulation* gds,
                                   std::vector<std::shared_ptr<Contingency>>& contList,
                                   const extraContingencyInfo& info,
                                   int skip);

static void addContingency(gridDynSimulation* gds,
                           std::vector<std::shared_ptr<Contingency>>& contList,
                           std::shared_ptr<Event>& newEvent,
                           const extraContingencyInfo& info);

static void addContingencyIfUnique(std::vector<std::shared_ptr<Contingency>>& contList,
                                   const Contingency& contingency1,
                                   const Contingency& contingency2,
                                   bool simplified);

// NOLINTNEXTLINE(misc-no-recursion)
size_t buildContingencyList(gridDynSimulation* gds,
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
            skip = std::max(skip - static_cast<int>(contingencies), 0);

            contingencies = buildContingencyList(gds, ContingencyMode::GEN, contList, info, skip);
            skip = std::max(skip - static_cast<int>(contingencies), 0);
            buildContingencyList(gds, ContingencyMode::LOAD, contList, info, skip);
            break;
        }
        case ContingencyMode::N_1_1:  // N-1-1 contingencies
        {
            const auto nMinusOneContingencies = buildContingencyList(gds, "N-1", info);
            extraContingencyInfo build(info);
            build.stage = 1;
            contList.reserve(nMinusOneContingencies.size() * nMinusOneContingencies.size());
            for (auto& cont : nMinusOneContingencies) {
                build.baseCont = cont->clone();
                buildContingencyList(gds, ContingencyMode::N_1, contList, build);
            }
        } break;
        case ContingencyMode::N_2:  // N-2 contingencies
        {
            const auto nMinusOneContingencies = buildContingencyList(gds, "N-1", info);
            extraContingencyInfo build(info);
            build.stage = 0;
            contList.reserve(nMinusOneContingencies.size() * nMinusOneContingencies.size() / 2);
            int contIndex{0};
            for (auto& cont : nMinusOneContingencies) {
                ++contIndex;
                build.baseCont = cont->clone();
                buildContingencyList(gds, ContingencyMode::N_1, contList, build, contIndex);
            }
            if (skip > 0) {
                if (static_cast<size_t>(skip) >= contList.size()) {
                    contList.clear();
                } else {
                    contList.erase(contList.begin(), contList.begin() + skip);
                }
            }
        } break;
        case ContingencyMode::N_2_LINE:  // N-2 line contingencies
        {
            const auto lineContingencies = buildContingencyList(gds, "line", info);
            extraContingencyInfo build(info);
            build.stage = 0;
            contList.reserve(lineContingencies.size() * lineContingencies.size() / 2);
            int contIndex{0};
            for (auto& cont : lineContingencies) {
                ++contIndex;
                build.baseCont = cont->clone();
                buildLineContingencies(gds, contList, build, contIndex);
            }
            if (skip > 0) {
                if (static_cast<size_t>(skip) >= contList.size()) {
                    contList.clear();
                } else {
                    contList.erase(contList.begin(), contList.begin() + skip);
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
std::vector<std::shared_ptr<Contingency>> buildContingencyList(gridDynSimulation* gds,
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
    const auto& wqI = getGlobalWorkQueue();

    int numContingencies = count2;
    int start = count1;
    if (count2 == 0) {
        if (start <= 0) {
            start = -start;
        } else {
            numContingencies = start;
            start = 0;
        }
    }
    std::vector<std::shared_ptr<Contingency>> usedList;
    usedList.reserve(numContingencies);
    int ccnt{0};
    int skipped{0};
    for (auto& cList : contList) {
        if (!cList) {
            continue;
        }

        if (skipped < start) {
            ++skipped;
            continue;
        }

        wqI->addWorkBlock(cList);
        usedList.push_back(cList);
        ++ccnt;
        if (numContingencies > 0 && ccnt > numContingencies) {
            break;
        }
    }
    if (output.starts_with("file:/")) {
        saveContingencyOutput(usedList, output.substr(7), numContingencies);
    } else if (output.starts_with("database:/")) {
        // TODO(PT)::something with a database
    } else {
        // assume it is a file output
        saveContingencyOutput(usedList, output, numContingencies);
    }
}

void buildBusContingencies(gridDynSimulation* gds,
                           std::vector<std::shared_ptr<Contingency>>& contList,
                           const extraContingencyInfo& info,
                           int skip)
{
    std::vector<gridBus*> buses;
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

void buildLineContingencies(gridDynSimulation* gds,
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

void buildLoadContingencies(gridDynSimulation* gds,
                            std::vector<std::shared_ptr<Contingency>>& contList,
                            const extraContingencyInfo& info,
                            int skip)
{
    std::vector<gridBus*> buses;
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

void buildGenContingencies(gridDynSimulation* gds,
                           std::vector<std::shared_ptr<Contingency>>& contList,
                           const extraContingencyInfo& info,
                           int skip)
{
    std::vector<gridBus*> buses;
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

void addContingency(gridDynSimulation* gds,
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
