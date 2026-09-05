/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/loads/ZipLoad.h"
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace griddyn {
namespace {
void warning(stringVec* warnings, const std::string& message)
{
    const std::string fullMessage = "PYPOWER export warning: " + message;
    std::cerr << fullMessage << '\n';
    if (warnings != nullptr) { warnings->push_back(fullMessage); }
}
double finiteLimit(double value, double fallback, stringVec* warnings, const std::string& label)
{
    if (!std::isfinite(value) || std::abs(value) > 1.0e12) {
        warning(warnings, label + " is unbounded; exported as " + std::to_string(fallback));
        return fallback;
    }
    return value;
}
void row(std::ofstream& output, const std::vector<double>& values)
{
    output << "        [";
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) { output << ", "; }
        output << values[index];
    }
    output << "],\n";
}
}

bool savePyPowerCase(const CoreObject* parentObject,
                     const std::string& fileName,
                     stringVec* warnings)
{
    if (warnings != nullptr) { warnings->clear(); }
    std::ofstream output(fileName);
    if (!output.is_open()) {
        warning(warnings, "unable to open '" + fileName + "' for writing");
        return false;
    }

    const auto busCount = static_cast<index_t>(parentObject->get("totalbuscount"));
    std::vector<const GridBus*> buses;
    std::unordered_map<const GridBus*, index_t> busNumbers;
    for (index_t index = 1; index <= busCount; ++index) {
        auto* bus = dynamic_cast<const GridBus*>(parentObject->findByUserID("bus", index));
        if (bus == nullptr) {
            warning(warnings, "bus slot " + std::to_string(index) + " is not an AC bus and was omitted");
            continue;
        }
        index_t busNumber = bus->getUserID();
        if ((busNumber == 0) || busNumbers.contains(bus)) {
            busNumber = static_cast<index_t>(buses.size() + 1);
            warning(warnings, bus->getName() + " has no usable unique bus ID; assigned " + std::to_string(busNumber));
        }
        buses.push_back(bus);
        busNumbers.emplace(bus, busNumber);
    }
    if (buses.empty()) {
        warning(warnings, "no exportable AC buses were found");
        return false;
    }

    std::string functionName = std::filesystem::path(fileName).stem().string();
    for (char& character : functionName) {
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') { character = '_'; }
    }
    if (functionName.empty() || std::isdigit(static_cast<unsigned char>(functionName.front()))) { functionName.insert(0, "case_"); }
    output << "from numpy import array\n\ndef " << functionName << "():\n    ppc = {\"version\": \"2\"}\n";
    output << "    ppc[\"baseMVA\"] = " << parentObject->get("basepower", units::MW) << "\n";
    output << "    ppc[\"bus\"] = array([\n";
    for (const auto* bus : buses) {
        double pd = 0.0, qd = 0.0, gs = 0.0, bs = 0.0;
        const auto loadCount = static_cast<index_t>(bus->get("loadcount"));
        for (index_t loadIndex = 0; loadIndex < loadCount; ++loadIndex) {
            auto* load = bus->getLoad(loadIndex);
            if (load == nullptr) { continue; }
            pd += load->get("p", units::MW);
            qd += load->get("q", units::MVAR);
            if (const auto* zip = dynamic_cast<const ZipLoad*>(load)) {
                gs += zip->get("yp", units::MW);
                bs -= zip->get("yq", units::MVAR);
                if (std::abs(zip->get("ip", units::MW)) > 1e-12 || std::abs(zip->get("iq", units::MVAR)) > 1e-12) {
                    warning(warnings, zip->getName() + " has constant-current load terms; they were omitted");
                }
            } else {
                warning(warnings, load->getName() + " is not a ZIP/constant load; exported only its P/Q operating point");
            }
        }
        int type = bus->getBusType();
        if (type == 1) { warning(warnings, bus->getName() + " is angle-fixed; exported as a PQ bus"); type = 1; }
        else if (type == 0) { type = 1; }
        else if (type == 2) { type = 2; }
        else if (type == 3) { type = 3; }
        else { warning(warnings, bus->getName() + " has an unsupported bus type; exported as PQ"); type = 1; }
        row(output, {static_cast<double>(busNumbers.at(bus)), static_cast<double>(type), pd, qd, gs, bs, 1.0,
                     bus->get("voltage"), bus->get("angle", units::deg), bus->get("basevoltage"), 1.0,
                     bus->get("vmax"), bus->get("vmin")});
    }
    const double basePower = parentObject->get("basepower", units::MW);
    output << "    ])\n    ppc[\"gen\"] = array([\n";
    for (const auto* bus : buses) {
        const auto genCount = static_cast<index_t>(bus->get("gencount"));
        for (index_t genIndex = 0; genIndex < genCount; ++genIndex) {
            auto* gen = bus->getGen(genIndex);
            if (gen == nullptr) { continue; }
            if (gen->getSubObject("genmodel", 0) != nullptr || gen->getSubObject("governor", 0) != nullptr || gen->getSubObject("exciter", 0) != nullptr) {
                warning(warnings, gen->getName() + " has dynamic submodels; they were omitted");
            }
            const double vtarget = gen->get("vtarget");
            row(output, {static_cast<double>(busNumbers.at(bus)), -gen->get("p") * basePower, -gen->get("q") * basePower,
                         finiteLimit(gen->get("qmax", units::MVAR), 1.0e6, warnings, gen->getName() + ".qmax"),
                         finiteLimit(gen->get("qmin", units::MVAR), -1.0e6, warnings, gen->getName() + ".qmin"),
                         (vtarget > 0.0) ? vtarget : bus->get("voltage"), basePower,
                         gen->isEnabled() ? 1.0 : 0.0,
                         finiteLimit(gen->get("pmax", units::MW), 1.0e6, warnings, gen->getName() + ".pmax"),
                         finiteLimit(gen->get("pmin", units::MW), -1.0e6, warnings, gen->getName() + ".pmin")});
        }
    }
    output << "    ])\n    ppc[\"branch\"] = array([\n";
    const auto linkCount = static_cast<index_t>(parentObject->get("totallinkcount"));
    for (index_t linkIndex = 1; linkIndex <= linkCount; ++linkIndex) {
        auto* link = dynamic_cast<const Link*>(parentObject->findByUserID("link", linkIndex));
        if (link == nullptr) { continue; }
        const auto* acLine = dynamic_cast<const AcLine*>(link);
        const auto bus1 = link->getBus(1); const auto bus2 = link->getBus(2);
        if (acLine == nullptr || bus1 == nullptr || bus2 == nullptr || !busNumbers.contains(bus1) || !busNumbers.contains(bus2)) {
            warning(warnings, link->getName() + " is not a two-terminal AC branch between exported buses; it was omitted");
            continue;
        }
        const double ratingA = link->get("ratinga") * basePower;
        row(output, {static_cast<double>(busNumbers.at(bus1)), static_cast<double>(busNumbers.at(bus2)), link->get("r"), link->get("x"), link->get("b"),
                     (ratingA > 1.0e12) ? 0.0 : ratingA, 0.0, 0.0, link->get("tap"), link->get("tapangle", units::deg),
                     link->isConnected() ? 1.0 : 0.0, -360.0, 360.0});
    }
    output << "    ])\n    return ppc\n";
    return output.good();
}
}  // namespace griddyn