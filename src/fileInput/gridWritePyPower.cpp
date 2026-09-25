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
#include "griddyn/MatPowerCostCurve.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/loads/ZipLoad.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace griddyn {
namespace {
    enum class CaseFormat : std::uint8_t { PYPOWER, MATPOWER };

    const char* formatName(CaseFormat format)
    {
        return (format == CaseFormat::PYPOWER) ? "PYPOWER" : "MATPOWER";
    }

    bool savePowerFlowCase(const CoreObject* parentObject,
                           const std::string& fileName,
                           stringVec* warnings,
                           CaseFormat format)
    {
        if (warnings != nullptr) {
            warnings->clear();
        }
        const auto warning = [warnings, format](const std::string& message) {
            const std::string fullMessage =
                std::string{formatName(format)} + " export warning: " + message;
            if (warnings != nullptr) {
                warnings->push_back(fullMessage);
            } else {
                std::cerr << fullMessage << '\n';
            }
        };
        std::ofstream output(fileName);
        if (!output.is_open()) {
            warning("unable to open '" + fileName + "' for writing");
            return false;
        }
        output << std::setprecision(std::numeric_limits<double>::max_digits10);
        const auto row = [&output, format](const std::vector<double>& values) {
            if (format == CaseFormat::PYPOWER) {
                output << "        [";
                for (size_t index = 0; index < values.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << values[index];
                }
                output << "],\n";
            } else {
                output << "    ";
                for (size_t index = 0; index < values.size(); ++index) {
                    if (index != 0) {
                        output << "\t";
                    }
                    output << values[index];
                }
                output << ";\n";
            }
        };
        const auto finiteLimit =
            [&warning](double value, double fallback, const std::string& label) {
                if (!std::isfinite(value) || std::abs(value) > 1.0e12) {
                    warning(label + " is unbounded; exported as " + std::to_string(fallback));
                    return fallback;
                }
                return value;
            };

        const auto busCount = static_cast<index_t>(parentObject->get("totalbuscount"));
        std::vector<const GridBus*> buses;
        std::unordered_map<const GridBus*, index_t> busNumbers;
        std::unordered_set<index_t> usedBusNumbers;
        for (index_t index = 1; index <= busCount; ++index) {
            const auto* bus =
                dynamic_cast<const GridBus*>(parentObject->findByUserID("bus", index));
            if (bus == nullptr) {
                warning("bus slot " + std::to_string(index) + " is not an AC bus and was omitted");
                continue;
            }
            index_t busNumber = bus->getUserID();
            if ((busNumber == 0) || !usedBusNumbers.insert(busNumber).second) {
                busNumber = static_cast<index_t>(buses.size() + 1);
                while (usedBusNumbers.contains(busNumber)) {
                    ++busNumber;
                }
                usedBusNumbers.insert(busNumber);
                warning(bus->getName() + " has no usable unique bus ID; assigned " +
                        std::to_string(busNumber));
            }
            buses.push_back(bus);
            busNumbers.emplace(bus, busNumber);
        }
        if (buses.empty()) {
            warning("no exportable AC buses were found");
            return false;
        }

        std::string functionName = std::filesystem::path(fileName).stem().string();
        for (char& character : functionName) {
            if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_') {
                character = '_';
            }
        }
        if (functionName.empty() ||
            std::isdigit(static_cast<unsigned char>(functionName.front())) != 0) {
            functionName.insert(0, "case_");
        }
        const double basePower = parentObject->get("basepower", units::MW);
        const auto busAreaNumber = [&warning](const GridBus* bus) {
            for (auto* object = bus->getParent(); object != nullptr; object = object->getParent()) {
                if (const auto* area = dynamic_cast<const GridArea*>(object)) {
                    const auto areaID = area->getUserID();
                    if (areaID > 0) {
                        return static_cast<double>(areaID);
                    }
                    warning(area->getName() + " has no positive area ID; exported as area 1");
                    return 1.0;
                }
            }
            return 1.0;
        };
        const auto branchRatingMW = [&warning, basePower](double ratingPU,
                                                          const std::string& label) {
            if (!std::isfinite(ratingPU)) {
                warning(label + " is not finite; exported as an unlimited rating");
                return 0.0;
            }
            if (ratingPU < 0.0) {
                warning(label + " is negative; exported as an unlimited rating");
                return 0.0;
            }
            if (basePower <= 0.0 || ratingPU >= 1.0e12 / basePower) {
                return 0.0;
            }
            return ratingPU * basePower;
        };
        if (format == CaseFormat::PYPOWER) {
            output << "from numpy import array\n\ndef " << functionName
                   << "():\n    ppc = {\"version\": \"2\"}\n";
            output << "    ppc[\"baseMVA\"] = " << basePower << "\n    ppc[\"bus\"] = array([\n";
        } else {
            output << "function mpc = " << functionName
                   << "\n% MATPOWER version 2 case generated by GridDyn\n";
            output << "mpc.version = '2';\nmpc.baseMVA = " << basePower << ";\n\nmpc.bus = [\n";
        }
        for (const auto* bus : buses) {
            double activePowerDemand = 0.0;
            double reactivePowerDemand = 0.0;
            double shuntConductance = 0.0;
            double shuntSusceptance = 0.0;
            const auto loadCount = static_cast<index_t>(bus->get("loadcount"));
            for (index_t loadIndex = 0; loadIndex < loadCount; ++loadIndex) {
                auto* load = bus->getLoad(loadIndex);
                if (load == nullptr) {
                    continue;
                }
                activePowerDemand += load->get("p", units::MW);
                reactivePowerDemand += load->get("q", units::MVAR);
                if (const auto* zip = dynamic_cast<const ZipLoad*>(load)) {
                    shuntConductance += zip->get("yp", units::MW);
                    shuntSusceptance -= zip->get("yq", units::MVAR);
                    if (std::abs(zip->get("ip", units::MW)) > 1e-12 ||
                        std::abs(zip->get("iq", units::MVAR)) > 1e-12) {
                        warning(zip->getName() +
                                " has constant-current load terms; they were omitted");
                    }
                } else {
                    warning(load->getName() +
                            " is not a ZIP/constant load; exported only its P/Q operating point");
                }
            }
            int type = bus->getBusType();
            if (!bus->isEnabled()) {
                type = 4;
            } else if (type == 1) {
                warning(bus->getName() + " is angle-fixed; exported as a PQ bus");
                type = 1;
            } else if (type == 0) {
                type = 1;
            } else if (type == 2) {
                type = 2;
            } else if (type == 3) {
                type = 3;
            } else {
                warning(bus->getName() + " has an unsupported bus type; exported as PQ");
                type = 1;
            }
            row({static_cast<double>(busNumbers.at(bus)),
                 static_cast<double>(type),
                 activePowerDemand,
                 reactivePowerDemand,
                 shuntConductance,
                 shuntSusceptance,
                 busAreaNumber(bus),
                 bus->get("voltage"),
                 bus->get("angle", units::deg),
                 bus->get("basevoltage"),
                 bus->get("zone"),
                 bus->get("vmax"),
                 bus->get("vmin")});
        }
        output << ((format == CaseFormat::PYPOWER) ? "    ])\n    ppc[\"gen\"] = array([\n" :
                                                     "];\n\nmpc.gen = [\n");
        std::vector<const Generator*> generators;
        for (const auto* bus : buses) {
            const auto genCount = static_cast<index_t>(bus->get("gencount"));
            for (index_t genIndex = 0; genIndex < genCount; ++genIndex) {
                auto* gen = bus->getGen(genIndex);
                if (gen == nullptr) {
                    continue;
                }
                if (gen->getSubObject("genmodel", 0) != nullptr ||
                    gen->getSubObject("governor", 0) != nullptr ||
                    gen->getSubObject("exciter", 0) != nullptr) {
                    warning(gen->getName() + " has dynamic submodels; they were omitted");
                }
                generators.push_back(gen);
                const double vtarget = gen->get("vtarget");
                std::vector<double> generatorRow{
                    static_cast<double>(busNumbers.at(bus)),
                    -gen->get("p") * basePower,
                    -gen->get("q") * basePower,
                    finiteLimit(gen->get("qmax", units::MVAR), 1.0e6, gen->getName() + ".qmax"),
                    finiteLimit(gen->get("qmin", units::MVAR), -1.0e6, gen->getName() + ".qmin"),
                    (vtarget > 0.0) ? vtarget : bus->get("voltage"),
                    finiteLimit(gen->get("mbase", units::MVAR),
                                basePower,
                                gen->getName() + ".mbase"),
                    gen->isEnabled() ? 1.0 : 0.0,
                    finiteLimit(gen->get("pmax", units::MW), 1.0e6, gen->getName() + ".pmax"),
                    finiteLimit(gen->get("pmin", units::MW), -1.0e6, gen->getName() + ".pmin")};
                const auto& capabilityP = gen->getCapabilityPowerPoints();
                const auto& capabilityQmin = gen->getCapabilityQminPoints();
                const auto& capabilityQmax = gen->getCapabilityQmaxPoints();
                if (gen->checkFlag(Generator::USE_CAPABILITY_CURVE)) {
                    if (capabilityP.size() >= 2 && capabilityP.size() == capabilityQmin.size() &&
                        capabilityP.size() == capabilityQmax.size()) {
                        const auto appendCapabilityPoint =
                            [&generatorRow, &capabilityP, basePower](std::size_t index) {
                                generatorRow.push_back(units::convert(
                                    capabilityP[index], units::puMW, units::MW, basePower));
                            };
                        // MATPOWER stores two ends for its linear capability envelope. GridDyn
                        // accepts additional points; retain the endpoints and report any lost
                        // interior shape rather than silently implying an exact conversion.
                        appendCapabilityPoint(0);
                        appendCapabilityPoint(capabilityP.size() - 1);
                        generatorRow.push_back(units::convert(
                            capabilityQmin.front(), units::puMW, units::MVAR, basePower));
                        generatorRow.push_back(units::convert(
                            capabilityQmax.front(), units::puMW, units::MVAR, basePower));
                        generatorRow.push_back(units::convert(
                            capabilityQmin.back(), units::puMW, units::MVAR, basePower));
                        generatorRow.push_back(units::convert(
                            capabilityQmax.back(), units::puMW, units::MVAR, basePower));
                        if (capabilityP.size() > 2) {
                            warning(gen->getName() +
                                    " has interior capability-curve points; MATPOWER export keeps "
                                    "only the endpoints");
                        }
                    } else {
                        warning(gen->getName() +
                                " uses a capability curve without exportable points; the curve "
                                "was omitted");
                        generatorRow.insert(generatorRow.end(), 6, 0.0);
                    }
                } else {
                    generatorRow.insert(generatorRow.end(), 6, 0.0);
                }
                generatorRow.push_back(gen->get("rampagc"));
                generatorRow.push_back(gen->get("ramp10"));
                generatorRow.push_back(gen->get("ramp30"));
                generatorRow.push_back(gen->get("rampq"));
                generatorRow.push_back(gen->get("participation"));
                row(generatorRow);
            }
        }
        bool hasCostData = false;
        bool hasReactiveCostData = false;
        const auto* costProvider =
            dynamic_cast<const MatPowerCostCurveProvider*>(parentObject->getRoot());
        const auto getCostCurve = [costProvider](const Generator* generator, bool reactive) {
            return (costProvider != nullptr) ?
                costProvider->matPowerCostCurve(generator, reactive) :
                nullptr;
        };
        for (const auto* generator : generators) {
            const auto* activeCost = getCostCurve(generator, false);
            const auto* reactiveCost = getCostCurve(generator, true);
            hasCostData = hasCostData || (activeCost != nullptr && activeCost->present()) ||
                (reactiveCost != nullptr && reactiveCost->present());
            hasReactiveCostData =
                hasReactiveCostData || (reactiveCost != nullptr && reactiveCost->present());
        }
        if (hasCostData) {
            std::size_t costColumnCount = 5;
            for (const auto* generator : generators) {
                const auto* activeCost = getCostCurve(generator, false);
                const auto* reactiveCost = getCostCurve(generator, true);
                if (activeCost != nullptr && activeCost->valid()) {
                    costColumnCount =
                        std::max(costColumnCount, activeCost->coefficients.size() + 4);
                }
                if (hasReactiveCostData && reactiveCost != nullptr && reactiveCost->valid()) {
                    costColumnCount =
                        std::max(costColumnCount, reactiveCost->coefficients.size() + 4);
                }
            }
            output << ((format == CaseFormat::PYPOWER) ?
                           "    ])\n    ppc[\"gencost\"] = array([\n" :
                           "];\n\nmpc.gencost = [\n");
            const auto writeCostCurve = [&row,
                                         &warning,
                                         costColumnCount](const Generator* generator,
                                                          const MatPowerCostCurve* curve,
                                                          bool reactive) {
                if (curve == nullptr || !curve->valid()) {
                    warning(generator->getName() + " has no valid " +
                            (reactive ? "reactive" : "active") +
                            " cost curve; exported with a zero-cost curve");
                    std::vector<double> zeroCost{2.0, 0.0, 0.0, 1.0, 0.0};
                    zeroCost.resize(costColumnCount, 0.0);
                    row(zeroCost);
                    return;
                }
                std::vector<double> values;
                values.reserve(curve->coefficients.size() + 4);
                values.push_back(static_cast<double>(curve->model));
                values.push_back(curve->startupCost);
                values.push_back(curve->shutdownCost);
                const auto coefficientCount =
                    static_cast<double>(curve->coefficients.size());
                const double costTermCount =
                    (curve->model == 1) ? coefficientCount / 2.0 : coefficientCount;
                values.push_back(costTermCount);
                values.insert(values.end(), curve->coefficients.begin(), curve->coefficients.end());
                values.resize(costColumnCount, 0.0);
                row(values);
            };
            for (const auto* generator : generators) {
                writeCostCurve(generator, getCostCurve(generator, false), false);
            }
            if (hasReactiveCostData) {
                for (const auto* generator : generators) {
                    writeCostCurve(generator, getCostCurve(generator, true), true);
                }
            }
            output << ((format == CaseFormat::PYPOWER) ? "    ])\n    ppc[\"branch\"] = array([\n" :
                                                         "];\n\nmpc.branch = [\n");
        } else {
            output << ((format == CaseFormat::PYPOWER) ? "    ])\n    ppc[\"branch\"] = array([\n" :
                                                         "];\n\nmpc.branch = [\n");
        }
        const auto linkCount = static_cast<index_t>(parentObject->get("totallinkcount"));
        for (index_t linkIndex = 1; linkIndex <= linkCount; ++linkIndex) {
            const auto* link =
                dynamic_cast<const Link*>(parentObject->findByUserID("link", linkIndex));
            if (link == nullptr) {
                continue;
            }
            const auto* acLine = dynamic_cast<const AcLine*>(link);
            auto* const bus1 = link->getBus(1);
            auto* const bus2 = link->getBus(2);
            if (acLine == nullptr || bus1 == nullptr || bus2 == nullptr ||
                !busNumbers.contains(bus1) || !busNumbers.contains(bus2)) {
                warning(link->getName() +
                        " is not a two-terminal AC branch between exported buses; it was omitted");
                continue;
            }
            const double ratingA =
                branchRatingMW(link->get("ratinga"), link->getName() + ".ratingA");
            const double ratingB =
                branchRatingMW(link->get("ratingb"), link->getName() + ".ratingB");
            const double ratingC =
                branchRatingMW(link->get("erating"), link->getName() + ".ratingC");
            double minAngle = units::convert(acLine->get("minangle"), units::rad, units::deg);
            double maxAngle = units::convert(acLine->get("maxangle"), units::rad, units::deg);
            if (!std::isfinite(minAngle) || !std::isfinite(maxAngle)) {
                warning(link->getName() + " has a non-finite angle limit; exported as unbounded");
                minAngle = 0.0;
                maxAngle = 0.0;
            } else if (minAngle <= -360.0 && maxAngle >= 360.0) {
                // MATPOWER/PYPOWER represents an unrestricted angle difference as [0, 0].
                minAngle = 0.0;
                maxAngle = 0.0;
            }
            row({static_cast<double>(busNumbers.at(bus1)),
                 static_cast<double>(busNumbers.at(bus2)),
                 link->get("r"),
                 link->get("x"),
                 link->get("b"),
                 ratingA,
                 ratingB,
                 ratingC,
                 link->get("tap"),
                 units::convert(acLine->get("tapangle"), units::rad, units::deg),
                 (link->isEnabled() && link->isConnected()) ? 1.0 : 0.0,
                 minAngle,
                 maxAngle});
        }
        output << ((format == CaseFormat::PYPOWER) ? "    ])\n    return ppc\n" : "];\n");
        output.flush();
        if (!output.good()) {
            warning("failed while writing '" + fileName + "'");
            return false;
        }
        return output.good();
    }
}  // namespace

bool savePyPowerCase(const CoreObject* parentObject,
                     const std::string& fileName,
                     stringVec* warnings)
{
    return savePowerFlowCase(parentObject, fileName, warnings, CaseFormat::PYPOWER);
}

bool saveMatPowerCase(const CoreObject* parentObject,
                      const std::string& fileName,
                      stringVec* warnings)
{
    return savePowerFlowCase(parentObject, fileName, warnings, CaseFormat::MATPOWER);
}
}  // namespace griddyn
