/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ZipLoad.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    constexpr double kFiniteLimit = 1.0e12;
    constexpr std::string_view kDigits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::string rawNumber(double value)
    {
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
        return text.str();
    }

    std::string rawId(std::size_t sequence)
    {
        std::string result;
        do {
            result.insert(result.begin(), kDigits[sequence % kDigits.size()]);
            sequence /= kDigits.size();
        } while (sequence != 0 && result.size() < 2);
        if (result.size() > 2) {
            result = "ZZ";
        }
        return result;
    }

    std::string rawQuoted(std::string value,
                          std::size_t maxLength,
                          const std::string& label,
                          const std::function<void(const std::string&)>& warning)
    {
        for (char& character : value) {
            if (character == '\'' || character == ',' || character == '\r' || character == '\n') {
                character = '_';
            }
        }
        if (value.size() > maxLength) {
            warning(label + " exceeds the RAW field width and was truncated");
            value.resize(maxLength);
        }
        return "'" + value + "'";
    }

    void writeRawRecord(std::ostream& output, const std::vector<std::string>& fields)
    {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (index != 0) {
                output << ',';
            }
            output << fields[index];
        }
        output << '\n';
    }

    std::string finiteRawLimit(double value,
                               double fallback,
                               const std::string& label,
                               const std::function<void(const std::string&)>& warning)
    {
        if (!std::isfinite(value) || std::abs(value) > kFiniteLimit) {
            warning(label + " is unbounded; exported as " + rawNumber(fallback));
            return rawNumber(fallback);
        }
        return rawNumber(value);
    }

    std::string rawRating(double ratingPU,
                          double basePower,
                          const std::string& label,
                          const std::function<void(const std::string&)>& warning)
    {
        if (!std::isfinite(ratingPU) || ratingPU < 0.0 || basePower <= 0.0 ||
            ratingPU >= kFiniteLimit / basePower) {
            if (!std::isfinite(ratingPU) || ratingPU < 0.0) {
                warning(label + " is unbounded; exported as an unlimited rating");
            }
            return "0";
        }
        return rawNumber(ratingPU * basePower);
    }

    std::string safeComment(std::string text)
    {
        for (char& character : text) {
            if (character == '\r' || character == '\n') {
                character = ' ';
            }
        }
        return text;
    }

    struct RawBus {
        const GridBus* object{};
        index_t number{};
        int area{1};
        int zone{1};
    };

    struct RawArea {
        const GridArea* object{};
        int number{1};
    };

    struct RawLoad {
        const RawBus* bus{};
        const GridLoad* object{};
        std::string id;
    };

    struct RawGenerator {
        const RawBus* bus{};
        const Generator* object{};
        std::string id;
    };

    struct RawLink {
        const Link* object{};
        const AcLine* line{};
        const RawBus* bus1{};
        const RawBus* bus2{};
        std::string id;
        bool transformer{false};
    };

    bool
        saveRawV33(const CoreObject* parentObject, const std::string& fileName, stringVec* warnings)
    {
        if (warnings != nullptr) {
            warnings->clear();
        }
        const auto warning = [warnings](const std::string& message) {
            const std::string fullMessage = "PSS/E RAW v33 export warning: " + message;
            if (warnings != nullptr) {
                warnings->push_back(fullMessage);
            } else {
                std::cerr << fullMessage << '\n';
            }
        };
        if (parentObject == nullptr) {
            warning("no network object was supplied");
            return false;
        }

        const double basePower = parentObject->get("basepower", units::MW);
        if (!std::isfinite(basePower) || basePower <= 0.0) {
            warning("system base power must be positive and finite");
            return false;
        }

        const auto busCount = static_cast<index_t>(parentObject->get("totalbuscount"));
        std::vector<RawBus> buses;
        std::unordered_map<const GridBus*, const RawBus*> busLookup;
        std::unordered_set<index_t> usedBusNumbers;
        for (index_t index = 1; index <= busCount; ++index) {
            const auto* bus =
                dynamic_cast<const GridBus*>(parentObject->findByUserID("bus", index));
            if (bus == nullptr) {
                warning("bus slot " + std::to_string(index) + " is not an AC bus and was omitted");
                continue;
            }
            index_t number = bus->getUserID();
            if ((number == 0) || (number > 999999) || !usedBusNumbers.insert(number).second) {
                number = static_cast<index_t>(buses.size() + 1);
                while (usedBusNumbers.contains(number)) {
                    ++number;
                }
                usedBusNumbers.insert(number);
                warning(bus->getName() + " has no usable unique RAW bus number; assigned " +
                        std::to_string(number));
            }
            buses.push_back({bus, number, 1, 1});
            busLookup.emplace(bus, &buses.back());
        }
        if (buses.empty()) {
            warning("no exportable AC buses were found");
            return false;
        }
        // Vector growth can invalidate the pointers stored above. Rebuild the lookup once
        // collection is complete.
        busLookup.clear();
        for (const auto& bus : buses) {
            busLookup.emplace(bus.object, &bus);
        }

        std::vector<RawArea> areas;
        std::unordered_map<const GridArea*, int> areaNumbers;
        std::unordered_map<int, const GridArea*> areasByNumber;
        std::set<int> areaIds{1};
        int nextArea = 2;
        for (auto& rawBus : buses) {
            const GridArea* area = nullptr;
            for (auto* object = rawBus.object->getParent(); object != nullptr;
                 object = object->getParent()) {
                auto* candidate = dynamic_cast<const GridArea*>(object);
                if (candidate != nullptr && candidate != parentObject) {
                    area = candidate;
                    break;
                }
            }
            if (area == nullptr) {
                rawBus.area = 1;
                continue;
            }
            auto found = areaNumbers.find(area);
            if (found == areaNumbers.end()) {
                int areaNumber = static_cast<int>(area->getUserID());
                if ((areaNumber <= 0) ||
                    (areasByNumber.contains(areaNumber) && areasByNumber[areaNumber] != area)) {
                    while (areaIds.contains(nextArea)) {
                        ++nextArea;
                    }
                    areaNumber = nextArea++;
                    warning(area->getName() + " has no unique positive RAW area number; assigned " +
                            std::to_string(areaNumber));
                }
                areaIds.insert(areaNumber);
                areaNumbers.emplace(area, areaNumber);
                areasByNumber.emplace(areaNumber, area);
                areas.push_back({area, areaNumber});
                found = areaNumbers.find(area);
            }
            rawBus.area = found->second;
        }
        if (!areasByNumber.contains(1)) {
            areasByNumber.emplace(1, nullptr);
        }

        for (auto& rawBus : buses) {
            const double zoneValue = rawBus.object->get("zone");
            if (std::isfinite(zoneValue) && zoneValue >= 1.0 && zoneValue <= 999999.0) {
                rawBus.zone = static_cast<int>(std::lround(zoneValue));
            } else {
                rawBus.zone = 1;
            }
        }

        std::vector<RawLoad> loads;
        std::vector<RawGenerator> generators;
        std::unordered_map<index_t, std::size_t> loadIds;
        std::unordered_map<index_t, std::size_t> generatorIds;
        for (const auto& bus : buses) {
            const auto loadCount = static_cast<index_t>(bus.object->get("loadcount"));
            for (index_t loadIndex = 0; loadIndex < loadCount; ++loadIndex) {
                const auto* load = bus.object->getLoad(loadIndex);
                if (load == nullptr) {
                    continue;
                }
                auto& sequence = loadIds[bus.number];
                loads.push_back({&bus, load, rawId(++sequence)});
            }
            const auto genCount = static_cast<index_t>(bus.object->get("gencount"));
            for (index_t genIndex = 0; genIndex < genCount; ++genIndex) {
                const auto* generator = bus.object->getGen(genIndex);
                if (generator == nullptr) {
                    continue;
                }
                auto& sequence = generatorIds[bus.number];
                generators.push_back({&bus, generator, rawId(++sequence)});
            }
        }

        std::vector<RawLink> links;
        std::unordered_map<std::uint64_t, std::size_t> circuitIds;
        const auto linkCount = static_cast<index_t>(parentObject->get("totallinkcount"));
        for (index_t index = 1; index <= linkCount; ++index) {
            const auto* link = dynamic_cast<const Link*>(parentObject->findByUserID("link", index));
            if (link == nullptr) {
                continue;
            }
            const auto* acLine = dynamic_cast<const AcLine*>(link);
            const auto* bus1Object = dynamic_cast<const GridBus*>(link->getBus(1));
            const auto* bus2Object = dynamic_cast<const GridBus*>(link->getBus(2));
            if (acLine == nullptr || bus1Object == nullptr || bus2Object == nullptr ||
                !busLookup.contains(bus1Object) || !busLookup.contains(bus2Object)) {
                warning(link->getName() +
                        " is not a two-terminal AC link between exported buses and was omitted");
                continue;
            }
            const auto* bus1 = busLookup.at(bus1Object);
            const auto* bus2 = busLookup.at(bus2Object);
            const auto lowBus = (std::min)(bus1->number, bus2->number);
            const auto highBus = (std::max)(bus1->number, bus2->number);
            const auto pairKey =
                (static_cast<std::uint64_t>(lowBus) << 32U) ^ static_cast<std::uint64_t>(highBus);
            auto& sequence = circuitIds[pairKey];
            const bool transformer =
                (dynamic_cast<const links::AdjustableTransformer*>(acLine) != nullptr) ||
                (std::abs(acLine->get("tap") - 1.0) > 1.0e-12) ||
                (std::abs(acLine->get("tapangle")) > 1.0e-12);
            links.push_back({link, acLine, bus1, bus2, rawId(++sequence), transformer});
        }

        std::unordered_map<std::uint64_t, std::size_t> remotePlantGeneratorCounts;
        for (const auto& generator : generators) {
            const auto* controlBus = generator.object->getRemoteBus();
            if (controlBus == nullptr || controlBus == generator.bus->object ||
                !busLookup.contains(controlBus)) {
                continue;
            }
            const auto controlNumber = busLookup.at(controlBus)->number;
            const auto key = (static_cast<std::uint64_t>(controlNumber) << 32U) ^
                static_cast<std::uint64_t>(generator.bus->number);
            ++remotePlantGeneratorCounts[key];
        }

        std::ofstream output(fileName);
        if (!output.is_open()) {
            warning("unable to open '" + fileName + "' for writing");
            return false;
        }
        output << std::setprecision(std::numeric_limits<double>::max_digits10);
        double baseFrequency = parentObject->get("basefreq");
        if (!std::isfinite(baseFrequency) || baseFrequency <= 0.0) {
            warning("invalid system frequency; exported the PSS/E default of 60 Hz");
            baseFrequency = 60.0;
        }
        output << "0, " << rawNumber(basePower) << ", 33, 0, 1, " << rawNumber(baseFrequency)
               << " / PSS(R)E-33 GridDyn export\n";
        auto caseName = safeComment(parentObject->getName());
        if (caseName.empty()) {
            caseName = "GridDyn network";
        }
        output << caseName << '\n';
        output << "Normalized static AC network exported by GridDyn\n";

        // PSS/E 33 bus records have no section header before the first bus card.
        for (const auto& bus : buses) {
            const auto* object = bus.object;
            int type = 1;
            if (!object->isEnabled()) {
                type = 4;
            } else if (object->getBusType() == static_cast<int>(GridBus::BusType::PV)) {
                type = 2;
            } else if (object->getBusType() == static_cast<int>(GridBus::BusType::SLK)) {
                type = 3;
            } else if (object->getBusType() == static_cast<int>(GridBus::BusType::AFIX)) {
                warning(object->getName() + " is angle-fixed; exported as a PQ bus");
            } else if (object->getBusType() != static_cast<int>(GridBus::BusType::PQ)) {
                warning(object->getName() + " has an unsupported bus type; exported as a PQ bus");
            }
            auto busName = object->getName();
            if (busName.empty()) {
                busName = "BUS_" + std::to_string(bus.number);
            }
            writeRawRecord(
                output,
                {std::to_string(bus.number),
                 rawQuoted(busName, 12, object->getName(), warning),
                 rawNumber(object->get("basevoltage", units::kV)),
                 std::to_string(type),
                 std::to_string(bus.area),
                 std::to_string(bus.zone),
                 "1",
                 rawNumber(object->get("voltage")),
                 rawNumber(object->get("angle", units::deg)),
                 finiteRawLimit(object->get("vmax"), 1.1, object->getName() + ".vmax", warning),
                 finiteRawLimit(object->get("vmin"), 0.9, object->getName() + ".vmin", warning),
                 finiteRawLimit(
                     object->get("vmax"), 1.1, object->getName() + ".emergency vmax", warning),
                 finiteRawLimit(
                     object->get("vmin"), 0.9, object->getName() + ".emergency vmin", warning)});
        }
        output << "0 / END OF BUS DATA, BEGIN LOAD DATA\n";

        std::vector<std::vector<std::string>> fixedShuntRows;
        for (const auto& load : loads) {
            const auto* object = load.object;
            const auto* zip = dynamic_cast<const ZipLoad*>(object);
            const auto* svd = dynamic_cast<const loads::Svd*>(object);
            const double p = object->get("p", units::MW);
            const double q = object->get("q", units::MVAR);
            const double ip = (zip != nullptr) ? zip->get("ip", units::MW) : 0.0;
            const double iq = (zip != nullptr) ? zip->get("iq", units::MVAR) : 0.0;
            const double yp = (zip != nullptr) ? zip->get("yp", units::MW) : 0.0;
            const double yq = (zip != nullptr) ? zip->get("yq", units::MVAR) : 0.0;
            if (zip == nullptr && svd == nullptr) {
                warning(object->getName() +
                        " is not a ZIP load; exported only its constant-power operating point");
            }
            if (svd != nullptr) {
                warning(object->getName() +
                        " switched-shunt control was reduced to its present admittance");
            }
            if (object->isFixedShunt() || svd != nullptr) {
                if (std::abs(p) > 1.0e-12 || std::abs(q) > 1.0e-12 || std::abs(ip) > 1.0e-12 ||
                    std::abs(iq) > 1.0e-12) {
                    warning(object->getName() +
                            " has non-admittance terms and was exported as a RAW load record");
                } else {
                    fixedShuntRows.push_back({std::to_string(load.bus->number),
                                              rawQuoted(load.id, 2, object->getName(), warning),
                                              object->isEnabled() ? "1" : "0",
                                              rawNumber(yp),
                                              rawNumber(-yq)});
                    continue;
                }
            }
            writeRawRecord(output,
                           {std::to_string(load.bus->number),
                            rawQuoted(load.id, 2, object->getName(), warning),
                            object->isEnabled() ? "1" : "0",
                            std::to_string(load.bus->area),
                            std::to_string(load.bus->zone),
                            rawNumber(p),
                            rawNumber(q),
                            rawNumber(ip),
                            rawNumber(iq),
                            rawNumber(yp),
                            rawNumber(-yq),
                            "1",
                            "1",
                            "0"});
        }
        output << "0 / END OF LOAD DATA, BEGIN FIXED SHUNT DATA\n";
        for (const auto& row : fixedShuntRows) {
            writeRawRecord(output, row);
        }
        // The transformer records below can carry first-winding magnetizing data.
        // Extra second-terminal admittance is emitted here as a bus shunt.
        std::unordered_map<index_t, std::size_t> shuntIds;
        for (const auto& load : loads) {
            if (load.object->isFixedShunt() ||
                dynamic_cast<const loads::Svd*>(load.object) != nullptr) {
                ++shuntIds[load.bus->number];
            }
        }
        for (const auto& link : links) {
            if (!link.transformer) {
                continue;
            }
            const double g2 = link.line->get("g2");
            const double b2 = link.line->get("b2");
            if (std::abs(g2) > 1.0e-12 || std::abs(b2) > 1.0e-12) {
                const auto id = rawId(++shuntIds[link.bus2->number]);
                writeRawRecord(output,
                               {std::to_string(link.bus2->number),
                                rawQuoted(id, 2, link.object->getName(), warning),
                                (link.object->isEnabled() && link.object->isConnected()) ? "1" :
                                                                                           "0",
                                rawNumber(g2),
                                rawNumber(-b2)});
                warning(link.object->getName() +
                        " second-terminal transformer admittance was represented as a bus shunt");
            }
        }
        output << "0 / END OF FIXED SHUNT DATA, BEGIN GENERATOR DATA\n";

        for (const auto& generator : generators) {
            const auto* object = generator.object;
            if (object->getSubObject("genmodel", 0) != nullptr ||
                object->getSubObject("governor", 0) != nullptr ||
                object->getSubObject("exciter", 0) != nullptr) {
                warning(object->getName() + " has dynamic submodels; they were omitted");
            }
            int remoteBus = 0;
            if (const auto* controlBus = object->getRemoteBus();
                controlBus != nullptr && controlBus != generator.bus->object) {
                if (busLookup.contains(controlBus)) {
                    remoteBus = static_cast<int>(busLookup.at(controlBus)->number);
                } else {
                    warning(object->getName() +
                            " regulates a bus outside the exported network; its IREG was cleared");
                }
            }
            double voltageTarget = object->get("vtarget");
            if (!std::isfinite(voltageTarget) || voltageTarget <= 0.0) {
                voltageTarget = generator.bus->object->get("voltage");
            }
            double participation = (remoteBus == 0) ? 100.0 : 100.0 * object->get("vcontrolfrac");
            if (remoteBus != 0) {
                const auto key = (static_cast<std::uint64_t>(remoteBus) << 32U) ^
                    static_cast<std::uint64_t>(generator.bus->number);
                if (const auto count = remotePlantGeneratorCounts.find(key);
                    count != remotePlantGeneratorCounts.end()) {
                    participation *= static_cast<double>(count->second);
                }
            }
            if (!std::isfinite(participation) || participation <= 0.0 || participation > 100.0) {
                warning(object->getName() +
                        ".vcontrolfrac is outside RAW's 0-100 range; "
                        "exported as 100");
                participation = 100.0;
            }
            writeRawRecord(output,
                           {std::to_string(generator.bus->number),
                            rawQuoted(generator.id, 2, object->getName(), warning),
                            rawNumber(-object->get("p") * basePower),
                            rawNumber(-object->get("q") * basePower),
                            finiteRawLimit(object->get("qmax", units::MVAR),
                                           9999.0,
                                           object->getName() + ".qmax",
                                           warning),
                            finiteRawLimit(object->get("qmin", units::MVAR),
                                           -9999.0,
                                           object->getName() + ".qmin",
                                           warning),
                            rawNumber(voltageTarget),
                            std::to_string(remoteBus),
                            rawNumber(object->get("mbase", units::MVAR)),
                            rawNumber(object->get("rs")),
                            rawNumber(object->get("xs")),
                            "0",
                            "0",
                            "1",
                            object->isEnabled() ? "1" : "0",
                            rawNumber(participation),
                            finiteRawLimit(object->get("pmax", units::MW),
                                           9999.0,
                                           object->getName() + ".pmax",
                                           warning),
                            finiteRawLimit(object->get("pmin", units::MW),
                                           -9999.0,
                                           object->getName() + ".pmin",
                                           warning),
                            "1",
                            "1.0",
                            "0",
                            "0.0",
                            "0",
                            "0.0",
                            "0",
                            "0.0",
                            "0",
                            "1.0"});
        }
        output << "0 / END OF GENERATOR DATA, BEGIN BRANCH DATA\n";

        for (const auto& link : links) {
            if (link.transformer) {
                continue;
            }
            const auto* line = link.line;
            const double totalB = line->get("b");
            const double halfB = totalB / 2.0;
            const int status = (link.object->isEnabled() && link.object->isConnected()) ? 1 : 0;
            if (link.object->isEnabled() && !link.object->isConnected()) {
                warning(link.object->getName() +
                        " has an open terminal; RAW branch status disables the whole link");
            }
            writeRawRecord(
                output,
                {std::to_string(link.bus1->number),
                 std::to_string(link.bus2->number),
                 rawQuoted(link.id, 2, link.object->getName(), warning),
                 rawNumber(line->get("r")),
                 rawNumber(line->get("x")),
                 rawNumber(totalB),
                 rawRating(link.object->get("ratinga"),
                           basePower,
                           link.object->getName() + ".ratingA",
                           warning),
                 rawRating(link.object->get("ratingb"),
                           basePower,
                           link.object->getName() + ".ratingB",
                           warning),
                 rawRating(link.object->get("erating"),
                           basePower,
                           link.object->getName() + ".ratingC",
                           warning),
                 rawNumber(line->get("g1")),
                 rawNumber(line->get("b1") - halfB),
                 rawNumber(line->get("g2")),
                 rawNumber(line->get("b2") - halfB),
                 std::to_string(status),
                 "1",
                 rawNumber(line->get("length", units::km)),
                 "1",
                 "1.0",
                 "0",
                 "0.0",
                 "0",
                 "0.0",
                 "0",
                 "0.0",
                 rawQuoted(link.object->getName(), 40, link.object->getName(), warning)});
        }
        output << "0 / END OF BRANCH DATA, BEGIN TRANSFORMER DATA\n";

        for (const auto& link : links) {
            if (!link.transformer) {
                continue;
            }
            const auto* line = link.line;
            const auto* adjustable = dynamic_cast<const links::AdjustableTransformer*>(link.object);
            if (adjustable != nullptr && adjustable->get("control_mode") != 0.0) {
                warning(link.object->getName() +
                        " transformer control was exported at its present tap setting");
            }
            const int status = (link.object->isEnabled() && link.object->isConnected()) ? 1 : 0;
            if (link.object->isEnabled() && !link.object->isConnected()) {
                warning(link.object->getName() +
                        " has an open terminal; RAW transformer status disables the whole link");
            }
            writeRawRecord(output,
                           {std::to_string(link.bus1->number),
                            std::to_string(link.bus2->number),
                            "0",
                            rawQuoted(link.id, 2, link.object->getName(), warning),
                            "1",
                            "1",
                            "1",
                            rawNumber(line->get("g1")),
                            rawNumber(line->get("b1")),
                            "2",
                            rawQuoted(link.object->getName(), 12, link.object->getName(), warning),
                            std::to_string(status),
                            "1",
                            "1.0",
                            "0",
                            "0.0",
                            "0",
                            "0.0",
                            "0",
                            "0.0",
                            rawQuoted("", 12, link.object->getName(), warning)});
            writeRawRecord(output,
                           {rawNumber(line->get("r")),
                            rawNumber(line->get("x")),
                            rawNumber(basePower)});
            writeRawRecord(output,
                           {rawNumber(line->get("tap")),
                            "0",
                            rawNumber(line->get("tapangle", units::deg)),
                            rawRating(link.object->get("ratinga"),
                                      basePower,
                                      link.object->getName() + ".ratingA",
                                      warning),
                            rawRating(link.object->get("ratingb"),
                                      basePower,
                                      link.object->getName() + ".ratingB",
                                      warning),
                            rawRating(link.object->get("erating"),
                                      basePower,
                                      link.object->getName() + ".ratingC",
                                      warning),
                            "0",
                            "0",
                            "0",
                            "0",
                            "1.2",
                            "0.8",
                            "1",
                            "0",
                            "0",
                            "0",
                            "0"});
            writeRawRecord(output, {"1.0", "0"});
        }
        output << "0 / END OF TRANSFORMER DATA, BEGIN AREA DATA\n";

        std::map<int, RawArea> sortedAreas;
        for (const auto& area : areas) {
            sortedAreas.emplace(area.number, area);
        }
        for (const auto& [number, area] : sortedAreas) {
            const auto* object = area.object;
            int slackBus = 0;
            double scheduledMW = 0.0;
            double toleranceMW = 0.0;
            std::string areaName = "AREA_" + std::to_string(number);
            if (object != nullptr) {
                if (const auto* slack = object->getInterchangeSlackBus();
                    slack != nullptr && busLookup.contains(slack)) {
                    slackBus = static_cast<int>(busLookup.at(slack)->number);
                }
                if (const auto schedule = object->getScheduledNetInterchangeMW()) {
                    scheduledMW = *schedule;
                }
                if (const auto tolerance = object->getInterchangeToleranceMW()) {
                    toleranceMW = *tolerance;
                }
                areaName = object->getName();
            }
            writeRawRecord(output,
                           {std::to_string(number),
                            std::to_string(slackBus),
                            rawNumber(scheduledMW),
                            rawNumber(toleranceMW),
                            rawQuoted(areaName, 12, areaName, warning)});
        }
        if (!sortedAreas.contains(1)) {
            writeRawRecord(output,
                           {"1", "0", "0", "0", rawQuoted("AREA_1", 12, "AREA_1", warning)});
        }
        output << "0 / END OF AREA DATA, BEGIN TWO-TERMINAL DC DATA\n";
        output << "0 / END OF TWO-TERMINAL DC DATA, BEGIN VSC DC LINE DATA\n";
        output << "0 / END OF VSC DC LINE DATA, BEGIN IMPEDANCE CORRECTION DATA\n";
        output << "0 / END OF IMPEDANCE CORRECTION DATA, BEGIN MULTI-TERMINAL DC DATA\n";
        output << "0 / END OF MULTI-TERMINAL DC DATA, BEGIN MULTI-SECTION LINE DATA\n";
        output << "0 / END OF MULTI-SECTION LINE DATA, BEGIN ZONE DATA\n";
        output << "0 / END OF ZONE DATA, BEGIN INTER-AREA TRANSFER DATA\n";
        if (const auto* rootArea = dynamic_cast<const GridArea*>(parentObject->getRoot())) {
            for (const auto& transfer : rootArea->getInterAreaTransfers()) {
                writeRawRecord(
                    output,
                    {std::to_string(transfer.fromAreaID),
                     std::to_string(transfer.toAreaID),
                     rawQuoted(transfer.transferID, 2, "inter-area transfer ID", warning),
                     rawNumber(transfer.scheduledMW)});
            }
        }
        output << "0 / END OF INTER-AREA TRANSFER DATA, BEGIN OWNER DATA\n";
        output << "0 / END OF OWNER DATA, BEGIN FACTS DEVICE DATA\n";
        output << "0 / END OF FACTS DEVICE DATA, BEGIN SWITCHED SHUNT DATA\n";
        output << "0 / END OF SWITCHED SHUNT DATA, BEGIN GNE DATA\n";
        output << "0 / END OF GNE DATA, BEGIN INDUCTION MACHINE DATA\n";
        output << "0 / END OF INDUCTION MACHINE DATA\n";
        output.flush();
        if (!output.good()) {
            warning("failed while writing '" + fileName + "'");
            return false;
        }
        return true;
    }
}  // namespace

bool saveRawCase(const CoreObject* parentObject, const std::string& fileName, stringVec* warnings)
{
    return saveRawV33(parentObject, fileName, warnings);
}
}  // namespace griddyn
