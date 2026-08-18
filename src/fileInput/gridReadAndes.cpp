/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridReadAndes.h"

#include "core/CoreExceptions.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/links/VSCShunt.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include "nlohmann/json.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace griddyn {
namespace {
    using Json = nlohmann::json;

    std::string indexKey(const Json& record, std::string_view field = "idx")
    {
        if (!record.contains(field) || record[field].is_null()) {
            return {};
        }
        if (record[field].is_string()) {
            return record[field].get<std::string>();
        }
        return record[field].dump();
    }

    std::string objectName(const Json& record, std::string_view fallback)
    {
        if (record.contains("name") && record["name"].is_string() && !record["name"].empty()) {
            return record["name"].get<std::string>();
        }
        return std::string{fallback} + "_" + indexKey(record);
    }

    double number(const Json& record, std::string_view field, double defaultValue = 0.0)
    {
        if (!record.contains(field) || record[field].is_null()) {
            return defaultValue;
        }
        return record[field].get<double>();
    }

    template<class Object>
    void setIfPresent(Object* object,
                      const Json& record,
                      std::string_view source,
                      std::string_view target)
    {
        if (record.contains(source) && !record[source].is_null()) {
            object->set(target, number(record, source));
        }
    }
}  // namespace

bool loadAndesJson(CoreObject* parentObject, const std::string& fileName)
{
    std::ifstream input(fileName);
    if (!input.is_open()) {
        return false;
    }

    Json document;
    try {
        input >> document;
    }
    catch (const Json::parse_error&) {
        return false;
    }

    // Node is the distinguishing ANDES DC-topology section.  Do not claim
    // ordinary GridDyn JSON files that happen to contain a similarly named field.
    if (!document.is_object() || !document.contains("Node") || !document["Node"].is_array()) {
        return false;
    }
    if (parentObject == nullptr) {
        throw(InvalidParameterValue("ANDES JSON import requires a simulation parent"));
    }

    std::unordered_map<std::string, GridBus*> acBuses;
    std::unordered_map<std::string, double> acBaseVoltages;
    if (document.contains("Bus") && document["Bus"].is_array()) {
        for (const auto& record : document["Bus"]) {
            auto* bus = new AcBus(objectName(record, "Bus"));
            setIfPresent(bus, record, "Vn", "basevoltage");
            setIfPresent(bus, record, "v0", "voltage");
            setIfPresent(bus, record, "a0", "angle");
            parentObject->add(bus);
            acBuses.emplace(indexKey(record), bus);
            acBaseVoltages.emplace(indexKey(record), number(record, "Vn", 0.0));
        }
    }

    // ANDES supplies static injections separately from its AC bus records.
    // Map those power-flow objects before adding the network branches.
    if (document.contains("PQ") && document["PQ"].is_array()) {
        for (const auto& record : document["PQ"]) {
            const auto bus = acBuses.find(indexKey(record, "bus"));
            if (bus == acBuses.end()) {
                continue;
            }
            auto* load =
                new ZipLoad(number(record, "p0"), number(record, "q0"), objectName(record, "PQ"));
            bus->second->add(load);
        }
    }
    if (document.contains("PV") && document["PV"].is_array()) {
        for (const auto& record : document["PV"]) {
            const auto bus = acBuses.find(indexKey(record, "bus"));
            if (bus == acBuses.end()) {
                continue;
            }
            bus->second->set("type", "pv");
            setIfPresent(bus->second, record, "v0", "vtarget");
            auto* generator = new Generator(objectName(record, "PV"));
            generator->set("p", number(record, "p0"));
            bus->second->add(generator);
        }
    }
    if (document.contains("Slack") && document["Slack"].is_array()) {
        for (const auto& record : document["Slack"]) {
            const auto bus = acBuses.find(indexKey(record, "bus"));
            if (bus == acBuses.end()) {
                continue;
            }
            bus->second->set("type", "swing");
            setIfPresent(bus->second, record, "v0", "vtarget");
            setIfPresent(bus->second, record, "a0", "atarget");
            auto* generator = new Generator(objectName(record, "Slack"));
            generator->set("p", number(record, "p0"));
            generator->set("q", number(record, "q0"));
            bus->second->add(generator);
        }
    }
    if (document.contains("Line") && document["Line"].is_array()) {
        for (const auto& record : document["Line"]) {
            const auto first = acBuses.find(indexKey(record, "bus1"));
            const auto second = acBuses.find(indexKey(record, "bus2"));
            if ((first == acBuses.end()) || (second == acBuses.end())) {
                continue;
            }
            auto* line = new AcLine(objectName(record, "Line"));
            setIfPresent(line, record, "r", "r");
            setIfPresent(line, record, "x", "x");
            setIfPresent(line, record, "b", "b");
            setIfPresent(line, record, "tap", "tap");
            setIfPresent(line, record, "phi", "tapangle");
            line->updateBus(first->second, 1);
            line->updateBus(second->second, 2);
            if (number(record, "u", 1.0) == 0.0) {
                line->disable();
            }
            parentObject->add(line);
        }
    }

    std::unordered_map<std::string, DcBus*> dcBuses;
    for (const auto& record : document["Node"]) {
        auto* bus = new DcBus(objectName(record, "Node"));
        setIfPresent(bus, record, "Vdcn", "basevoltage");
        setIfPresent(bus, record, "v0", "voltage");
        parentObject->add(bus);
        dcBuses.emplace(indexKey(record), bus);
    }

    if (document.contains("Ground") && document["Ground"].is_array()) {
        for (const auto& record : document["Ground"]) {
            const auto node = indexKey(record, "node");
            const auto found = dcBuses.find(node);
            if (found != dcBuses.end()) {
                found->second->set("type", "swing");
                setIfPresent(found->second, record, "voltage", "voltage");
            }
        }
    }

    const std::array<std::pair<std::string_view, std::string_view>, 8> branchModels{{
        {"R", "r"},
        {"L", "l"},
        {"C", "c"},
        {"RLs", "rls"},
        {"RCp", "rcp"},
        {"RLCp", "rlcp"},
        {"RCs", "rcs"},
        {"RLCs", "rlcs"},
    }};
    for (const auto& [section, model] : branchModels) {
        if (!document.contains(section) || !document[section].is_array()) {
            continue;
        }
        for (const auto& record : document[section]) {
            const auto first = dcBuses.find(indexKey(record, "node1"));
            const auto second = dcBuses.find(indexKey(record, "node2"));
            if ((first == dcBuses.end()) || (second == dcBuses.end())) {
                continue;
            }
            auto* branch = new links::DcLink(objectName(record, section));
            branch->set("model", model);
            branch->set("andes_current_balance", true);
            setIfPresent(branch, record, "R", "r");
            setIfPresent(branch, record, "L", "l");
            setIfPresent(branch, record, "C", "c");
            // ANDES' R model has no implicit inductance; GridDyn's historical
            // DcLink default does, so explicitly remove it during conversion.
            if (model == "r") {
                branch->set("l", 0.0);
            }
            branch->updateBus(first->second, 1);
            branch->updateBus(second->second, 2);
            parentObject->add(branch);
        }
    }

    if (document.contains("VSCShunt") && document["VSCShunt"].is_array()) {
        for (const auto& record : document["VSCShunt"]) {
            const auto ac = acBuses.find(indexKey(record, "bus"));
            const auto dc = dcBuses.find(indexKey(record, "node1"));
            const auto dcReference = dcBuses.find(indexKey(record, "node2"));
            if ((ac == acBuses.end()) || (dc == dcBuses.end()) || (dcReference == dcBuses.end())) {
                continue;
            }
            auto* converter = new links::VSCShunt(objectName(record, "VSCShunt"));
            converter->set("andes_current_balance", true);
            // In Andes, rsh and xsh are impedance parameters (z=True).  They
            // are converted to the AC bus base using (VSC Vn / Bus Vn)^2.
            // GridDyn stores the values directly on the bus base, so carry
            // out that conversion while importing the original JSON values.
            const auto baseVoltage = acBaseVoltages.find(indexKey(record, "bus"));
            const auto busVn = (baseVoltage != acBaseVoltages.end()) ? baseVoltage->second : 0.0;
            const auto vscVn = number(record, "Vn", busVn);
            const auto impedanceScale =
                (busVn > 0.0 && vscVn > 0.0) ? std::pow(vscVn / busVn, 2) : 1.0;
            if (record.contains("rsh") && !record["rsh"].is_null()) {
                converter->set("r", number(record, "rsh") * impedanceScale);
            }
            if (record.contains("xsh") && !record["xsh"].is_null()) {
                converter->set("x", number(record, "xsh") * impedanceScale);
            }
            setIfPresent(converter, record, "control", "control");
            setIfPresent(converter, record, "v0", "v0");
            setIfPresent(converter, record, "p0", "p0");
            setIfPresent(converter, record, "q0", "q0");
            setIfPresent(converter, record, "vdc0", "vdc0");
            setIfPresent(converter, record, "k0", "k0");
            setIfPresent(converter, record, "k1", "k1");
            setIfPresent(converter, record, "k2", "k2");
            setIfPresent(converter, record, "droop", "droop");
            setIfPresent(converter, record, "K", "k");
            setIfPresent(converter, record, "vhigh", "vhigh");
            setIfPresent(converter, record, "vlow", "vlow");
            setIfPresent(converter, record, "vshmax", "vshmax");
            setIfPresent(converter, record, "vshmin", "vshmin");
            setIfPresent(converter, record, "Ishmax", "ishmax");
            converter->updateBus(ac->second, 1);
            converter->updateBus(dc->second, 2);
            converter->updateBus(dcReference->second, 3);
            parentObject->add(converter);
        }
    }
    return true;
}
}  // namespace griddyn
