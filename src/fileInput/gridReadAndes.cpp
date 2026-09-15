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
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/links/VSCShunt.h"
#include "griddyn/links/ZBreaker.h"
#include "griddyn/loads/FDepLoad.h"
#include "griddyn/loads/ShuntTD.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include "nlohmann/json.hpp"
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

    template<class Number>
    std::vector<Number> numberList(const Json& record, std::string_view field)
    {
        std::vector<Number> values;
        if (!record.contains(field) || record[field].is_null()) {
            return values;
        }

        const auto& value = record[field];
        if (value.is_array()) {
            values.reserve(value.size());
            for (const auto& item : value) {
                values.push_back(item.get<Number>());
            }
            return values;
        }
        if (value.is_number()) {
            values.push_back(value.get<Number>());
            return values;
        }
        if (value.is_string()) {
            const auto parsed = Json::parse(value.get<std::string>(), nullptr, false);
            if (parsed.is_array()) {
                values.reserve(parsed.size());
                for (const auto& item : parsed) {
                    values.push_back(item.get<Number>());
                }
            } else if (parsed.is_number()) {
                values.push_back(parsed.get<Number>());
            }
        }
        return values;
    }

    double calculateAndesAdmittanceScale(const Json& record,
                                         double systemBasePower,
                                         double busBaseVoltage)
    {
        const auto deviceBasePower = number(record, "Sn", 100.0);
        const auto deviceBaseVoltage = number(record, "Vn", 110.0);
        if ((deviceBasePower <= 0.0) || (deviceBaseVoltage <= 0.0) || (busBaseVoltage <= 0.0)) {
            return 1.0;
        }
        return (deviceBasePower / systemBasePower) *
            std::pow(busBaseVoltage / deviceBaseVoltage, 2);
    }

    std::string normalizeAndesJson(std::string text)
    {
        bool inString = false;
        bool escaped = false;
        for (size_t index = 0; index < text.size();) {
            const auto character = text[index];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    inString = false;
                }
                ++index;
                continue;
            }
            if (character == '"') {
                inString = true;
                ++index;
                continue;
            }

            const bool hasNan = (text.compare(index, 3, "NaN") == 0);
            const bool hasSignedNan =
                (character == '-') && (text.compare(index + 1, 3, "NaN") == 0);
            const auto tokenLength = hasSignedNan ? 4U : 3U;
            if ((hasNan || hasSignedNan) &&
                ((index == 0) ||
                 (std::isalnum(static_cast<unsigned char>(text[index - 1])) == 0)) &&
                ((index + tokenLength >= text.size()) ||
                 (std::isalnum(static_cast<unsigned char>(text[index + tokenLength])) == 0))) {
                text.replace(index, tokenLength, "null");
                index += 4;
            } else {
                ++index;
            }
        }
        return text;
    }

    struct AndesStaticLoad {
        GridBus* mBus = nullptr;
        ZipLoad* mLoad = nullptr;
        double mP0 = 0.0;
        double mQ0 = 0.0;
    };

    void loadAndesFrequencyDependentLoads(
        const Json& document,
        const std::unordered_map<std::string, GridBus*>& acBuses,
        std::unordered_map<std::string, AndesStaticLoad>& pqLoads,
        std::unordered_map<std::string, GridBus*>& busFrequencyBuses)
    {
        // ANDES supplies static injections separately from its AC bus records.
        // Map those power-flow objects before adding the network branches.
        if (document.contains("PQ") && document["PQ"].is_array()) {
            for (const auto& record : document["PQ"]) {
                const auto bus = acBuses.find(indexKey(record, "bus"));
                if (bus == acBuses.end()) {
                    continue;
                }
                const auto realPower = number(record, "p0");
                const auto reactivePower = number(record, "q0");
                auto* load = new ZipLoad(realPower, reactivePower, objectName(record, "PQ"));
                if (number(record, "u", 1.0) == 0.0) {
                    load->disable();
                }
                bus->second->add(load);
                pqLoads.emplace(indexKey(record),
                                AndesStaticLoad{.mBus = bus->second,
                                                .mLoad = load,
                                                .mP0 = realPower,
                                                .mQ0 = reactivePower});
            }
        }

        // ANDES DeviceFinder resolves a valid busf index to an existing BusFreq
        // device. GridDyn has one frequency measurement path per AcBus, so keep
        // the compatible local association and let unresolved or remote links use
        // the owning bus frequency path.
        if (document.contains("BusFreq") && document["BusFreq"].is_array()) {
            for (const auto& record : document["BusFreq"]) {
                if (number(record, "u", 1.0) == 0.0) {
                    continue;
                }
                const auto bus = acBuses.find(indexKey(record, "bus"));
                if (bus == acBuses.end()) {
                    continue;
                }
                busFrequencyBuses.emplace(indexKey(record), bus->second);
                if (auto* acBus = dynamic_cast<AcBus*>(bus->second); acBus != nullptr) {
                    acBus->configureFrequencyFilter(number(record, "Tf", 0.02),
                                                    number(record, "Tw", 0.1),
                                                    number(record, "fn", 60.0));
                }
            }
        }

        // ANDES FLoad replaces its linked static PQ load. GridDyn's FDepLoad is
        // the closest model: its new scale/reference-voltage parameters preserve
        // the FLoad P/V/f and Q/V/f equations at the import boundary.
        if (document.contains("FLoad") && document["FLoad"].is_array()) {
            for (const auto& record : document["FLoad"]) {
                const auto pqLoad = pqLoads.find(indexKey(record, "pq"));
                if (pqLoad == pqLoads.end()) {
                    throw InvalidParameterValue("ANDES FLoad references an unknown PQ record: " +
                                                indexKey(record, "pq"));
                }

                auto* fload = new loads::FDepLoad(pqLoad->second.mP0,
                                                  pqLoad->second.mQ0,
                                                  objectName(record, "FLoad"));
                fload->set("kp", number(record, "kp", 100.0));
                fload->set("kq", number(record, "kq", 100.0));
                fload->set("vref", pqLoad->second.mBus->getVoltage());
                fload->set("ap", number(record, "ap", 1.0));
                fload->set("aq", number(record, "aq", 0.0));
                fload->set("betap", number(record, "bp", 0.0));
                fload->set("betaq", number(record, "bq", 0.0));
                if (record.contains("busf") && !record["busf"].is_null()) {
                    const auto busFrequency = busFrequencyBuses.find(indexKey(record, "busf"));
                    if ((busFrequency != busFrequencyBuses.end()) &&
                        (busFrequency->second == pqLoad->second.mBus)) {
                        fload->setLocalFrequencyBus(pqLoad->second.mBus);
                    }
                }

                if (number(record, "u", 1.0) == 0.0) {
                    fload->disable();
                }

                // The ANDES `replaces` relationship makes the static PQ inactive
                // whenever an FLoad is present. The FDepLoad supplies its output.
                pqLoad->second.mLoad->disable();
                pqLoad->second.mBus->add(fload);
            }
        }
    }

    void loadAndesShuntModels(const Json& document,
                              const std::unordered_map<std::string, GridBus*>& acBuses,
                              const std::unordered_map<std::string, double>& acBaseVoltages,
                              double systemBasePower)
    {
        if (document.contains("Shunt") && document["Shunt"].is_array()) {
            for (const auto& record : document["Shunt"]) {
                const auto busIndex = indexKey(record, "bus");
                const auto bus = acBuses.find(busIndex);
                const auto baseVoltage = acBaseVoltages.find(busIndex);
                if ((bus == acBuses.end()) || (baseVoltage == acBaseVoltages.end())) {
                    continue;
                }

                // ANDES marks g and b as y=True parameters. Convert from the
                // device base to the system/bus base using Zb/Zn, then map the
                // ANDES injection convention P=g*V^2, Q=-b*V^2 to ZipLoad.
                const auto scale =
                    calculateAndesAdmittanceScale(record, systemBasePower, baseVoltage->second);

                auto* shunt = new ZipLoad(objectName(record, "Shunt"));
                shunt->set("yp", number(record, "g") * scale);
                shunt->set("yq", -number(record, "b") * scale);
                if (number(record, "u", 1.0) == 0.0) {
                    shunt->disable();
                }
                bus->second->add(shunt);
            }
        }
        if (document.contains("ShuntTD") && document["ShuntTD"].is_array()) {
            for (const auto& record : document["ShuntTD"]) {
                const auto busIndex = indexKey(record, "bus");
                const auto bus = acBuses.find(busIndex);
                const auto baseVoltage = acBaseVoltages.find(busIndex);
                if ((bus == acBuses.end()) || (baseVoltage == acBaseVoltages.end())) {
                    continue;
                }

                const auto scale =
                    calculateAndesAdmittanceScale(record, systemBasePower, baseVoltage->second);
                auto* shunt = new loads::ShuntTD(objectName(record, "ShuntTD"));
                shunt->set("yp", number(record, "g") * scale);
                shunt->set("yq", -number(record, "b") * scale);
                if (number(record, "u", 1.0) == 0.0) {
                    shunt->disable();
                }
                bus->second->add(shunt);
            }
        }
        if (document.contains("ShuntSw") && document["ShuntSw"].is_array()) {
            for (const auto& record : document["ShuntSw"]) {
                const auto busIndex = indexKey(record, "bus");
                const auto bus = acBuses.find(busIndex);
                const auto baseVoltage = acBaseVoltages.find(busIndex);
                if ((bus == acBuses.end()) || (baseVoltage == acBaseVoltages.end())) {
                    continue;
                }

                const auto scale =
                    calculateAndesAdmittanceScale(record, systemBasePower, baseVoltage->second);
                auto conductanceSteps = numberList<double>(record, "gs");
                auto susceptanceSteps = numberList<double>(record, "bs");
                auto stepCounts = numberList<int>(record, "ns");
                if ((!conductanceSteps.empty() && conductanceSteps.size() != stepCounts.size()) ||
                    (!susceptanceSteps.empty() && susceptanceSteps.size() != stepCounts.size())) {
                    throw InvalidParameterValue(
                        "ANDES ShuntSw bank arrays must have matching gs, bs, and ns lengths for " +
                        objectName(record, "ShuntSw"));
                }
                for (auto& value : conductanceSteps) {
                    value *= scale;
                }
                for (auto& value : susceptanceSteps) {
                    value *= scale;
                }

                auto* shunt = new loads::Svd(objectName(record, "ShuntSw"));
                bus->second->add(shunt);
                shunt->configureAndesShunt(conductanceSteps,
                                           susceptanceSteps,
                                           stepCounts,
                                           number(record, "vref", 1.0),
                                           number(record, "dv", 0.05),
                                           number(record, "dt", 30.0),
                                           number(record, "g") * scale,
                                           number(record, "b") * scale);
                shunt->set("min_iter", number(record, "min_iter", 2.0));
                shunt->set("err_tol", number(record, "err_tol", 0.01));
                if (number(record, "u", 1.0) == 0.0) {
                    shunt->disable();
                }
            }
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
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        document = Json::parse(normalizeAndesJson(contents));
    }
    catch (const Json::parse_error&) {
        return false;
    }

    // ANDES exports can contain AC data only, or both AC and DC data.  The
    // capitalized Bus/Node sections distinguish them from GridDyn's generic
    // JSON element reader without requiring a DC Node section to be present.
    const bool hasAndesBus =
        document.is_object() && document.contains("Bus") && document["Bus"].is_array();
    const bool hasAndesNode =
        document.is_object() && document.contains("Node") && document["Node"].is_array();
    const bool hasAndesAcModel = hasAndesBus &&
        (document.contains("PQ") || document.contains("PV") || document.contains("Slack") ||
         document.contains("Line") || document.contains("Shunt") || document.contains("ShuntSw") ||
         document.contains("ShuntTD") || document.contains("FLoad") ||
         document.contains("BusFreq"));
    if (!hasAndesAcModel && !hasAndesNode) {
        return false;
    }
    if (parentObject == nullptr) {
        throw(InvalidParameterValue("ANDES JSON import requires a simulation parent"));
    }

    std::unordered_map<std::string, GridBus*> acBuses;
    std::unordered_map<std::string, double> acBaseVoltages;
    const auto parentBasePower = parentObject->get("basepower");
    const auto systemBasePower = (parentBasePower > 0.0) ? parentBasePower : 100.0;
    if (document.contains("Bus") && document["Bus"].is_array()) {
        for (const auto& record : document["Bus"]) {
            auto* bus = new AcBus(objectName(record, "Bus"));
            if (record.contains("idx") && record["idx"].is_number_integer()) {
                // Preserve the ANDES bus index used by PSS/E DYR records.
                // Dynamic-model readers resolve their target buses by user ID.
                bus->setUserID(record["idx"].get<index_t>());
            }
            setIfPresent(bus, record, "Vn", "basevoltage");
            setIfPresent(bus, record, "v0", "voltage");
            setIfPresent(bus, record, "a0", "angle");
            parentObject->add(bus);
            acBuses.emplace(indexKey(record), bus);
            acBaseVoltages.emplace(indexKey(record), number(record, "Vn", 0.0));
        }
    }

    std::unordered_map<std::string, AndesStaticLoad> pqLoads;
    std::unordered_map<std::string, GridBus*> busFrequencyBuses;
    loadAndesFrequencyDependentLoads(document, acBuses, pqLoads, busFrequencyBuses);
    loadAndesShuntModels(document, acBuses, acBaseVoltages, systemBasePower);
    if (document.contains("PV") && document["PV"].is_array()) {
        for (const auto& record : document["PV"]) {
            const auto bus = acBuses.find(indexKey(record, "bus"));
            if (bus == acBuses.end()) {
                continue;
            }
            bus->second->set("type", "pv");
            setIfPresent(bus->second, record, "v0", "vtarget");
            auto* generator = new DynamicGenerator(objectName(record, "PV"));
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
            auto* generator = new DynamicGenerator(objectName(record, "Slack"));
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
    if (document.contains("Jumper") && document["Jumper"].is_array()) {
        for (const auto& record : document["Jumper"]) {
            const auto first = acBuses.find(indexKey(record, "bus1"));
            const auto second = acBuses.find(indexKey(record, "bus2"));
            if ((first == acBuses.end()) || (second == acBuses.end())) {
                continue;
            }
            auto* jumper = new links::ZBreaker(objectName(record, "Jumper"));
            jumper->updateBus(first->second, 1);
            jumper->updateBus(second->second, 2);
            if (number(record, "u", 1.0) == 0.0) {
                jumper->disable();
            }
            parentObject->add(jumper);
        }
    }

    std::unordered_map<std::string, DcBus*> dcBuses;
    if (hasAndesNode) {
        for (const auto& record : document["Node"]) {
            auto* bus = new DcBus(objectName(record, "Node"));
            setIfPresent(bus, record, "Vdcn", "basevoltage");
            setIfPresent(bus, record, "v0", "voltage");
            parentObject->add(bus);
            dcBuses.emplace(indexKey(record), bus);
        }
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
            branch->set("andes_current_balance", 1.0);
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
            const auto acBus = acBuses.find(indexKey(record, "bus"));
            const auto dcBus = dcBuses.find(indexKey(record, "node1"));
            const auto dcReference = dcBuses.find(indexKey(record, "node2"));
            if ((acBus == acBuses.end()) || (dcBus == dcBuses.end()) ||
                (dcReference == dcBuses.end())) {
                continue;
            }
            auto* converter = new links::VSCShunt(objectName(record, "VSCShunt"));
            converter->set("andes_current_balance", 1.0);
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
            converter->updateBus(acBus->second, 1);
            converter->updateBus(dcBus->second, 2);
            converter->updateBus(dcReference->second, 3);
            parentObject->add(converter);
        }
    }
    return true;
}
}  // namespace griddyn
