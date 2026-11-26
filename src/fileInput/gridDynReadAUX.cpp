/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Link.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "readerHelper.h"
#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace griddyn {
namespace {

    std::string trimAux(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        const auto last = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(first, last - first + 1));
    }

    std::string lowerAux(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const auto character : value) {
            result.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return result;
    }

    std::string fieldKey(std::string_view field)
    {
        std::string result;
        result.reserve(field.size());
        for (const auto character : field) {
            if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == ':') {
                result.push_back(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
        }
        return result;
    }

    bool auxYes(std::string_view value)
    {
        const auto normalized = lowerAux(trimAux(value));
        return normalized == "yes" || normalized == "true" || normalized == "1" ||
            normalized == "closed" || normalized == "active";
    }

    bool auxClosed(std::string_view value)
    {
        const auto normalized = lowerAux(trimAux(value));
        return normalized != "open" && normalized != "out of service" &&
            normalized != "outofservice" && normalized != "off" && normalized != "0" &&
            normalized != "no" && normalized != "false" && normalized != "inactive";
    }

    struct AuxSection {
        std::string mType;
        std::unordered_map<std::string, size_t> mColumns;
    };

    struct AuxRow {
        const AuxSection& mSection;
        const std::vector<std::string>& mValues;
        size_t mLineNumber;

        [[nodiscard]] size_t index(std::initializer_list<std::string_view> names) const
        {
            for (const auto name : names) {
                if (const auto found = mSection.mColumns.find(std::string(name));
                    found != mSection.mColumns.end()) {
                    return found->second;
                }
            }
            return mValues.size();
        }

        [[nodiscard]] bool has(std::initializer_list<std::string_view> names) const
        {
            return index(names) < mValues.size();
        }

        [[nodiscard]] std::string_view value(std::initializer_list<std::string_view> names) const
        {
            const auto location = index(names);
            return (location < mValues.size()) ? std::string_view(mValues[location]) :
                                                 std::string_view{};
        }

        [[nodiscard]] double number(std::initializer_list<std::string_view> names,
                                    double defaultValue = 0.0) const
        {
            const auto text = value(names);
            if (text.empty() || lowerAux(text) == "same") {
                return defaultValue;
            }
            const auto converted = trimAux(text);
            char* end = nullptr;
            const double result = std::strtod(converted.c_str(), &end);
            return (end != converted.c_str() && *end == '\0' && std::isfinite(result)) ?
                result :
                defaultValue;
        }

        [[nodiscard]] int integer(std::initializer_list<std::string_view> names,
                                  int defaultValue = 0) const
        {
            return static_cast<int>(number(names, static_cast<double>(defaultValue)));
        }
    };

    [[nodiscard]] std::optional<double>
        auxOptionalNumber(const AuxRow& row, std::initializer_list<std::string_view> names)
    {
        const auto value = row.number(names, std::numeric_limits<double>::quiet_NaN());
        return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
    }

    // AUX has both DATA (Object, [columns], ...) and concise Object (columns) headers.
    AuxSection parseAuxHeader(std::string_view header)
    {
        AuxSection section;
        const auto open = header.find('(');
        if (open == std::string_view::npos) {
            section.mType = "script";
            return section;
        }
        const auto prefix = lowerAux(trimAux(header.substr(0, open)));
        if (prefix == "data" || prefix.starts_with("data ")) {
            const auto comma = header.find(',', open + 1);
            if (comma == std::string_view::npos) {
                throw std::runtime_error("AUX DATA section has no object type");
            }
            section.mType = lowerAux(trimAux(header.substr(open + 1, comma - open - 1)));
        } else {
            const auto separator = prefix.find_first_of(" \t");
            section.mType = prefix.substr(0, separator);
        }

        const auto fieldsStart =
            (prefix == "data" || prefix.starts_with("data ")) ? header.find('[', open) : open;
        const auto fieldsEnd = (prefix == "data" || prefix.starts_with("data ")) ?
            header.find(']', fieldsStart) :
            header.rfind(')');
        if (fieldsStart == std::string_view::npos || fieldsEnd == std::string_view::npos ||
            fieldsEnd <= fieldsStart) {
            throw std::runtime_error("AUX DATA section has no field list: " + section.mType);
        }
        const auto fields = header.substr(fieldsStart + 1, fieldsEnd - fieldsStart - 1);
        size_t start = 0;
        while (start < fields.size()) {
            const auto comma = fields.find(',', start);
            const auto end = (comma == std::string_view::npos) ? fields.size() : comma;
            const auto name = fieldKey(trimAux(fields.substr(start, end - start)));
            if (!name.empty()) {
                section.mColumns.emplace(name, section.mColumns.size());
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
        return section;
    }

    std::vector<std::string> tokenizeAuxRow(std::string_view line)
    {
        // Quoted fields can contain spaces and commas; doubled or backslash-escaped
        // quote characters belong to the field rather than ending it.
        std::vector<std::string> values;
        std::string value;
        char quote = 0;
        bool present = false;
        for (size_t position = 0; position < line.size(); ++position) {
            const char character = line[position];
            if (quote != 0) {
                if (character == quote) {
                    if ((position + 1 < line.size()) && (line[position + 1] == quote)) {
                        value.push_back(quote);
                        ++position;
                    } else {
                        quote = 0;
                    }
                } else if (character == '\\' && (position + 1 < line.size()) &&
                           line[position + 1] == quote) {
                    value.push_back(quote);
                    ++position;
                } else {
                    value.push_back(character);
                }
            } else if (character == '"' || character == '\'') {
                quote = character;
                present = true;
            } else if (character == '/' && (position + 1 < line.size()) &&
                       line[position + 1] == '/') {
                break;
            } else if (std::isspace(static_cast<unsigned char>(character)) != 0 ||
                       character == ',') {
                if (present) {
                    values.push_back(std::move(value));
                    value.clear();
                    present = false;
                }
            } else {
                value.push_back(character);
                present = true;
            }
        }
        if (quote != 0) {
            throw std::runtime_error("unterminated quoted AUX value");
        }
        if (present) {
            values.push_back(std::move(value));
        }
        return values;
    }

    using AuxRowHandler = std::function<void(const AuxRow&)>;
    using AuxSectionFilter = std::function<bool(std::string_view)>;
    using AuxSubdataHandler = std::function<void(std::string_view, std::string_view, size_t)>;

    void scanAuxFile(const std::string& fileName,
                     const AuxSectionFilter& wantsSection,
                     const AuxRowHandler& handleRow,
                     const AuxSubdataHandler& handleSubdata = {})
    {
        auto input = openInputFile(fileName);
        std::string line;
        std::string header;
        AuxSection section;
        bool inHeader = false;
        bool inBody = false;
        bool inSubdata = false;
        std::string subdataName;
        bool active = false;
        size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto trimmed = trimAux(line);
            if (trimmed.empty() || trimmed.starts_with("//")) {
                continue;
            }
            if (!inHeader && !inBody) {
                const auto lower = lowerAux(trimmed);
                if (lower.starts_with("data ") || lower.starts_with("data(") ||
                    lower.starts_with("script ") || lower == "script" ||
                    (std::isalpha(static_cast<unsigned char>(trimmed.front())) != 0 &&
                     trimmed.contains('('))) {
                    header = trimmed;
                    inHeader = true;
                } else {
                    continue;
                }
            } else if (inHeader) {
                header.push_back(' ');
                header += trimmed;
            } else {
                if (inSubdata) {
                    if (lowerAux(trimmed).starts_with("</subdata")) {
                        inSubdata = false;
                        subdataName.clear();
                    } else if (active && handleSubdata != nullptr) {
                        try {
                            handleSubdata(subdataName, trimmed, lineNumber);
                        }
                        catch (const std::exception& error) {
                            std::string message = fileName;
                            message.push_back(':');
                            message.append(std::to_string(lineNumber));
                            message.append(" (");
                            message.append(section.mType);
                            message.append(" SUBDATA ");
                            message.append(subdataName);
                            message.append("): ");
                            message.append(error.what());
                            throw std::runtime_error(message);
                        }
                    }
                    continue;
                }
                if (lowerAux(trimmed).starts_with("<subdata")) {
                    // SUBDATA is attached to the preceding table row. Most
                    // readers ignore it; the OPF pass consumes Gen BidCurve.
                    const auto end = trimmed.find('>');
                    if (end == std::string::npos) {
                        throw std::runtime_error(fileName + ':' + std::to_string(lineNumber) +
                                                 ": unterminated AUX SUBDATA tag");
                    }
                    subdataName = lowerAux(trimAux(
                        std::string_view(trimmed).substr(std::string_view("<subdata").size(),
                                                         end -
                                                             std::string_view("<subdata").size())));
                    inSubdata = !lowerAux(trimmed).contains("</subdata");
                    continue;
                }
                if (trimmed == "}") {
                    inBody = false;
                    active = false;
                    continue;
                }
                if (!active) {
                    continue;
                }
                try {
                    auto values = tokenizeAuxRow(trimmed);
                    if (!values.empty()) {
                        if (values.size() != section.mColumns.size()) {
                            throw std::runtime_error(
                                "expected " + std::to_string(section.mColumns.size()) +
                                " fields, found " + std::to_string(values.size()));
                        }
                        handleRow(AuxRow{.mSection = section,
                                         .mValues = values,
                                         .mLineNumber = lineNumber});
                    }
                }
                catch (const std::exception& error) {
                    throw std::runtime_error(fileName + ':' + std::to_string(lineNumber) + " (" +
                                             section.mType + "): " + error.what());
                }
                continue;
            }

            if (inHeader && header.contains('{')) {
                const auto brace = header.find('{');
                section = parseAuxHeader(std::string_view(header).substr(0, brace));
                active = wantsSection(section.mType);
                inHeader = false;
                inBody = true;
                header.clear();
            }
        }
        if (inHeader || inBody || inSubdata) {
            throw std::runtime_error(fileName + ": unterminated AUX section");
        }
    }

    struct AuxNetwork {
        CoreObject* mParent;
        const BasicReaderInfo& mOptions;
        std::unordered_map<int, GridArea*> mAreas;
        std::unordered_map<int, GridBus*> mBuses;
        std::unordered_map<std::string, Link*> mBranches;
        std::unordered_map<int, std::vector<Generator*>> mGeneratorsByBus;
        std::unordered_map<int, std::vector<Generator*>> mRemoteGroups;
        std::unordered_set<int> mRemoteTerminalBuses;
        double mBase;
        bool mExistingNetwork;
    };

    std::string auxName(const AuxNetwork& network, std::string name)
    {
        if (!network.mOptions.prefix.empty()) {
            name.insert(0, network.mOptions.prefix + '_');
        }
        return name;
    }

    GridBus* findAuxBus(AuxNetwork& network, int number)
    {
        if (const auto found = network.mBuses.find(number); found != network.mBuses.end()) {
            return found->second;
        }
        if (network.mExistingNetwork) {
            // AUX can supplement a network loaded from another format. Reuse
            // buses by PowerWorld user ID instead of creating duplicates.
            auto* bus = dynamic_cast<GridBus*>(
                network.mParent->getRoot()->findByUserID("bus", static_cast<index_t>(number)));
            if (bus != nullptr) {
                network.mBuses.emplace(number, bus);
            }
            return bus;
        }
        return nullptr;
    }

    GridBus* requireAuxBus(AuxNetwork& network, int number, const AuxRow& row)
    {
        auto* bus = findAuxBus(network, number);
        if (bus == nullptr) {
            throw std::runtime_error("bus " + std::to_string(number) + " referenced by " +
                                     row.mSection.mType + " at line " +
                                     std::to_string(row.mLineNumber) + " does not exist");
        }
        return bus;
    }

    CoreObject* auxLinkParent(CoreObject* parent, const Link* link)
    {
        const auto* bus1 = link->getBus(1);
        const auto* bus2 = link->getBus(2);
        auto* area1 = (bus1 != nullptr) ? dynamic_cast<GridArea*>(bus1->getParent()) : nullptr;
        auto* area2 = (bus2 != nullptr) ? dynamic_cast<GridArea*>(bus2->getParent()) : nullptr;
        return (area1 != nullptr && area1 == area2) ? static_cast<CoreObject*>(area1) : parent;
    }

    std::string auxBranchKey(int fromBus, int toBus, std::string_view circuit)
    {
        return std::to_string(fromBus) + ':' + std::to_string(toBus) + ':' + trimAux(circuit);
    }

    void readAuxMetadata(AuxNetwork& network, const AuxRow& row)
    {
        if (row.mSection.mType == "sim_solution_options_value") {
            if (lowerAux(trimAux(row.value({"variablename", "option"}))) == "mvabase") {
                const auto base = row.number({"valuefield", "value"}, network.mBase);
                if (base > 0.0) {
                    network.mBase = base;
                    network.mParent->set("basepower", base, units::MW);
                }
            }
            return;
        }
        const int number = row.integer({"areanum", "number"});
        if (number <= 0 || network.mAreas.contains(number)) {
            return;
        }
        auto* existing = dynamic_cast<GridArea*>(
            network.mParent->getRoot()->findByUserID("area", static_cast<index_t>(number)));
        if (existing != nullptr) {
            network.mAreas.emplace(number, existing);
            return;
        }
        auto name = trimAux(row.value({"areaname", "name"}));
        if (name.empty()) {
            name = "AREA_" + std::to_string(number);
        }
        auto* area = new GridArea(auxName(network, std::move(name)));
        area->setUserID(static_cast<index_t>(number));
        addToParentWithRename(area, network.mParent);
        network.mAreas.emplace(number, area);
    }

    void readAuxBus(AuxNetwork& network, const AuxRow& row)
    {
        const int number = row.integer({"busnum", "number"});
        if (number <= 0) {
            throw std::runtime_error("AUX bus number must be positive");
        }
        auto* bus = findAuxBus(network, number);
        if (bus == nullptr) {
            auto name = trimAux(row.value({"busname", "name"}));
            if (name.empty()) {
                name = "BUS_" + std::to_string(number);
            }
            bus = new AcBus(auxName(network, std::move(name)));
            bus->setUserID(static_cast<index_t>(number));
            bus->set("basepower", network.mBase, units::MW);
            const auto areaNumber = row.integer({"areanum", "areanumber"});
            auto* busParent = network.mParent;
            if (const auto area = network.mAreas.find(areaNumber); area != network.mAreas.end()) {
                busParent = area->second;
            }
            addToParentWithRename(bus, busParent);
            network.mBuses.emplace(number, bus);
        }
        const auto baseVoltage = row.number({"busnomvolt", "nomkv"});
        if (baseVoltage > 0.0) {
            bus->set("basevoltage", baseVoltage, units::kV);
        }
        const auto voltage = row.number({"buspuvolt", "vpu"});
        if (voltage > 0.0) {
            // AUX has no RAW bus category; use the exported voltage as the
            // initial local regulation target, as the RAW reader does.
            bus->set("voltage", voltage);
            bus->set("vtarget", voltage);
        }
        if (row.has({"busangle", "vangle"})) {
            bus->set("angle", row.number({"busangle", "vangle"}), units::deg);
        }
        if (row.has({"busslack", "slack"}) && auxYes(row.value({"busslack", "slack"}))) {
            bus->set("type", "swing");
            bus->set("atarget", row.number({"busangle", "vangle"}), units::deg);
        } else if (!network.mExistingNetwork) {
            bus->set("type", "PQ");
        }
        const auto zone = row.number({"zonenum", "zonenumber"});
        if (zone > 0.0) {
            bus->set("zone", zone);
        }
        const auto vmax = row.number({"busvoltlimhigh:1", "limithigha"});
        const auto vmin = row.number({"busvoltlimlow:1", "limitlowa"});
        if (vmax > 0.0) {
            bus->set("vmax", vmax);
        }
        if (vmin > 0.0) {
            bus->set("vmin", vmin);
        }
        const double conductance = row.number({"busg:1", "nomg"});
        const double susceptance = row.number({"busb:1", "nomb"});
        if (conductance != 0.0 || susceptance != 0.0) {
            const auto name = bus->getName() + "_shunt_bus";
            if (bus->find(name) == nullptr) {
                auto* shunt = new ZipLoad(name);
                shunt->setFixedShunt();
                shunt->set("yp", conductance, units::MW);
                shunt->set("yq", -susceptance, units::MVAR);
                bus->add(shunt);
            }
        }
    }

    void readAuxGenerator(AuxNetwork& network, const AuxRow& row)
    {
        const int busNumber = row.integer({"busnum"});
        auto* bus = requireAuxBus(network, busNumber, row);
        auto identifier = trimAux(row.value({"genid", "id"}));
        if (identifier.empty()) {
            identifier = "1";
        }
        const auto name = bus->getName() + "_Gen_" + identifier;
        auto* generator = dynamic_cast<Generator*>(bus->find(name));
        if (generator == nullptr) {
            if (!row.has({"genmwsetpoint", "genmw", "mwsetpoint", "genmwmax", "mwmax"})) {
                return;  // An AUX supplement may only amend an existing generator.
            }
            generator = network.mOptions.checkFlag(ASSUME_POWERFLOW_ONLY) ?
                new Generator(name) :
                new DynamicGenerator(name);
            generator->set("basepower", network.mBase, units::MW);
            generator->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
            bus->add(generator);
        }
        auto& busGenerators = network.mGeneratorsByBus[busNumber];
        if (std::find(busGenerators.begin(), busGenerators.end(), generator) ==
            busGenerators.end()) {
            busGenerators.push_back(generator);
        }
        if (row.has({"genstatus", "status"}) && !auxClosed(row.value({"genstatus", "status"}))) {
            generator->disable();
        }
        if (row.has({"genmwsetpoint", "genmw", "mwsetpoint"})) {
            generator->set("p", row.number({"genmwsetpoint", "genmw", "mwsetpoint"}), units::MW);
        }
        if (row.has({"genmvrsetpoint", "genmvr", "mvarsetpoint"})) {
            generator->set("q",
                           row.number({"genmvrsetpoint", "genmvr", "mvarsetpoint"}),
                           units::MVAR);
        }
        if (row.has({"genmwmax", "mwmax"})) {
            generator->set("pmax", row.number({"genmwmax", "mwmax"}), units::MW);
        }
        if (row.has({"genmwmin", "mwmin"})) {
            generator->set("pmin", row.number({"genmwmin", "mwmin"}), units::MW);
        }
        const int regulatedBus = row.integer({"genregnum", "regbusnum"}, busNumber);
        const double qmax = row.number({"genmvrmax", "mvarmax"});
        const double qmin = row.number({"genmvrmin", "mvarmin"});
        const bool omittedLocalQRange =
            qmax == 0.0 && qmin == 0.0 && (regulatedBus <= 0 || regulatedBus == busNumber);
        // A local zero/zero pair is PowerWorld's export of unspecified reactive
        // limits in this case.  Keep the generator's unbounded defaults, as RAW
        // does, rather than making its voltage regulation impossible.
        if (!omittedLocalQRange) {
            if (row.has({"genmvrmax", "mvarmax"})) {
                generator->set("qmax", qmax, units::MVAR);
            }
            if (row.has({"genmvrmin", "mvarmin"})) {
                generator->set("qmin", qmin, units::MVAR);
            }
        }
        const auto machineBase = row.number({"genmvabase", "mvabase"});
        if (machineBase > 0.0) {
            generator->set("mbase", machineBase, units::MVAR);
        }
        if (row.has({"genzr", "genr"})) {
            generator->set("rs", row.number({"genzr", "genr"}));
        }
        if (row.has({"genzx", "genx"})) {
            generator->set("xs", row.number({"genzx", "genx"}));
        }
        const auto target = row.number({"genvoltset", "voltset"});
        if (target > 0.0) {
            generator->set("vtarget", target);
        }
        const bool regulates = generator->isEnabled() &&
            (!row.has({"genavrable", "avr"}) || auxYes(row.value({"genavrable", "avr"}))) &&
            (!row.has({"genmvrmax", "mvarmax", "genmvrmin", "mvarmin"}) || qmax != qmin ||
             omittedLocalQRange);
        if (!regulates) {
            generator->setFlag("local_voltage_control", false);
            generator->setFlag("remote_voltage_control", false);
            generator->setFlag("adjustable_q", false);
            // PowerWorld AUX omits the RAW bus category.  A generator bus can
            // retain its PV category even when its machine is out of service.
            if ((!generator->isEnabled() || auxYes(row.value({"genavrable", "avr"}))) &&
                bus->getType() == GridBus::BusType::PQ) {
                bus->set("type", "PV");
            }
            return;
        }
        if (regulatedBus > 0 && regulatedBus != busNumber) {
            auto* controlBus = requireAuxBus(network, regulatedBus, row);
            const auto participation = row.number({"genrmpct", "regfactor"}, 100.0);
            generator->set("vcontrolfrac", participation / 100.0);
            generator->set("qbias", row.number({"genmvrsetpoint", "mvarsetpoint"}), units::MVAR);
            if (target > 0.0) {
                controlBus->set("vtarget", target);
            }
            generator->add(controlBus);
            network.mRemoteGroups[regulatedBus].push_back(generator);
            network.mRemoteTerminalBuses.insert(busNumber);
        }
        if (bus->getType() != GridBus::BusType::SLK) {
            bus->set("type", "PV");
        }
    }

    void finishAuxGeneratorControls(AuxNetwork& network)
    {
        // PowerWorld exports remote participation as percentages. GridDyn
        // expects fractions that sum to one for each regulated bus.
        for (auto& [busNumber, generators] : network.mRemoteGroups) {
            (void)busNumber;
            double totalParticipation = 0.0;
            for (const auto* generator : generators) {
                totalParticipation += generator->get("vcontrolfrac");
            }
            if (totalParticipation > 0.0) {
                for (auto* generator : generators) {
                    generator->set("vcontrolfrac",
                                   generator->get("vcontrolfrac") / totalParticipation);
                }
            }
        }
        // A remote generator's terminal must not also regulate its own voltage.
        for (const auto terminalNumber : network.mRemoteTerminalBuses) {
            auto* terminalBus = findAuxBus(network, terminalNumber);
            if (terminalBus == nullptr) {
                continue;
            }
            if (terminalBus->getType() == GridBus::BusType::SLK) {
                terminalBus->set("type", "afix");
                terminalBus->set("dyntype", "fixangle");
            } else {
                terminalBus->set("type", "PQ");
            }
            for (auto* generator : network.mGeneratorsByBus[terminalNumber]) {
                generator->setFlag("local_voltage_control", false);
            }
        }
    }

    void readAuxLoad(AuxNetwork& network, const AuxRow& row)
    {
        auto* bus = requireAuxBus(network, row.integer({"busnum"}), row);
        auto identifier = trimAux(row.value({"loadid", "id"}));
        if (identifier.empty()) {
            identifier = "1";
        }
        const auto name = bus->getName() + "_load_" + identifier;
        auto* load = dynamic_cast<ZipLoad*>(bus->find(name));
        if (load == nullptr) {
            if (!row.has({"loadsmw", "smw", "loadsmvr", "smvar"})) {
                return;
            }
            load = new ZipLoad(name);
            load->set("basepower", network.mBase, units::MW);
            load->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
            bus->add(load);
        }
        if (row.has({"loadstatus", "status"}) && !auxClosed(row.value({"loadstatus", "status"}))) {
            load->disable();
        }
        if (row.has({"loadsmw", "smw"})) {
            load->set("p", row.number({"loadsmw", "smw"}), units::MW);
        }
        if (row.has({"loadsmvr", "smvar"})) {
            load->set("q", row.number({"loadsmvr", "smvar"}), units::MVAR);
        }
        if (row.has({"loadimw", "imw"})) {
            load->set("ip", row.number({"loadimw", "imw"}), units::MW);
        }
        if (row.has({"loadimvr", "imvar"})) {
            load->set("iq", row.number({"loadimvr", "imvar"}), units::MVAR);
        }
        if (row.has({"loadzmw", "zmw"})) {
            load->set("yp", row.number({"loadzmw", "zmw"}), units::MW);
        }
        if (row.has({"loadzmvr", "zmvar"})) {
            // ZipLoad stores susceptance with the opposite sign from the
            // PowerWorld constant-impedance reactive load field.
            load->set("yq", -row.number({"loadzmvr", "zmvar"}), units::MVAR);
        }
    }

    void readAuxShunt(AuxNetwork& network, const AuxRow& row)
    {
        auto* bus = requireAuxBus(network, row.integer({"busnum"}), row);
        auto identifier = trimAux(row.value({"shuntid", "id"}));
        if (identifier.empty()) {
            identifier = "1";
        }
        const auto name = bus->getName() + "_shunt_" + identifier;
        if (bus->find(name) != nullptr) {
            return;
        }
        const auto mode = lowerAux(trimAux(row.value({"sscmode", "shuntmode"})));
        bool hasBlocks = false;
        for (int block = 1; block <= 10; ++block) {
            if (row.integer({"blocknumberstep" + std::to_string(block)}) > 0) {
                hasBlocks = true;
                break;
            }
        }
        const double megawatts = row.number({"ssnmw", "mwnom"});
        const double mvar = row.number({"ssnmvr", "mvarnom"});
        GridLoad* shunt = nullptr;
        if ((mode == "continuous" || mode == "discrete") && hasBlocks) {
            auto* svd = new loads::Svd(name);
            svd->set("mode", mode == "continuous" ? "cont" : "stepped");
            const double high = row.number({"reghigh"});
            const double low = row.number({"reglow"});
            if (high > 0.0) {
                svd->set("vmax", high);
            }
            if (low > 0.0) {
                svd->set("vmin", low);
            }
            const int controlBusNumber = row.integer({"regbusnum"});
            if (controlBusNumber > 0 && controlBusNumber != bus->getUserID()) {
                svd->setControlBus(requireAuxBus(network, controlBusNumber, row));
            }
            for (int block = 1; block <= 10; ++block) {
                const auto suffix = std::to_string(block);
                const int steps = row.integer({"blocknumberstep" + suffix});
                const double value = row.number({"blockmvarperstep" + suffix});
                if (steps > 0 && value != 0.0) {
                    svd->addBlock(steps, -value, units::MVAR);
                }
            }
            svd->ZipLoad::set("yq", -mvar, units::MVAR);
            svd->setInitialReactivePower(-mvar, units::MVAR);
            shunt = svd;
        } else {
            auto* fixed = new ZipLoad(name);
            fixed->setFixedShunt();
            fixed->set("yp", megawatts, units::MW);
            fixed->set("yq", -mvar, units::MVAR);
            shunt = fixed;
        }
        shunt->set("basepower", network.mBase, units::MW);
        shunt->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
        bus->add(shunt);
        if (row.has({"ssstatus", "status"}) && !auxClosed(row.value({"ssstatus", "status"}))) {
            shunt->disable();
        }
    }

    void readAuxBranch(AuxNetwork& network, const AuxRow& row)
    {
        const int fromNumber = row.integer({"busnum", "busnumfrom"});
        const int toNumber = row.integer({"busnum:1", "busnumto"});
        auto* fromBus = requireAuxBus(network, fromNumber, row);
        auto* toBus = requireAuxBus(network, toNumber, row);
        const auto circuit = trimAux(row.value({"linecircuit", "circuit"}));
        const auto key = auxBranchKey(fromNumber, toNumber, circuit);
        const auto deviceType = lowerAux(trimAux(row.value({"branchdevicetype"})));
        const bool transformer =
            (deviceType.contains("transformer") && deviceType != "transformerwinding") ||
            row.has({"liner:1", "rxfbase", "xfxldcrcc", "tapxfbase"});
        Link* link = nullptr;
        if (const auto found = network.mBranches.find(key); found != network.mBranches.end()) {
            link = found->second;
        } else {
            std::string name = transformer ? "tx_" : "";
            name += fromBus->getName() + "_to_" + toBus->getName();
            if (!circuit.empty() && circuit != "1") {
                name += '_' + circuit;
            }
            const auto controlType = lowerAux(trimAux(row.value({"linexftype", "controltype"})));
            const bool adjustable = transformer && auxYes(row.value({"xfauto", "autocontrol"})) &&
                controlType != "fixed" && !controlType.empty();
            link = adjustable ? static_cast<Link*>(new links::AdjustableTransformer(name)) :
                                static_cast<Link*>(new AcLine(name));
            link->set("basepower", network.mBase, units::MW);
            link->updateBus(fromBus, 1);
            link->updateBus(toBus, 2);
            addToParentWithRename(link, auxLinkParent(network.mParent, link));
            network.mBranches.emplace(key, link);
            if (auto* control = dynamic_cast<links::AdjustableTransformer*>(link)) {
                const auto controlMode =
                    lowerAux(trimAux(row.value({"xfregtargettype", "regtargettype"})));
                const bool phaseControl = controlType.contains("phase");
                std::string_view mode = "voltage";
                if (phaseControl || controlMode.contains("mw")) {
                    mode = "mw";
                } else if (controlMode.contains("mvar")) {
                    mode = "mvar";
                }
                control->set("mode", mode);
                const int controlBusNumber = row.integer({"xfregbus", "regbusnum"});
                if (controlBusNumber > 0) {
                    if (controlBusNumber == fromNumber) {
                        control->setControlBus(1);
                    } else if (controlBusNumber == toNumber) {
                        control->setControlBus(2);
                    } else {
                        control->setControlBus(requireAuxBus(network, controlBusNumber, row));
                    }
                } else if (phaseControl) {
                    // A phase controller with no explicit regulation bus meters
                    // the from side in this PowerWorld export.
                    control->setControlBus(1);
                }
                if (row.has({"xftapmin:1", "tapminxfbase", "xftapmin"})) {
                    const auto minimum = row.number({"xftapmin:1", "tapminxfbase", "xftapmin"});
                    if (phaseControl) {
                        control->set("mintapangle", minimum, units::deg);
                    } else {
                        control->set("mintap", minimum);
                    }
                }
                if (row.has({"xftapmax:1", "tapmaxxfbase", "xftapmax"})) {
                    const auto maximum = row.number({"xftapmax:1", "tapmaxxfbase", "xftapmax"});
                    if (phaseControl) {
                        control->set("maxtapangle", maximum, units::deg);
                    } else {
                        control->set("maxtap", maximum);
                    }
                }
                if (row.has({"xfregmin", "regmin"})) {
                    // PowerWorld MW/Mvar limits must enter the transformer in
                    // physical units; the default unit is a per-unit quantity.
                    auto unit = units::defunit;
                    if (phaseControl || controlMode.contains("mw")) {
                        unit = units::MW;
                    } else if (controlMode.contains("mvar")) {
                        unit = units::MVAR;
                    }
                    control->set("min", row.number({"xfregmin", "regmin"}), unit);
                }
                if (row.has({"xfregmax", "regmax"})) {
                    auto unit = units::defunit;
                    if (phaseControl || controlMode.contains("mw")) {
                        unit = units::MW;
                    } else if (controlMode.contains("mvar")) {
                        unit = units::MVAR;
                    }
                    control->set("max", row.number({"xfregmax", "regmax"}), unit);
                }
                const auto step = row.number({"xfstep:1", "xfstep", "tapstepsizexfbase"});
                // The transformer class also applies stepsize to tap-ratio
                // initialization.  RAW deliberately skips it for phase shifters.
                if (step > 0.0 && !phaseControl) {
                    control->set("stepsize", step);
                }
            }
        }

        if (transformer) {
            const auto windingBase = row.number({"xfmvabase"}, network.mBase);
            double scale = (windingBase > 0.0) ? network.mBase / windingBase : 1.0;
            const double nominalFrom = row.number({"xfnominalkv", "xfnomkvbasefrom"});
            const double nominalTo = row.number({"xfnominalkv:1", "xfnomkvbaseto"});
            double tap = row.number({"linetap:1", "tapxfbase", "linetap"}, 1.0);
            // Some PowerWorld phase-shifter exports express both tap and leakage
            // impedance on the low-voltage winding's physical turns ratio.  The
            // matching RAW record uses a per-unit tap and leakage on system base.
            const bool physicalPhaseTap =
                lowerAux(trimAux(row.value({"linexftype", "controltype"}))).contains("phase") &&
                nominalFrom > 0.0 && nominalTo > 0.0 &&
                std::abs(tap - (nominalTo / nominalFrom)) < 0.05 && std::abs(tap - 1.0) > 0.2;
            if (physicalPhaseTap) {
                scale *= (nominalTo / nominalFrom) * (nominalTo / nominalFrom);
            }
            if (row.has({"liner:1", "rxfbase"})) {
                link->set("r", row.number({"liner:1", "rxfbase"}) * scale);
            }
            if (row.has({"linex:1", "xxfbase"})) {
                link->set("x", row.number({"linex:1", "xxfbase"}) * scale);
            }
            if (nominalFrom > 0.0 && nominalTo > 0.0) {
                const auto fromBase = fromBus->get("basevoltage", units::kV);
                const auto toBase = toBus->get("basevoltage", units::kV);
                if (fromBase > 0.0 && toBase > 0.0) {
                    tap *= (nominalFrom / fromBase) / (nominalTo / toBase);
                }
            }
            if (tap > 0.0) {
                link->set("tap", tap);
            }
            if (row.has({"linephase", "phase"})) {
                link->set("tapangle", row.number({"linephase", "phase"}), units::deg);
            }
            if (row.has({"xfrmermagnetizingg:1", "gmagxfbase"})) {
                link->set("g1", row.number({"xfrmermagnetizingg:1", "gmagxfbase"}));
            }
            if (row.has({"xfrmermagnetizingb:1", "bmagxfbase"})) {
                link->set("b1", row.number({"xfrmermagnetizingb:1", "bmagxfbase"}));
            }
        } else {
            if (row.has({"liner", "r"})) {
                link->set("r", row.number({"liner", "r"}));
            }
            if (row.has({"linex", "x"})) {
                link->set("x", row.number({"linex", "x"}));
            }
            if (row.has({"linec", "b"})) {
                link->set("b", row.number({"linec", "b"}));
            }
            if (row.has({"lineg", "g"})) {
                const auto half = row.number({"lineg", "g"}) / 2.0;
                link->set("g1", half);
                link->set("g2", half);
            }
        }
        const auto ratingA = row.number({"lineamva", "limitmvaa"});
        const auto ratingB = row.number({"lineamva:1", "limitmvab"});
        const auto ratingC = row.number({"lineamva:2", "limitmvac"});
        if (ratingA > 0.0) {
            link->set("ratinga", ratingA, units::MW);
        }
        if (ratingB > 0.0) {
            link->set("ratingb", ratingB, units::MW);
        }
        if (ratingC > 0.0) {
            link->set("ratingc", ratingC, units::MW);
        }
        if (row.has({"linestatus", "status"}) && !auxClosed(row.value({"linestatus", "status"}))) {
            link->disable();
        }
    }

    void readAuxThreeWinding(AuxNetwork& network, const AuxRow& row)
    {
        const std::array<int, 4> numbers{row.integer({"busidentifier"}),
                                         row.integer({"busidentifier:1"}),
                                         row.integer({"busidentifier:2"}),
                                         row.integer({"busidentifier:3"})};
        std::array<GridBus*, 4> buses{};
        for (size_t index = 0; index < buses.size(); ++index) {
            buses[index] = requireAuxBus(network, numbers[index], row);
        }
        const auto circuit = trimAux(row.value({"linecircuit", "circuit"}));
        const auto name = "tx3_" + std::to_string(numbers[0]) + '_' + std::to_string(numbers[1]) +
            '_' + std::to_string(numbers[2]) + '_' + circuit;
        const std::array<double, 3> pairBase{row.number({"xfmvabase"}, network.mBase),
                                             row.number({"xfmvabase:1"}, network.mBase),
                                             row.number({"xfmvabase:2"}, network.mBase)};
        std::array<double, 3> resistance{row.number({"r3w:3"}),
                                         row.number({"r3w:4"}),
                                         row.number({"r3w:5"})};
        std::array<double, 3> reactance{row.number({"x3w:3"}),
                                        row.number({"x3w:4"}),
                                        row.number({"x3w:5"})};
        for (size_t index = 0; index < 3; ++index) {
            if (pairBase[index] > 0.0) {
                resistance[index] *= network.mBase / pairBase[index];
                reactance[index] *= network.mBase / pairBase[index];
            }
        }
        // PowerWorld gives pairwise impedances. Convert them to the three
        // terminal-to-star impedances used by GridDyn's explicit star bus.
        const std::array<double, 3> starR{(resistance[0] + resistance[2] - resistance[1]) / 2.0,
                                          (resistance[0] + resistance[1] - resistance[2]) / 2.0,
                                          (resistance[1] + resistance[2] - resistance[0]) / 2.0};
        const std::array<double, 3> starX{(reactance[0] + reactance[2] - reactance[1]) / 2.0,
                                          (reactance[0] + reactance[1] - reactance[2]) / 2.0,
                                          (reactance[1] + reactance[2] - reactance[0]) / 2.0};
        std::array<double, 3> taps{row.number({"tap3w"}, 1.0),
                                   row.number({"tap3w:4"}, 1.0),
                                   row.number({"tap3w:5"}, 1.0)};
        const std::array<double, 3> nominalVoltages{row.number({"nomvolt3w"}),
                                                    row.number({"nomvolt3w:1"}),
                                                    row.number({"nomvolt3w:2"})};
        for (size_t index = 0; index < 3; ++index) {
            const auto busBase = buses[index]->get("basevoltage", units::kV);
            if (nominalVoltages[index] > 0.0 && busBase > 0.0) {
                taps[index] *= nominalVoltages[index] / busBase;
            }
        }
        const std::array<double, 3> phases{row.number({"linephase"}),
                                           row.number({"linephase:1"}),
                                           row.number({"linephase:2"})};
        const std::array<double, 3> ratings{row.number({"linemvapri"}),
                                            row.number({"linemvasec"}),
                                            row.number({"linemvater"})};
        const std::array<std::string_view, 3> statuses{row.value({"linestatus"}),
                                                       row.value({"linestatus:1"}),
                                                       row.value({"linestatus:2"})};
        for (size_t index = 0; index < 3; ++index) {
            const auto legName = name + "_w" + std::to_string(index + 1);
            const auto key = auxBranchKey(numbers[index], numbers[3], circuit);
            Link* leg = nullptr;
            // Detailed TransformerWinding rows may already have created each
            // leg. Keep their electrical data and add the 3WXFormer controls.
            if (const auto found = network.mBranches.find(key); found != network.mBranches.end()) {
                leg = found->second;
            } else {
                auto* newLeg = new AcLine(legName);
                newLeg->set("basepower", network.mBase, units::MW);
                newLeg->updateBus(buses[index], 1);
                newLeg->updateBus(buses[3], 2);
                newLeg->set("r", starR[index]);
                newLeg->set("x", starX[index]);
                addToParentWithRename(newLeg, auxLinkParent(network.mParent, newLeg));
                network.mBranches.emplace(key, newLeg);
                leg = newLeg;
            }
            if (taps[index] > 0.0) {
                leg->set("tap", taps[index]);
            }
            if (phases[index] != 0.0) {
                leg->set("tapangle", phases[index], units::deg);
            }
            if (ratings[index] > 0.0) {
                leg->set("ratinga", ratings[index], units::MW);
            }
            if (index == 0) {
                leg->set("g1", row.number({"3wxfmagetizingg:1"}));
                leg->set("b1", row.number({"3wxfmagetizingb:1"}));
            }
            if (!auxClosed(statuses[index])) {
                leg->disable();
            }
        }
    }

    void readAuxTransformerSettings(AuxNetwork& network, const AuxRow& row)
    {
        const auto from = row.integer({"busnum"});
        const auto toBus = row.integer({"busnum:1"});
        const auto circuit = trimAux(row.value({"linecircuit"}));
        const auto found = network.mBranches.find(auxBranchKey(from, toBus, circuit));
        if (found == network.mBranches.end()) {
            return;
        }
        auto* link = found->second;
        if (row.has({"linetap"})) {
            const auto tap = row.number({"linetap"});
            if (tap > 0.0) {
                link->set("tap", tap);
            }
        }
    }

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
    struct AuxGeneratorCost {
        int mBusNumber = 0;
        std::string mGeneratorId;
        std::string mModel;
        double mFixed = 0.0;
        double mFuel = 0.0;
        double mVariableOm = 0.0;
        double mIoA = 0.0;
        double mIoB = 0.0;
        double mIoC = 0.0;
        double mIoD = 0.0;
        double mMultiplier = 1.0;
        bool mHasUnsupportedExtras = false;
        std::vector<std::pair<double, double>> mBids;
    };

    void loadAuxOpfCosts(AuxNetwork& network,
                         const std::string& fileName,
                         GridDynOptimization& optimization)
    {
        std::optional<AuxGeneratorCost> pending;
        size_t unsupportedCubic = 0;
        size_t unsupportedExtras = 0;
        size_t missingBids = 0;
        size_t unmatchedGenerators = 0;
        size_t invalidBids = 0;
        const auto finish = [&]() {
            if (!pending.has_value() ||
                (pending->mModel != "cubic" && pending->mModel != "piecewise linear")) {
                return;
            }
            if (pending->mHasUnsupportedExtras) {
                ++unsupportedExtras;
                return;
            }
            auto* bus = findAuxBus(network, pending->mBusNumber);
            if (bus == nullptr) {
                ++unmatchedGenerators;
                return;
            }
            auto* generator = dynamic_cast<Generator*>(
                bus->find(bus->getName() + "_Gen_" + pending->mGeneratorId));
            if (generator == nullptr) {
                ++unmatchedGenerators;
                return;
            }
            MatPowerCostCurve curve;
            const auto multiplier = pending->mMultiplier;
            if (pending->mModel == "cubic") {
                // GridDyn's DC OPF evaluates costs through quadratic order.
                // Never silently discard a nonzero cubic term.
                if (pending->mIoD != 0.0) {
                    ++unsupportedCubic;
                    return;
                }
                curve.model = 2;
                curve.coefficients = {multiplier * pending->mFuel * pending->mIoC,
                                      multiplier *
                                          ((pending->mFuel * pending->mIoB) + pending->mVariableOm),
                                      multiplier *
                                          (pending->mFixed + (pending->mFuel * pending->mIoA))};
            } else if (pending->mModel == "piecewise linear" && !pending->mBids.empty()) {
                // PowerWorld stores the marginal price from each MW break to
                // the next. MATPOWER stores total cost at each break instead.
                curve.model = 1;
                double previousMw = 0.0;
                double cost = pending->mFixed * multiplier;
                double price = pending->mBids.front().second * multiplier;
                curve.coefficients = {0.0, cost};
                for (const auto& [breakpointMw, nextPrice] : pending->mBids) {
                    if (breakpointMw < previousMw) {
                        ++invalidBids;
                        return;
                    }
                    // Repeated MW breaks have no width. Keep the last price
                    // as the slope of the segment to their right.
                    if (breakpointMw > previousMw) {
                        cost += (breakpointMw - previousMw) * price;
                        curve.coefficients.insert(curve.coefficients.end(), {breakpointMw, cost});
                    }
                    previousMw = breakpointMw;
                    price = nextPrice * multiplier;
                }
                const double maximumMw = generator->get("pmax", units::MW);
                if (maximumMw > previousMw) {
                    cost += (maximumMw - previousMw) * price;
                    curve.coefficients.insert(curve.coefficients.end(), {maximumMw, cost});
                }
            } else {
                ++missingBids;
                return;
            }
            if (curve.valid()) {
                optimization.setGeneratorCostCurve(generator, curve);
            } else {
                ++invalidBids;
            }
        };
        scanAuxFile(
            fileName,
            [](std::string_view type) { return type == "gen"; },
            [&](const AuxRow& row) {
                finish();
                pending.reset();
                if (!row.has({"gencostmodel"})) {
                    return;
                }
                AuxGeneratorCost cost;
                cost.mBusNumber = row.integer({"busnum"});
                cost.mGeneratorId = trimAux(row.value({"genid"}));
                if (cost.mGeneratorId.empty()) {
                    cost.mGeneratorId = "1";
                }
                cost.mModel = lowerAux(trimAux(row.value({"gencostmodel"})));
                cost.mFixed = row.number({"genfixedcost"});
                cost.mFuel = row.number({"genfuelcost"});
                cost.mVariableOm = row.number({"genvariableom"});
                cost.mIoA = row.number({"genioa"});
                cost.mIoB = row.number({"geniob"});
                cost.mIoC = row.number({"genioc"});
                cost.mIoD = row.number({"geniod"});
                cost.mMultiplier = row.number({"gencostmultiplier"}, 1.0);
                // These PowerWorld adjustments need a separate, verified
                // mapping; omitting them would change the OPF objective.
                cost.mHasUnsupportedExtras = row.number({"genextrafuelcost"}) != 0.0 ||
                    row.number({"genextraomcost"}) != 0.0 || row.number({"gencostshift"}) != 0.0;
                pending = std::move(cost);
            },
            [&](std::string_view name, std::string_view line, size_t /*lineNumber*/) {
                if (!pending.has_value() || name != "bidcurve") {
                    return;
                }
                const auto values = tokenizeAuxRow(line);
                if (values.size() != 2U) {
                    throw std::runtime_error(
                        "AUX generator BidCurve requires MW and marginal price");
                }
                std::size_t used = 0;
                const double breakpointMw = std::stod(values[0], &used);
                if (used != values[0].size() || !std::isfinite(breakpointMw) ||
                    breakpointMw < 0.0) {
                    throw std::runtime_error("invalid AUX generator BidCurve MW break");
                }
                const double price = std::stod(values[1], &used);
                if (used != values[1].size() || !std::isfinite(price)) {
                    throw std::runtime_error("invalid AUX generator BidCurve marginal price");
                }
                pending->mBids.emplace_back(breakpointMw, price);
            });
        finish();
        if (unsupportedCubic + unsupportedExtras + missingBids + unmatchedGenerators +
                invalidBids !=
            0U) {
            optimization.log(&optimization,
                             PrintLevel::WARNING,
                             "AUX OPF costs skipped: " + std::to_string(unsupportedCubic) +
                                 " nonzero cubic terms, " + std::to_string(unsupportedExtras) +
                                 " unmodeled cost adjustments, " + std::to_string(missingBids) +
                                 " missing BidCurve data, " + std::to_string(unmatchedGenerators) +
                                 " unmatched generators, " + std::to_string(invalidBids) +
                                 " invalid curves");
        }
    }
#endif

}  // namespace

void loadAux(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    if (parentObject == nullptr) {
        throw InvalidParameterValue("PowerWorld AUX reader requires a simulation parent object");
    }
    AuxNetwork network{.mParent = parentObject,
                       .mOptions = readerOptions,
                       .mAreas = {},
                       .mBuses = {},
                       .mBranches = {},
                       .mGeneratorsByBus = {},
                       .mRemoteGroups = {},
                       .mRemoteTerminalBuses = {},
                       .mBase = readerOptions.base,
                       .mExistingNetwork = parentObject->getRoot()->getInt("totalbuscount") > 0};
    if (!network.mExistingNetwork) {
        GridSimulation::resetObjectCounters();
    }
    // Stream the network in three passes rather than buffering it. Areas and
    // base power precede buses and equipment; optimization costs need a fourth
    // pass after their generator objects exist.
    scanAuxFile(
        fileName,
        [](std::string_view type) {
            return type == "area" || type == "sim_solution_options_value";
        },
        [&](const AuxRow& row) { readAuxMetadata(network, row); });
    scanAuxFile(
        fileName,
        [](std::string_view type) { return type == "bus"; },
        [&](const AuxRow& row) { readAuxBus(network, row); });
    scanAuxFile(
        fileName,
        [](std::string_view type) {
            return type == "gen" || type == "load" || type == "shunt" || type == "branch" ||
                type == "3wxformer" || type == "transformer";
        },
        [&](const AuxRow& row) {
            if (row.mSection.mType == "gen") {
                if (row.has({"genmwsetpoint", "genmw", "genmwmax", "genmwmin", "genstatus"})) {
                    readAuxGenerator(network, row);
                }
            } else if (row.mSection.mType == "load") {
                readAuxLoad(network, row);
            } else if (row.mSection.mType == "shunt") {
                readAuxShunt(network, row);
            } else if (row.mSection.mType == "branch") {
                readAuxBranch(network, row);
            } else if (row.mSection.mType == "3wxformer") {
                readAuxThreeWinding(network, row);
            } else if (row.mSection.mType == "transformer") {
                readAuxTransformerSettings(network, row);
            }
        });
    finishAuxGeneratorControls(network);
#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
    auto* optimization = dynamic_cast<GridDynOptimization*>(parentObject->getRoot());
    if (optimization != nullptr) {
        loadAuxOpfCosts(network, fileName, *optimization);
    }
#endif
}

AuxCaseMetadata readAuxCaseMetadata(const std::string& fileName)
{
    AuxCaseMetadata metadata;
    scanAuxFile(
        fileName,
        [](std::string_view type) {
            return type == "owner" || type == "zone" || type == "substation" ||
                type == "superarea" || type == "balancingauthority" || type == "bus";
        },
        [&](const AuxRow& row) {
            const auto& type = row.mSection.mType;
            if (type == "owner") {
                const int number = row.integer({"ownernum"});
                if (number <= 0) {
                    return;
                }
                auto& owner = metadata.mOwners[number];
                owner.mNumber = number;
                if (auto name = trimAux(row.value({"ownername"})); !name.empty()) {
                    owner.mName = std::move(name);
                }
            } else if (type == "zone") {
                const int number = row.integer({"zonenum"});
                if (number <= 0) {
                    return;
                }
                auto& zone = metadata.mZones[number];
                zone.mNumber = number;
                if (auto name = trimAux(row.value({"zonename"})); !name.empty()) {
                    zone.mName = std::move(name);
                }
            } else if (type == "balancingauthority") {
                const int number = row.integer({"banumber"});
                if (number <= 0) {
                    return;
                }
                auto& authority = metadata.mBalancingAuthorities[number];
                authority.mNumber = number;
                if (auto name = trimAux(row.value({"baname"})); !name.empty()) {
                    authority.mName = std::move(name);
                }
            } else if (type == "substation") {
                const int number = row.integer({"subnum"});
                if (number <= 0) {
                    return;
                }
                auto& substation = metadata.mSubstations[number];
                substation.mNumber = number;
                if (auto name = trimAux(row.value({"subname"})); !name.empty()) {
                    substation.mName = std::move(name);
                }
                if (auto latitude = auxOptionalNumber(row, {"latitude"}); latitude.has_value()) {
                    substation.mLatitude = latitude;
                }
                if (auto longitude = auxOptionalNumber(row, {"longitude"}); longitude.has_value()) {
                    substation.mLongitude = longitude;
                }
            } else if (type == "superarea") {
                auto name = trimAux(row.value({"saname"}));
                if (!name.empty() &&
                    std::find(metadata.mSuperAreas.begin(), metadata.mSuperAreas.end(), name) ==
                        metadata.mSuperAreas.end()) {
                    metadata.mSuperAreas.push_back(std::move(name));
                }
            } else if (type == "bus") {
                const int number = row.integer({"busnum"});
                if (number <= 0) {
                    return;
                }
                auto& bus = metadata.mBuses[number];
                bus.mBusNumber = number;
                // A later Bus section may supply only a few fields. Retain
                // assignments obtained from earlier sections of the same file.
                if (row.has({"areanum"})) {
                    bus.mAreaNumber = row.integer({"areanum"});
                }
                if (row.has({"zonenum"})) {
                    bus.mZoneNumber = row.integer({"zonenum"});
                }
                if (row.has({"ownernum"})) {
                    bus.mOwnerNumber = row.integer({"ownernum"});
                }
                if (row.has({"subnum"})) {
                    bus.mSubstationNumber = row.integer({"subnum"});
                }
                if (row.has({"banumber"})) {
                    bus.mBalancingAuthorityNumber = row.integer({"banumber"});
                }
                if (auto latitude = auxOptionalNumber(row, {"latitude:1", "latitude"});
                    latitude.has_value()) {
                    bus.mLatitude = latitude;
                }
                if (auto longitude = auxOptionalNumber(row, {"longitude:1", "longitude"});
                    longitude.has_value()) {
                    bus.mLongitude = longitude;
                }
            }
        });
    return metadata;
}

}  // namespace griddyn
