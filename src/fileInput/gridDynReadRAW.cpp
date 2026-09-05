/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/CoreOwningPtr.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/links/RawDcLine.h"
#include "griddyn/links/ThreeWindingTransformer.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/primary/AcBus.h"
#include "readerHelper.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::convertToUpperCase;
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::stringVector;
using gmlc::utilities::stringOps::removeQuotes;
using gmlc::utilities::stringOps::splitline;
using gmlc::utilities::stringOps::splitlineQuotes;
using gmlc::utilities::stringOps::trim;
using gmlc::utilities::stringOps::trimString;
using units::deg;
using units::MVAR;
using units::MW;

using ImpedanceCorrectionTable = std::vector<std::pair<double, double>>;
using ImpedanceCorrectionTables = std::unordered_map<int, ImpedanceCorrectionTable>;

static double correctionFactor(const ImpedanceCorrectionTables& tables, int tableId, double tap)
{
    const auto table = tables.find(tableId);
    if ((tableId == 0) || (table == tables.end()) || table->second.empty()) {
        return 1.0;
    }
    const auto& points = table->second;
    if (tap <= points.front().first) {
        return points.front().second;
    }
    if (tap >= points.back().first) {
        return points.back().second;
    }
    const auto upper =
        std::lower_bound(points.begin(), points.end(), tap, [](const auto& point, double value) {
            return point.first < value;
        });
    const auto lower = std::prev(upper);
    const auto fraction = (tap - lower->first) / (upper->first - lower->first);
    return lower->second + (fraction * (upper->second - lower->second));
}

static ImpedanceCorrectionTables readImpedanceCorrectionTables(const std::string& fileName,
                                                               int rawVersion)
{
    ImpedanceCorrectionTables tables;
    std::ifstream file(fileName, std::ios::in);
    std::string line;
    bool inCorrectionSection = false;
    int currentTableId = 0;
    while (std::getline(file, line)) {
        if (!inCorrectionSection) {
            inCorrectionSection = line.contains("BEGIN IMPEDANCE CORRECTION DATA");
            continue;
        }
        trimString(line);
        if (line.empty()) {
            continue;
        }
        if (line[0] == '0') {
            break;
        }
        const auto fields = splitline(line);
        if (fields.size() < 2) {
            continue;
        }
        const auto firstField = trim(fields[0]);
        const auto isTableId = firstField.find_first_of(".eE") == std::string_view::npos;
        size_t firstPoint = 0;
        if (isTableId) {
            currentTableId = numeric_conversion<int>(firstField, 0);
            firstPoint = 1;
        }
        if (currentTableId == 0) {
            continue;
        }
        auto& points = tables[currentTableId];
        // Through RAW v34 each point is T,F. RAW v35 extends F to a complex
        // value encoded as T,Re(F),Im(F). Continuation cards omit the table id.
        const size_t fieldStride = (rawVersion >= 35) ? 3U : 2U;
        for (size_t ii = firstPoint; ii + 1 < fields.size(); ii += fieldStride) {
            const auto tap = numeric_conversion<double>(fields[ii], 0.0);
            const auto factor = numeric_conversion<double>(fields[ii + 1], 1.0);
            const auto imaginary = (fieldStride == 3U && ii + 2 < fields.size()) ?
                numeric_conversion<double>(fields[ii + 2], 0.0) :
                0.0;
            // PSS/E pads incomplete table cards with a zero triple. It is not
            // a table point; retaining it duplicates T=0 with factor zero.
            if ((tap != 0.0) || (factor != 0.0) || (imaginary != 0.0)) {
                points.emplace_back(tap, factor);
            }
        }
    }
    for (auto& [tableId, points] : tables) {
        std::sort(points.begin(), points.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
    }
    return tables;
}

static int getPSSversion(const std::string& line);
static void rawReadBus(GridBus* bus, const std::string& line, BasicReaderInfo& opt);
static void rawReadLoad(GridLoad* loadObject, const std::string& line, BasicReaderInfo& opt);
static void rawReadFixedShunt(GridLoad* loadObject, const std::string& line, BasicReaderInfo& opt);
static void rawReadGen(Generator* gen, const std::string& line, BasicReaderInfo& opt);
static void rawReadBranch(CoreObject* parentObject,
                          const std::string& line,
                          std::vector<GridBus*>& busList,
                          BasicReaderInfo& opt);
static int rawReadTX(CoreObject* parentObject,
                     stringVec& txlines,
                     std::vector<GridBus*>& busList,
                     BasicReaderInfo& opt,
                     const ImpedanceCorrectionTables& correctionTables);
static void rawReadThreeWindingTransformer(CoreObject* parentObject,
                                           const stringVec& header,
                                           const stringVec& impedance,
                                           const std::array<stringVec, 3>& windings,
                                           std::vector<GridBus*>& busList,
                                           BasicReaderInfo& opt,
                                           const ImpedanceCorrectionTables& correctionTables)
{
    const auto busNumber1 = numeric_conversion<int>(header[0], 0);
    const auto busNumber2 = numeric_conversion<int>(header[1], 0);
    const auto busNumber3 = numeric_conversion<int>(header[2], 0);
    if ((busNumber1 <= 0) || (busNumber2 <= 0) || (busNumber3 <= 0) ||
        (std::cmp_greater_equal(static_cast<size_t>(busNumber1), busList.size())) ||
        (std::cmp_greater_equal(static_cast<size_t>(busNumber2), busList.size())) ||
        (std::cmp_greater_equal(static_cast<size_t>(busNumber3), busList.size()) ||
         (busList[busNumber1] == nullptr) || (busList[busNumber2] == nullptr) ||
         (busList[busNumber3] == nullptr))) {
        throw std::runtime_error("invalid three-winding transformer bus");
    }

    const auto circuit = trim(removeQuotes(header[3]));
    auto name = std::string{"tx3_"} + std::to_string(busNumber1) + '_' +
        std::to_string(busNumber2) + '_' + std::to_string(busNumber3) + '_' + circuit;
    // Some RAW section readers present the final transformer record twice.
    // Avoid adding a duplicate parallel three-leg network for that replay.
    if (parentObject->find(name + "_w1-2") != nullptr) {
        return;
    }
    auto* starBus = new AcBus(name + "_star");
    starBus->set("basepower", opt.base);
    starBus->set("basevoltage", 1.0, units::kV);
    starBus->setVoltageAngle(numeric_conversion<double>(impedance[9], 1.0),
                             units::convert(numeric_conversion<double>(impedance[10], 0.0),
                                            deg,
                                            units::rad));
    addToParentWithRename(starBus, parentObject);

    // PSS/E stores the three pairwise leakage impedances.  Convert their
    // equivalent delta into the star legs used by ThreeWindingTransformer.
    std::array<double, 3> resistance{numeric_conversion<double>(impedance[0], 0.0),
                                     numeric_conversion<double>(impedance[3], 0.0),
                                     numeric_conversion<double>(impedance[6], 0.0)};
    std::array<double, 3> reactance{numeric_conversion<double>(impedance[1], 0.0),
                                    numeric_conversion<double>(impedance[4], 0.0),
                                    numeric_conversion<double>(impedance[7], 0.0)};
    const std::array<double, 3> windingBase{numeric_conversion<double>(impedance[2], opt.base),
                                            numeric_conversion<double>(impedance[5], opt.base),
                                            numeric_conversion<double>(impedance[8], opt.base)};
    const std::array<GridBus*, 3> exterior{busList[busNumber1],
                                           busList[busNumber2],
                                           busList[busNumber3]};
    const auto impedanceCode = numeric_conversion<int>(header[5], 1);
    for (size_t ii = 0; ii < 3; ++ii) {
        if (impedanceCode == 2) {
            if (windingBase[ii] > 0.0) {
                const auto nominalVoltage = numeric_conversion<double>(windings[ii][1], 0.0);
                const auto busBaseVoltage = exterior[ii]->get("basevoltage");
                const auto voltageScale = (nominalVoltage > 0.0 && busBaseVoltage > 0.0) ?
                    nominalVoltage / busBaseVoltage :
                    1.0;
                const auto impedanceScale =
                    opt.base / windingBase[ii] * voltageScale * voltageScale;
                resistance[ii] *= impedanceScale;
                reactance[ii] *= impedanceScale;
            }
        } else if (impedanceCode == 3) {
            // CZ=3 uses load loss in W and impedance magnitude on winding base.
            if (windingBase[ii] > 0.0) {
                resistance[ii] /= windingBase[ii] * 1.0e6;
                reactance[ii] = std::sqrt(
                    std::max((reactance[ii] * reactance[ii]) - (resistance[ii] * resistance[ii]),
                             0.0));
                const auto nominalVoltage = numeric_conversion<double>(windings[ii][1], 0.0);
                const auto busBaseVoltage = exterior[ii]->get("basevoltage");
                const auto voltageScale = (nominalVoltage > 0.0 && busBaseVoltage > 0.0) ?
                    nominalVoltage / busBaseVoltage :
                    1.0;
                const auto impedanceScale =
                    opt.base / windingBase[ii] * voltageScale * voltageScale;
                resistance[ii] *= impedanceScale;
                reactance[ii] *= impedanceScale;
            }
        }
    }
    std::array<double, 3> starResistance{(resistance[0] + resistance[2] - resistance[1]) / 2.0,
                                         (resistance[0] + resistance[1] - resistance[2]) / 2.0,
                                         (resistance[1] + resistance[2] - resistance[0]) / 2.0};
    std::array<double, 3> starReactance{(reactance[0] + reactance[2] - reactance[1]) / 2.0,
                                        (reactance[0] + reactance[1] - reactance[2]) / 2.0,
                                        (reactance[1] + reactance[2] - reactance[0]) / 2.0};
    const size_t windingTableIndex = (opt.version >= 35) ? 23U : 13U;
    const auto impedanceCorrection =
        correctionFactor(correctionTables,
                         numeric_conversion<int>(windings[0][windingTableIndex], 0),
                         numeric_conversion<double>(windings[0][2], 0.0));
    starResistance[0] *= impedanceCorrection;
    starReactance[0] *= impedanceCorrection;
    const auto tapCode = numeric_conversion<int>(header[4], 1);
    for (size_t ii = 0; ii < 3; ++ii) {
        auto tap = numeric_conversion<double>(windings[ii][0], 1.0);
        if (tap == 0.0) {
            tap = 1.0;
        }
        const auto busBaseVoltage = exterior[ii]->get("basevoltage");
        if ((tapCode == 2) && (busBaseVoltage > 0.0)) {
            // WINDV is in kV for CW=2.
            tap /= busBaseVoltage;
        } else if (tapCode == 3) {
            // WINDV is per-unit on the nominal winding voltage for CW=3.
            const auto nominalVoltage = numeric_conversion<double>(windings[ii][1], 0.0);
            if ((nominalVoltage > 0.0) && (busBaseVoltage > 0.0)) {
                tap *= nominalVoltage / busBaseVoltage;
            }
        }
        auto* leg = new AcLine(name + "_w" + std::to_string(ii + 1));
        leg->set("basepower", opt.base);
        leg->updateBus(exterior[ii], 1);
        leg->updateBus(starBus, 2);
        leg->set("r", starResistance[ii]);
        leg->set("x", starReactance[ii]);
        leg->set("tap", tap);
        leg->set("tapangle", numeric_conversion<double>(windings[ii][2], 0.0), deg);
        leg->set("ratinga", numeric_conversion<double>(windings[ii][3], 0.0), MW);
        leg->set("ratingb", numeric_conversion<double>(windings[ii][4], 0.0), MW);
        leg->set("ratingc", numeric_conversion<double>(windings[ii][5], 0.0), MW);
        if ((ii == 0) && (numeric_conversion<int>(header[6], 1) == 1)) {
            leg->set("g", numeric_conversion<double>(header[7], 0.0));
            leg->set("b", numeric_conversion<double>(header[8], 0.0));
        }
        const auto status = numeric_conversion<int>(header[11], 1);
        const bool windingOutOfService = (status == 0) || ((status == 2) && (ii == 1)) ||
            ((status == 3) && (ii == 2)) || ((status == 4) && (ii == 0));
        if (windingOutOfService) {
            leg->disable();
        }
        addToParentWithRename(leg, parentObject);
    }
    if (numeric_conversion<int>(header[6], 1) != 1) {
        std::cerr << "three-winding transformer magnetizing code is not fully supported\n";
    }
}

static int rawReadTxV33(CoreObject* parentObject,
                        stringVec& txlines,
                        std::vector<GridBus*>& busList,
                        BasicReaderInfo& opt,
                        const ImpedanceCorrectionTables& correctionTables);

static void rawReadSwitchedShunt(CoreObject* parentObject,
                                 const std::string& line,
                                 std::vector<GridBus*>& busList,
                                 BasicReaderInfo& opt);
static void rawReadTXadj(CoreObject* parentObject,
                         const std::string& line,
                         std::vector<GridBus*>& busList,
                         BasicReaderInfo& opt);

static void rawReadTwoTerminalDc(CoreObject* parentObject,
                                 const std::array<std::string, 3>& records,
                                 const std::vector<GridBus*>& busList,
                                 index_t sequence,
                                 std::unordered_set<int>& voltageControlledBuses);
static void rawReadVscDc(CoreObject* parentObject,
                         const std::array<std::string, 3>& records,
                         const std::vector<GridBus*>& busList,
                         index_t sequence,
                         std::unordered_set<int>& voltageControlledBuses);

// static int rawReadDCLine(CoreObject* parentObject,
//                          stringVec& txlines,
//                          std::vector<GridBus*>& busList,
//                          BasicReaderInfo& opt);

namespace {
    enum class SectionType : std::uint8_t {
        UNKNOWN,
        BUS,
        BRANCH,
        LOAD,
        FIXED_SHUNT,
        GENERATOR,
        TX,
        SWITCHED_SHUNT,
        TXADJ,
        TWO_TERMINAL_DC,
        VSC_DC
    };
}  // namespace

// get the basic busFactory
static TypeFactory<GridBus>* gBusfactory = nullptr;

// get the basic load Factory
static TypeFactory<GridLoad>* gLdfactory = nullptr;
// get the basic Link Factory
static ChildTypeFactory<AcLine, Link>* gLinkfactory = nullptr;
// get the basic Generator Factory
static TypeFactory<Generator>* gGenfactory = nullptr;

static SectionType findSectionType(const std::string& line);

static bool checkNextLine(std::ifstream& file, std::string& nextLine)
{
    while (std::getline(file, nextLine)) {
        trimString(nextLine);
        // PSS/E v35 may emit field-definition cards before the records in a
        // section. They begin with @! and are not data cards.
        if (nextLine.empty() || nextLine.starts_with("@!")) {
            continue;
        }
        return (nextLine[0] != '0');
    }
    return false;
}

static GridBus* findBus(std::vector<GridBus*>& busList, const std::string& line)
{
    auto pos = line.find_first_of(',');
    const auto temp1 = gmlc::utilities::string_viewOps::trim(std::string_view{line}.substr(0, pos));

    auto index = gmlc::utilities::numConv<std::size_t>(temp1);

    if (index >= busList.size()) {
        std::cerr << "Invalid bus number" << index << '\n';
        return nullptr;
    }
    return busList[index];
}

static void readRawBusSection(CoreObject* parentObject,
                              std::ifstream& file,
                              std::string& line,
                              std::vector<GridBus*>& busList,
                              BasicReaderInfo& opt)
{
    while (checkNextLine(file, line)) {
        const auto pos = line.find_first_of(',');
        const auto index = numeric_conversion<index_t>(line.substr(0, pos), 0);

        if (std::cmp_greater_equal(index, busList.size())) {
            if (index < 100000000) {
                busList.resize((2 * index) + 1, nullptr);
            } else {
                std::cerr << "Bus index overload " << index << '\n';
            }
        }
        if (busList[index] == nullptr) {
            busList[index] = gBusfactory->makeTypeObject();
            busList[index]->set("basepower", opt.base);
            busList[index]->setUserID(index);

            rawReadBus(busList[index], line, opt);
            auto* tobj = parentObject->find(busList[index]->getName());
            if (tobj == nullptr) {
                parentObject->add(busList[index]);
            } else {
                const auto prevName = busList[index]->getName();
                busList[index]->setName(prevName + '_' +
                                        std::to_string(busList[index]->getInt("basevoltage")));
                try {
                    parentObject->add(busList[index]);
                }
                catch (const ObjectAddFailure&) {
                    busList[index]->setName(prevName);
                    addToParentWithRename(busList[index], parentObject);
                }
            }
        } else {
            std::cerr << "Invalid bus code " << index << '\n';
        }
    }
}

void loadRaw(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    ImpedanceCorrectionTables impedanceCorrectionTables;
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string temp1;  // temporary storage for substrings
    std::vector<GridBus*> busList;
    BasicReaderInfo readerOptionsCopy(readerOptions);
    auto& opt = readerOptionsCopy;
    GridLoad* loadObject;
    Generator* gen;
    GridBus* bus;
    size_t pos;

    /*load up the factories*/
    if (gBusfactory == nullptr) {
        // get the basic busFactory
        gBusfactory = static_cast<decltype(gBusfactory)>(
            CoreObjectFactory::instance()->getFactory("bus")->getFactory(""));

        // get the basic load Factory
        gLdfactory = static_cast<decltype(gLdfactory)>(
            CoreObjectFactory::instance()->getFactory("load")->getFactory(""));

        // get the basic load Factory
        gGenfactory = static_cast<decltype(gGenfactory)>(
            CoreObjectFactory::instance()->getFactory("generator")->getFactory(""));
        // get the basic link Factory
        gLinkfactory = static_cast<decltype(gLinkfactory)>(
            CoreObjectFactory::instance()->getFactory("link")->getFactory(""));
    }
    /* Process the first line
    First card in file.

    Columns  2- 9   Date, in format DD/MM/state with leading zeros. If no date
            provided, use 0b/0b/0b where b is blank.
    Columns 11-30   Originator's name (A)
    Columns 32-37   MVA Base (F*)
    Columns 39-42   Year (I)
    Column  44      Season (S - Summer, W - Winter)
    Column  46-73   Case identification (A) */

    // reset all the object counters
    GridSimulation::resetObjectCounters();
    // get the base scenario information
    if (std::getline(file, line)) {
        // PSS/E v35 writes an @!IC field-definition card before the actual
        // case-identification record.
        if (line.starts_with("@!IC") && !std::getline(file, line)) {
            return;
        }
        // auto res = sscanf(
        //    line.c_str(), "%*d, %lf, %d,%*d,%*d,%lf", &(opt.base), &(opt.version),
        //    &(opt.basefreq));

        auto strvec = splitlineQuotes(line);
        if (strvec.size() >= 6) {
            readerOptionsCopy.base = numeric_conversion<double>(strvec[1], 100.0);
            readerOptionsCopy.version = numeric_conversion<int>(strvec[2], 0);
            readerOptionsCopy.basefreq = numeric_conversion<double>(strvec[5], 60.0);
        }
        if (readerOptionsCopy.base != 100.0) {
            parentObject->set("basepower", readerOptionsCopy.base);
        }
        // temp1=line.substr(45,27);
        // parentObject->set("name",&temp1);
        // if (res > 2) {
        if (readerOptionsCopy.basefreq != 60.0) {
            parentObject->set("basefreq", readerOptionsCopy.basefreq);
        }
        //}

        if (readerOptionsCopy.version == 0) {
            readerOptionsCopy.version = getPSSversion(line);
        }
    }
    impedanceCorrectionTables = readImpedanceCorrectionTables(fileName, opt.version);
    if (std::getline(file, line)) {
        pos = line.find_first_of(',');
        temp1 = line.substr(0, pos);
        trimString(temp1);
        parentObject->setName(temp1);
    }
    temp1 = line;
    // get the second comment line and ignore it
    std::getline(file, line);
    temp1 = temp1 + '\n' + line;
    // set the case description
    parentObject->setDescription(temp1);
    // PSS/E v35 RAW files can include a system-wide-data block between the
    // two comment lines and the bus section.  Skip it through its explicit
    // bus-section marker before treating subsequent records as bus cards.
    if (opt.version >= 35) {
        while (!line.contains("BEGIN BUS DATA") && std::getline(file, line)) {
            trimString(line);
        }
    }
    // Bus data does not have a header but is always the first section.
    readRawBusSection(parentObject, file, line, busList, opt);

    stringVec txlines;
    txlines.resize(5);
    int tline = 5;
    index_t dcLineSequence = 0;
    std::unordered_set<int> dcVoltageControlledBuses;

    bool moreSections = true;

    while (moreSections) {
        const SectionType currSection = findSectionType(line);
        bool moreData = true;
        switch (currSection) {
            case SectionType::LOAD:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        bus = findBus(busList, line);
                        if (bus != nullptr) {
                            loadObject = gLdfactory->makeTypeObject();
                            bus->add(loadObject);
                            rawReadLoad(loadObject, line, opt);
                        } else {
                            std::cerr << "Invalid bus number for load " << line.substr(0, 30)
                                      << '\n';
                        }
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::GENERATOR:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        bus = findBus(busList, line);
                        if (bus != nullptr) {
                            gen = gGenfactory->makeTypeObject();
                            bus->add(gen);
                            rawReadGen(gen, line, opt);
                        } else {
                            std::cerr << "Invalid bus number for fixed shunt " << line.substr(0, 30)
                                      << '\n';
                        }
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::BRANCH:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        rawReadBranch(parentObject, line, busList, opt);
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::FIXED_SHUNT:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        bus = findBus(busList, line);
                        if (bus != nullptr) {
                            loadObject = gLdfactory->makeTypeObject();
                            bus->add(loadObject);
                            rawReadFixedShunt(loadObject, line, opt);
                        } else {
                            std::cerr << "Invalid bus number for fixed shunt " << line.substr(0, 30)
                                      << '\n';
                        }
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::SWITCHED_SHUNT:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        rawReadSwitchedShunt(parentObject, line, busList, opt);
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::TXADJ:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        rawReadTXadj(parentObject, line, busList, opt);
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::TX:

                while (moreData) {
                    if (tline == 5) {
                        if (checkNextLine(file, line)) {
                            txlines[0] = line;
                            std::getline(file, txlines[1]);
                            std::getline(file, txlines[2]);
                            std::getline(file, txlines[3]);
                            std::getline(file, txlines[4]);
                        } else {
                            moreData = false;
                        }
                    } else {
                        temp1 = txlines[4];
                        trimString(temp1);
                        if (temp1[0] == '0') {
                            moreData = false;
                            continue;
                        }
                        txlines[0] = temp1;
                        std::getline(file, txlines[1]);
                        std::getline(file, txlines[2]);
                        std::getline(file, txlines[3]);
                        std::getline(file, txlines[4]);
                    }
                    if (!moreData) {
                        break;
                    }
                    if (opt.version >= 33) {
                        tline = rawReadTxV33(
                            parentObject, txlines, busList, opt, impedanceCorrectionTables);
                    } else {
                        tline = rawReadTX(
                            parentObject, txlines, busList, opt, impedanceCorrectionTables);
                    }
                }
                break;
            case SectionType::TWO_TERMINAL_DC:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        std::array<std::string, 3> records{line, {}, {}};
                        if (!std::getline(file, records[1]) || !std::getline(file, records[2])) {
                            std::cerr << "Incomplete two-terminal DC record\n";
                            moreData = false;
                            moreSections = false;
                            continue;
                        }
                        trimString(records[1]);
                        trimString(records[2]);
                        if (records[1].empty() || records[2].empty() || records[1][0] == '0' ||
                            records[2][0] == '0') {
                            std::cerr << "Incomplete two-terminal DC record\n";
                            moreData = false;
                            continue;
                        }
                        rawReadTwoTerminalDc(parentObject,
                                             records,
                                             busList,
                                             ++dcLineSequence,
                                             dcVoltageControlledBuses);
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::VSC_DC:
                while (moreData) {
                    if (checkNextLine(file, line)) {
                        std::array<std::string, 3> records{line, {}, {}};
                        if (!std::getline(file, records[1]) || !std::getline(file, records[2])) {
                            std::cerr << "Incomplete VSC DC record\n";
                            moreData = false;
                            moreSections = false;
                            continue;
                        }
                        trimString(records[1]);
                        trimString(records[2]);
                        if (records[1].empty() || records[2].empty() || records[1][0] == '0' ||
                            records[2][0] == '0') {
                            std::cerr << "Incomplete VSC DC record\n";
                            moreData = false;
                            continue;
                        }
                        rawReadVscDc(parentObject,
                                     records,
                                     busList,
                                     ++dcLineSequence,
                                     dcVoltageControlledBuses);
                    } else {
                        moreData = false;
                    }
                }
                break;
            case SectionType::UNKNOWN:
            default:
                while (moreData) {
                    if (std::getline(file, line)) {
                        trimString(line);
                        if (line[0] == '0') {
                            moreData = false;
                            continue;
                        }
                    } else {
                        moreData = false;
                        moreSections = false;
                    }
                }
                break;
        }
    }

    file.close();
}

static GridBus* rawDcLookupBus(const std::vector<GridBus*>& busList, int busNumber)
{
    if ((busNumber <= 0) || std::cmp_greater_equal(busNumber, busList.size()) ||
        busList[busNumber] == nullptr) {
        return nullptr;
    }
    return busList[busNumber];
}

static double rawDcField(const stringVector& record, size_t index);

/**
 * Add the simple, AC-terminal DC-line representation used by PowerModels for
 * PSS/E RAW imports.  RawDcLine represents the scheduled active transfer and
 * PowerModels-style terminal reactive/voltage equations, rather than a
 * physical DC network or converter.  This preserves the existing physical DC
 * and converter models.
 */
static links::RawDcLine* addRawDcCompatibilityLink(CoreObject* parentObject,
                                                   GridBus* fromBus,
                                                   GridBus* toBus,
                                                   index_t sequence,
                                                   double scheduledPower,
                                                   double lossFraction,
                                                   double rating,
                                                   bool enabled,
                                                   double fromVoltageTarget,
                                                   double toVoltageTarget,
                                                   bool controlFromVoltage,
                                                   bool controlToVoltage,
                                                   const std::string& description)
{
    auto* link =
        new links::RawDcLine(parentObject->getName() + "_psse_dc_" + std::to_string(sequence));
    link->setDescription(description);
    link->updateBus(fromBus, 1);
    link->updateBus(toBus, 2);
    try {
        parentObject->add(link);
    }
    catch (const ObjectAddFailure&) {
        addToParentWithRename(link, parentObject);
    }

    link->set("pset", scheduledPower, MW);
    link->set("lossfraction", lossFraction);
    link->set("from_vtarget", fromVoltageTarget);
    link->set("to_vtarget", toVoltageTarget);
    link->set("from_voltage_control", controlFromVoltage ? 1.0 : 0.0);
    link->set("to_voltage_control", controlToVoltage ? 1.0 : 0.0);
    if (rating > 0.0) {
        link->set("rating", rating, MW);
    }
    if (!enabled) {
        // Keep an out-of-service DC record from contributing to either AC
        // terminal without allowing Link::disable() to cascade to a bus.
        link->switchMode(1, true);
        link->switchMode(2, true);
    }
    return link;
}

static bool rawDcUseVoltageControl(GridBus* bus,
                                   int busNumber,
                                   std::unordered_set<int>& voltageControlledBuses)
{
    auto* acBus = dynamic_cast<AcBus*>(bus);
    if ((acBus == nullptr) ||
        (acBus->getMode(cPflowSolverMode) != static_cast<int>(GridBus::BusType::PQ))) {
        return false;
    }
    // PowerModels adds a voltage equality for every dcline terminal.  The
    // equations are redundant if several RAW DC records share a PQ bus.  Keep
    // one controller and retain the other terminal's specified q=0 start
    // value, producing a nonsingular GridDyn system with the same voltage.
    return voltageControlledBuses.insert(busNumber).second;
}

static void rawReadTwoTerminalDc(CoreObject* parentObject,
                                 const std::array<std::string, 3>& records,
                                 const std::vector<GridBus*>& busList,
                                 index_t sequence,
                                 std::unordered_set<int>& voltageControlledBuses)
{
    const auto header = splitlineQuotes(records[0]);
    const auto rectifier = splitline(records[1]);
    const auto inverter = splitline(records[2]);
    if ((header.size() < 5) || rectifier.empty() || inverter.empty()) {
        std::cerr << "Invalid two-terminal DC record\n";
        return;
    }

    const auto mdc = numeric_conversion<int>(header[1], 0);
    const auto setvl = numeric_conversion<double>(header[3], 0.0);
    const auto vschd = numeric_conversion<double>(header[4], 0.0);
    double powerDemand = 0.0;
    if (mdc == 1) {
        powerDemand = std::abs(setvl);
    } else if (mdc == 2) {
        if (vschd == 0.0) {
            std::cerr << "Two-terminal DC current-control record has zero VSCHD\n";
        } else {
            // Match PowerModels' PSS/E RAW dcline conversion exactly.
            powerDemand = std::abs(setvl / vschd / 1000.0);
        }
    }

    const auto fromBusNumber = numeric_conversion<int>(rectifier[0], 0);
    const auto toBusNumber = numeric_conversion<int>(inverter[0], 0);
    auto* fromBus = rawDcLookupBus(busList, fromBusNumber);
    auto* toBus = rawDcLookupBus(busList, toBusNumber);
    if ((fromBus == nullptr) || (toBus == nullptr)) {
        std::cerr << "Invalid AC bus in two-terminal DC record\n";
        return;
    }

    const auto name = std::string(trim(removeQuotes(header[0])));
    const auto resistance = numeric_conversion<double>(header[2], 0.0);
    const auto fromVoltageTarget = fromBus->getVoltage();
    const auto toVoltageTarget = toBus->getVoltage();
    const auto fromQmin = -powerDemand * std::cos(rawDcField(rectifier, 3) * kPI / 180.0);
    const auto toQmin = -powerDemand * std::cos(rawDcField(inverter, 3) * kPI / 180.0);
    addRawDcCompatibilityLink(
        parentObject,
        fromBus,
        toBus,
        sequence,
        powerDemand,
        0.0,
        powerDemand,
        mdc != 0,
        fromVoltageTarget,
        toVoltageTarget,
        rawDcUseVoltageControl(fromBus, fromBusNumber, voltageControlledBuses),
        rawDcUseVoltageControl(toBus, toBusNumber, voltageControlledBuses),
        "PSS/E RAW two-terminal DC compatibility import; name='" + name +
            "', MDC=" + std::to_string(mdc) + ", RDC=" + std::to_string(resistance) +
            ", SETVL=" + std::to_string(setvl) + ", VSCHD=" + std::to_string(vschd) + ", qminf=" +
            std::to_string(fromQmin) + ", qmaxf=0, qmint=" + std::to_string(toQmin) + ", qmaxt=0");
}

static double rawDcField(const stringVector& record, size_t index)
{
    return (index < record.size()) ? numeric_conversion<double>(record[index], 0.0) : 0.0;
}

static double rawVscTransferLimit(const stringVector& converter)
{
    // PSS/E VSC converter fields: SMAX, IMAX, PWF, MAXQ, MINQ start at 8.
    if (converter.size() < 13) {
        return 0.0;
    }
    const auto smax = rawDcField(converter, 8);
    const auto imax = rawDcField(converter, 9);
    if ((smax == 0.0) && (imax == 0.0)) {
        return std::max(std::abs(rawDcField(converter, 11)), std::abs(rawDcField(converter, 12)));
    }
    return std::min(imax, smax);
}

static void rawReadVscDc(CoreObject* parentObject,
                         const std::array<std::string, 3>& records,
                         const std::vector<GridBus*>& busList,
                         index_t sequence,
                         std::unordered_set<int>& voltageControlledBuses)
{
    const auto header = splitlineQuotes(records[0]);
    const auto fromConverter = splitline(records[1]);
    const auto toConverter = splitline(records[2]);
    if ((header.size() < 3) || (fromConverter.size() < 2) || (toConverter.size() < 2)) {
        std::cerr << "Invalid VSC DC record\n";
        return;
    }

    const auto fromBusNumber = numeric_conversion<int>(fromConverter[0], 0);
    const auto toBusNumber = numeric_conversion<int>(toConverter[0], 0);
    auto* fromBus = rawDcLookupBus(busList, fromBusNumber);
    auto* toBus = rawDcLookupBus(busList, toBusNumber);
    if ((fromBus == nullptr) || (toBus == nullptr)) {
        std::cerr << "Invalid AC bus in VSC DC record\n";
        return;
    }

    const auto mdc = numeric_conversion<int>(header[1], 0);
    const auto fromType = numeric_conversion<int>(fromConverter[1], 0);
    const auto toType = numeric_conversion<int>(toConverter[1], 0);
    const auto fromMode = numeric_conversion<int>(fromConverter[2], 0);
    const auto toMode = numeric_conversion<int>(toConverter[2], 0);
    const auto name = std::string(trim(removeQuotes(header[0])));
    const auto resistance = numeric_conversion<double>(header[2], 0.0);
    const auto loss0 = (rawDcField(fromConverter, 5) + rawDcField(toConverter, 5) +
                        rawDcField(fromConverter, 7) + rawDcField(toConverter, 7)) *
        1e-3;
    const auto loss1 = (rawDcField(fromConverter, 6) + rawDcField(toConverter, 6)) * 1e-3;
    const auto rating =
        std::max(rawVscTransferLimit(fromConverter), rawVscTransferLimit(toConverter));
    const auto fromVoltageTarget =
        (rawDcField(fromConverter, 2) == 1.0) ? rawDcField(fromConverter, 4) : 1.0;
    const auto toVoltageTarget =
        (rawDcField(toConverter, 2) == 1.0) ? rawDcField(toConverter, 4) : 1.0;

    // PowerModels initializes VSC RAW dclines at zero active and reactive flow.
    // Retain its loss and limit data in the description while preserving that
    // solvable, scheduled-link behavior.  GridDyn's physical VSC models stay
    // available for native DC-network inputs.
    addRawDcCompatibilityLink(
        parentObject,
        fromBus,
        toBus,
        sequence,
        0.0,
        loss1,
        rating,
        (mdc != 0) && (fromType != 0) && (toType != 0),
        fromVoltageTarget,
        toVoltageTarget,
        (fromMode == 1) && rawDcUseVoltageControl(fromBus, fromBusNumber, voltageControlledBuses),
        (toMode == 1) && rawDcUseVoltageControl(toBus, toBusNumber, voltageControlledBuses),
        "PSS/E RAW VSC DC compatibility import; name='" + name + "', MDC=" + std::to_string(mdc) +
            ", RDC=" + std::to_string(resistance) + ", loss0=" + std::to_string(loss0) +
            ", loss1=" + std::to_string(loss1) +
            ", qminf=" + std::to_string(rawDcField(fromConverter, 12)) +
            ", qmaxf=" + std::to_string(rawDcField(fromConverter, 11)) +
            ", qmint=" + std::to_string(rawDcField(toConverter, 12)) +
            ", qmaxt=" + std::to_string(rawDcField(toConverter, 11)));
}

static int getPSSversion(const std::string& line)
{
    int ver = 29;
    auto slp = line.find_first_of('/');
    if (slp == std::string::npos) {
        return ver;
    }

    auto sloc = line.find("PSS", slp);
    if (sloc != std::string::npos) {
        auto dloc = line.find_first_of('-', sloc + 3);
        auto sploc = line.find_first_of(' ', dloc);
        ver = gmlc::utilities::numConv<int>(
            std::string_view{line}.substr(dloc + 1, sploc - dloc - 2));
    } else {
        sloc = line.find("VER", slp);
        if (sloc != std::string::npos) {
            ver = gmlc::utilities::numConv<int>(std::string_view{line}.substr(sloc + 3, 4));
            return ver;
        }
        sloc = line.find("version", slp);
        if (sloc != std::string::npos) {
            ver = gmlc::utilities::numConv<int>(std::string_view{line}.substr(sloc + 7, 4));
            return ver;
        }
    }
    return ver;
}

static constexpr std::array<std::pair<std::string_view, SectionType>, 20> sectionNames{{
    {"BEGIN FIXED SHUNT", SectionType::FIXED_SHUNT},
    {"BEGIN SWITCHED SHUNT DATA", SectionType::SWITCHED_SHUNT},
    {"BEGIN AREA INTERCHANGE DATA", SectionType::UNKNOWN},
    {"BEGIN TWO-TERMINAL DC LINE DATA", SectionType::TWO_TERMINAL_DC},
    {"BEGIN TWO-TERMINAL DC DATA", SectionType::TWO_TERMINAL_DC},
    {"BEGIN VOLTAGE SOURCE CONVERTER DATA", SectionType::VSC_DC},
    {"BEGIN VSC DC LINE DATA", SectionType::VSC_DC},
    {"BEGIN TRANSFORMER IMPEDANCE CORRECTION DATA", SectionType::UNKNOWN},
    {"BEGIN IMPEDANCE CORRECTION DATA", SectionType::UNKNOWN},
    {"BEGIN MULTI-TERMINAL DC LINE DATA", SectionType::UNKNOWN},
    {"BEGIN MULTI-SECTION LINE GROUP DATA", SectionType::UNKNOWN},
    {"BEGIN ZONE DATA", SectionType::UNKNOWN},
    {"BEGIN INTER-AREA TRANSFER DATA", SectionType::UNKNOWN},
    {"BEGIN OWNER DATA", SectionType::UNKNOWN},
    {"BEGIN FACTS CONTROL DEVICE DATA", SectionType::UNKNOWN},
    {"BEGIN LOAD DATA", SectionType::LOAD},
    {"BEGIN GENERATOR DATA", SectionType::GENERATOR},
    {"BEGIN BRANCH DATA", SectionType::BRANCH},
    {"BEGIN TRANSFORMER ADJUSTMENT DATA", SectionType::TXADJ},
    {"BEGIN TRANSFORMER DATA", SectionType::TX},
}};

static SectionType findSectionType(const std::string& line)
{
    const auto upperLine = convertToUpperCase(line);
    for (const auto& sectionName : sectionNames) {
        if (line.contains(sectionName.first) || upperLine.contains(sectionName.first)) {
            return sectionName.second;
        }
    }
    return SectionType::UNKNOWN;
}

static void rawReadBus(GridBus* bus, const std::string& line, BasicReaderInfo& opt)
{
    double baseVoltage = 0.0;
    double voltageMagnitude = 0.0;
    double voltageAngle = 0.0;
    int type;

    auto strvec = splitlineQuotes(line);
    // get the bus name
    auto temp = trim(strvec[0]);
    auto temp2 = trim(removeQuotes(strvec[1]));

    if (opt.prefix.empty()) {
        if (temp2.empty())  // 12 spaces is default value which would all get trimmed
        {
            temp2 = "BUS_" + temp;
        }
    } else {
        if (temp2.empty())  // 12 spaces is default value which would all get trimmed
        {
            temp2 = opt.prefix + "_BUS_" + temp;
        } else {
            temp2 = opt.prefix + '_' + temp2;
        }
    }
    bus->setName(temp2);

    // get the localBaseVoltage
    baseVoltage = gmlc::utilities::numConv<double>(strvec[2]);
    if (baseVoltage > 0.0) {
        bus->set("basevoltage", baseVoltage);
    }

    // get the bus type
    if (strvec[3].empty()) {
        type = 1;
    } else {
        type = gmlc::utilities::numConv<int>(strvec[3]);
    }

    switch (type) {
        case 1:
            temp = "PQ";
            break;
        case 2:
            temp = "PV";
            break;
        case 3:
            temp = "swing";
            break;
        case 4:
            bus->disable();
            temp = "PQ";
            break;
        default:
            bus->disable();
    }
    bus->set("type", temp);
    if (opt.version >= 31) {
        // skip the load flow area and loss zone for now
        // skip the owner information
        // get the voltage and angle specifications
        voltageMagnitude = numeric_conversion<double>(strvec[7], 0.0);
        voltageAngle = numeric_conversion<double>(strvec[8], 0.0);
        if (strvec.size() > 10) {
            baseVoltage = numeric_conversion<double>(strvec[9], 0.0);
            bus->set("vmax", baseVoltage);
            baseVoltage = numeric_conversion<double>(strvec[10], 0.0);
            bus->set("vmin", baseVoltage);
        }
    } else {
        // get the zone information
        const auto zone = numeric_conversion<double>(strvec[7], 0.0);
        bus->set("zone", zone);

        // RAW v29/v30 retain the fixed bus shunt fields, so VM and VA are
        // shifted two columns relative to v31 and later.  Loading ZONE as VM
        // produced initial voltages as high as 9 pu and made otherwise valid
        // v30 cases appear to fail in the nonlinear solver.
        voltageMagnitude = numeric_conversion<double>(strvec[8], 0.0);
        voltageAngle = numeric_conversion<double>(strvec[9], 0.0);
        // load the fixed shunt data
        const auto realAdmittance = numeric_conversion<double>(strvec[4], 0.0);
        const auto reactiveAdmittance = numeric_conversion<double>(strvec[5], 0.0);
        if ((realAdmittance != 0) || (reactiveAdmittance != 0)) {
            auto* fixedLoad = gLdfactory->makeTypeObject();
            bus->add(fixedLoad);
            if (realAdmittance != 0.0) {
                fixedLoad->set("yp", realAdmittance, MW);
            }
            if (reactiveAdmittance != 0.0) {
                fixedLoad->set("yq", -reactiveAdmittance, MVAR);
            }
        }
    }

    if (voltageAngle != 0) {
        bus->set("angle", voltageAngle, deg);
    }
    if (voltageMagnitude != 0) {
        // Preserve the solved RAW bus voltage as the local control target.
        // This mirrors the EPC reader and prevents a remote generator's VS
        // value from overwriting the terminal voltage while generators are
        // read sequentially.
        bus->set("vtarget", voltageMagnitude);
        bus->set("voltage", voltageMagnitude);
    }
}

static void rawReadLoad(GridLoad* loadObject, const std::string& line, BasicReaderInfo& /*bri*/)
{
    // version 32:
    //  0,  1,      2,    3,    4,    5,    6,      7,   8,  9, 10,   11
    // Bus, Id, Status, Area, Zone, PL(MW), QL (MW), IP, IQ, YP, YQ, OWNER

    auto strvec = splitline(line);

    // get the load index and name
    auto temp = trim(removeQuotes(strvec[1]));

    auto prefix = loadObject->getParent()->getName() + "_load_" + temp;
    loadObject->setName(prefix);

    // get the status
    auto status = gmlc::utilities::numConv<int>(strvec[2]);
    if (status == 0) {
        loadObject->disable();
    }
    // skip the area and zone information for now

    // get the constant power part of the load
    auto realPower = numeric_conversion<double>(strvec[5], 0.0);
    auto reactivePower = numeric_conversion<double>(strvec[6], 0.0);
    if (realPower != 0.0) {
        loadObject->set("p", realPower, MW);
    }
    if (reactivePower != 0.0) {
        loadObject->set("q", reactivePower, MVAR);
    }
    // get the constant current part of the load
    realPower = numeric_conversion<double>(strvec[7], 0.0);
    reactivePower = numeric_conversion<double>(strvec[8], 0.0);
    if (realPower != 0.0) {
        loadObject->set("ip", realPower, MW);
    }
    if (reactivePower != 0.0) {
        loadObject->set("iq", reactivePower, MVAR);
    }
    // get the impedance part of the load
    realPower = numeric_conversion<double>(strvec[9], 0.0);
    reactivePower = numeric_conversion<double>(strvec[10], 0.0);
    if (realPower != 0.0) {
        loadObject->set("yp", realPower, MW);
    }
    if (reactivePower != 0.0) {
        loadObject->set("yq", -reactivePower, MVAR);
    }
    // ignore the owner field
}

static void
    rawReadFixedShunt(GridLoad* loadObject, const std::string& line, BasicReaderInfo& /*bri*/)
{
    // 0,    1,      2,      3,      4
    // Bus, name, Status, g (MW), b (Mvar)
    auto strvec = splitline(line);

    // get the load index and name
    auto temp = trim(removeQuotes(strvec[1]));
    auto name = loadObject->getParent()->getName() + "_shunt_" + temp;
    loadObject->setName(name);

    // get the status
    auto status = gmlc::utilities::numConv<int>(strvec[2]);
    if (status == 0) {
        loadObject->disable();
    }
    // skip the area and zone information for now

    // get the constant power part of the load
    const auto conductance = numeric_conversion<double>(strvec[3], 0.0);
    const auto susceptance = numeric_conversion<double>(strvec[4], 0.0);
    if (conductance != 0.0) {
        loadObject->set("yp", conductance, MW);
    }
    if (susceptance != 0.0) {
        loadObject->set("yq", -susceptance, MVAR);
    }
}

static void rawReadGen(Generator* gen, const std::string& line, BasicReaderInfo& opt)
{
    // PSS/E v35 adds NREG after IREG.  All generator fields from MBASE
    // onward consequently move one column to the right.
    // v33: I,ID,PG,QG,QT,QB,VS,IREG,MBASE,ZR,ZX,RT,XT,GTAP,STAT,RMPCT,PT,PB
    // v35: I,ID,PG,QG,QT,QB,VS,IREG,NREG,MBASE,ZR,ZX,RT,XT,GTAP,STAT,RMPCT,PT,PB
    auto strvec = splitline(line);
    const size_t generatorFieldOffset = (opt.version >= 35) ? 1U : 0U;

    // get the load index and name
    auto temp = trim(removeQuotes(strvec[1]));

    auto prefix = gen->getParent()->getName() + "_Gen_" + temp;
    gen->setName(prefix);
    // get the status
    auto status = gmlc::utilities::numConv<int>(strvec[14 + generatorFieldOffset]);
    if (status == 0) {
        gen->disable();
    }

    auto machineBase = numeric_conversion<double>(strvec[8 + generatorFieldOffset], 0.0);
    gen->set("mbase", machineBase);

    // get the power generation
    auto realPower = numeric_conversion<double>(strvec[2], 0.0);
    auto reactivePower = numeric_conversion<double>(strvec[3], 0.0);
    if (realPower != 0.0) {
        gen->set("p", realPower, MW);
    }
    if (reactivePower != 0.0) {
        gen->set("q", reactivePower, MVAR);
    }
    // PT and PB are the generator's real-power capability limits.  EPC
    // imports the corresponding fields; leaving them at +/-infinity in RAW
    // gives slack and recovery adjustments an unbounded participation range.
    const auto pmax = numeric_conversion<double>(strvec[16 + generatorFieldOffset], 0.0);
    const auto pmin = numeric_conversion<double>(strvec[17 + generatorFieldOffset], 0.0);
    gen->set("pmax", pmax, MW);
    gen->set("pmin", pmin, MW);
    // get the Qmax and Qmin
    auto qmax = numeric_conversion<double>(strvec[4], 0.0);
    auto qmin = numeric_conversion<double>(strvec[5], 0.0);
    // Preserve a one-sided zero limit, but treat a zero/zero pair as omitted
    // limits.  IEEE and legacy PSS/E exports use that pair for an unconstrained
    // swing generator; applying it literally would force the unit to Q = 0.
    if ((qmax != 0.0) || (qmin != 0.0)) {
        gen->set("qmax", qmax, MVAR);
        gen->set("qmin", qmin, MVAR);
    }
    const auto rbus = numeric_conversion<int>(strvec[7], 0);
    if (rbus != 0) {
        // PSS/E IREG remote voltage regulation requires coordinated reactive
        // participation among every generator controlling the same bus.  The
        // present Generator remote-control path registers each unit as an
        // independent constraint.  The bus record already preserved the
        // supplied voltage as its local target, so leave VS/IREG unapplied for
        // this fixed-control MATPOWER/EPC-equivalent power-flow model.
    }

    auto resistance = numeric_conversion<double>(strvec[9 + generatorFieldOffset], 0.0);
    gen->set("rs", resistance);

    auto reactance = numeric_conversion<double>(strvec[10 + generatorFieldOffset], 0.0);
    gen->set("xs", reactance);

    if (!opt.checkFlag(IGNORE_STEP_UP_TRANSFORMER)) {
        resistance = numeric_conversion<double>(strvec[11 + generatorFieldOffset], 0.0);
        reactance = numeric_conversion<double>(strvec[12 + generatorFieldOffset], 0.0);
        if ((resistance != 0) || (reactance != 0))  // need to add a step up transformer
        {
            auto* oBus = dynamic_cast<GridBus*>(gen->getParent());
            if (oBus == nullptr) {
                throw(ObjectAddFailure(gen));
            }
            GridBus* nBus = gBusfactory->makeTypeObject();
            auto* lnk = new AcLine(resistance * opt.base / machineBase,
                                   reactance * opt.base /
                                       machineBase);  // we need to adjust to the simulation base as
                                                      // opposed to the machine base

            if (gen->getName().starts_with(oBus->getName())) {
                lnk->setName(gen->getName() + "_TX");
                nBus->setName(gen->getName() + "_TXBUS");
            } else {
                lnk->setName(oBus->getName() + '_' + gen->getName() + "_TX");
                nBus->setName(oBus->getName() + '_' + gen->getName() + "_TXBUS");
            }

            // Keep the generator alive while it is moved from the terminal bus to the
            // newly-created internal bus.  The original bus owns the generator at this
            // point, so removing it before adding the replacement owner can otherwise
            // delete the object and leave a dangling pointer.
            CoreOwningPtr<Generator> genTransferGuard(gen);
            oBus->remove(gen);
            nBus->add(gen);

            lnk->updateBus(oBus, 1);
            lnk->updateBus(nBus, 2);

            if (!gen->isEnabled()) {
                nBus->disable();
            }
            if (!oBus->isEnabled()) {
                nBus->disable();
            }
            oBus->getParent()->add(nBus);
            oBus->getParent()->add(lnk);
            // get the tap ratio
            const auto tapRatio =
                numeric_conversion<double>(strvec[13 + generatorFieldOffset], 0.0);
            lnk->set("tap", tapRatio);
            // match the voltage and angle of the other bus
            nBus->setVoltageAngle(oBus->getVoltage() * tapRatio, oBus->getAngle());
            gen->add(oBus);
            // get the power again for the generator
            realPower = numeric_conversion<double>(strvec[2], 0.0);
            reactivePower = numeric_conversion<double>(strvec[3], 0.0);
            // now adjust the newBus angle and Voltage to match the power flows
            lnk->fixPower(-realPower, -reactivePower, 1, 1, MVAR);
            if (!gen->isEnabled()) {
                nBus->disable();
            }
        }
    }
}

static auto generateBranchName(const stringVector& strvec,
                               const std::vector<GridBus*>& busList,
                               const std::string& prefix,
                               int cctIndex = -1)
{
    const int ind1 = gmlc::utilities::numConv<int>(strvec[0]);

    int ind2 = gmlc::utilities::numConv<int>(strvec[1]);

    // negative bus number indicates direction of measurement in PSS/E this is irrelevant in GridDyn
    // since it can do both directions
    if (ind2 < 0) {
        // int tmp=ind1;
        ind2 = abs(ind2);
        // ind2 = tmp;
    }

    if ((ind1 < 0) || (ind2 < 0) || std::cmp_greater_equal(ind1, busList.size()) ||
        std::cmp_greater_equal(ind2, busList.size())) {
        std::cerr << "invalid link buses\n";
        assert(false);
    }
    // create the bus name
    std::string name;
    if (prefix.empty()) {
        name = busList[ind1]->getName() + "_to_" + busList[ind2]->getName();
    } else if (prefix.back() == '_') {
        name = prefix + busList[ind1]->getName() + "_to_" + busList[ind2]->getName();
    } else {
        name = prefix + '_' + busList[ind1]->getName() + "_to_" + busList[ind2]->getName();
    }
    if (cctIndex >= 0) {
        auto temp = removeQuotes(strvec[cctIndex]);
        trimString(temp);
        if (temp != "1") {
            name.push_back('_');
            name += temp;
        }
    }

    return std::make_tuple(name, ind1, ind2);
}

static void rawReadBranch(CoreObject* parentObject,
                          const std::string& line,
                          std::vector<GridBus*>& busList,
                          BasicReaderInfo& opt)
{
    //
    // I,J,CKT,R,X,B,RATEA,RATEB,RATEC,GI,BI,GJ,BJ,ST,LEN,O1,F1,...,O4,F4
    //
    auto strvec = splitline(line);

    std::string name;
    int ind1;
    int ind2;
    std::tie(name, ind1, ind2) = generateBranchName(strvec, busList, opt.prefix, 2);

    AcLine* lnk = gLinkfactory->makeDirectObject(name);
    // set the base power to that used this model
    lnk->set("basepower", opt.base);

    lnk->updateBus(busList[ind1], 1);
    lnk->updateBus(busList[ind2], 2);

    // check for circuit identifier

    try {
        parentObject->add(lnk);
    }
    catch (const ObjectAddFailure&) {
        // must be a parallel branch
        const std::string sub = lnk->getName();
        char parallel = 'a';
        while (lnk->isRoot()) {
            lnk->setName(sub + '_' + parallel);
            parallel = parallel + 1;
            try {
                parentObject->add(lnk);
            }
            catch (const ObjectAddFailure& e) {
                if (parallel > 'z') {
                    throw e;
                }
            }
        }
    }

    auto resistance = numeric_conversion<double>(strvec[3], 0.0);
    auto reactance = numeric_conversion<double>(strvec[4], 0.0);
    // get line impedances and resistance
    lnk->set("r", resistance);
    lnk->set("x", reactance);
    // get line capacitance
    auto val = numeric_conversion<double>(strvec[5], 0.0);
    lnk->set("b", val);
    // RAW v35 inserts a branch name before RATE1 through RATE12.
    const size_t ratingStart = (opt.version >= 35) ? 7U : 6U;
    auto ratA = numeric_conversion<double>(strvec[ratingStart], 0.0);
    auto ratB = numeric_conversion<double>(strvec[ratingStart + 1], 0.0);
    auto ratC = numeric_conversion<double>(strvec[ratingStart + 2], 0.0);

    if (ratA != 0.0) {
        lnk->set("ratinga", ratA, MW);
    }
    if (ratB != 0.0 && ratB != ratA) {
        lnk->set("ratingb", ratB, MW);
    }
    if (ratC != 0.0 && ratC != ratA && ratC != ratB) {
        lnk->set("ratingc", ratC, MW);
    }
    int status;
    if (opt.version >= 35) {
        // v35 retains RATE1 through RATE12 before the terminal shunts and
        // STAT. The older layout has only RATEA/B/C, with STAT at column 13.
        status = gmlc::utilities::numConv<int>(strvec[23]);
        if (status == 0) {
            lnk->disable();
        }
    } else if (opt.version >= 29) {
        status = gmlc::utilities::numConv<int>(strvec[13]);
        if (status == 0) {
            lnk->disable();
        }
    } else {
        status = gmlc::utilities::numConv<int>(strvec[15]);
        if (status == 0) {
            lnk->disable();
        }
    }
    if (opt.version <= 26)  // transformers described in this section and in TX adj section
    {
        val = numeric_conversion<double>(strvec[9], 0.0);
        if (val != 0.0) {
            lnk->set("tap", val);
            val = numeric_conversion<double>(strvec[10], 0.0);
            if (val != 0) {
                lnk->set("tapAngle", val, deg);
            }
        }
    }

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    // TODO(phlpt): Get the other parameters; not critical for power flow.
}

static void rawReadTXadj(CoreObject* parentObject,
                         const std::string& line,
                         std::vector<GridBus*>& busList,
                         BasicReaderInfo& opt)
{
    // int status;

    auto strvec = splitline(line);

    std::string name;
    int ind1;
    int ind2;
    std::tie(name, ind1, ind2) =
        generateBranchName(strvec, busList, (opt.prefix.empty()) ? "tx_" : opt.prefix + "_tx_");

    auto* lnk = static_cast<AcLine*>(parentObject->find(name));

    if (lnk == nullptr) {
        parentObject->log(parentObject, PrintLevel::ERROR, "unable to locate link " + name);
        return;
    }

    auto* adjTX = new links::AdjustableTransformer();
    lnk->clone(adjTX);
    lnk->addOwningReference();
    parentObject->remove(lnk);
    adjTX->updateBus(lnk->getBus(1), 1);
    adjTX->updateBus(lnk->getBus(2), 2);
    lnk->updateBus(nullptr, 1);
    lnk->updateBus(nullptr, 2);
    removeReference(lnk);
    parentObject->add(adjTX);
    auto tapAngle = adjTX->getTapAngle();
    int code;
    if (tapAngle != 0) {
        adjTX->set("mode", "mw");
        adjTX->set("stepmode", "continuous");
        code = 3;
    } else {
        adjTX->set("mode", "voltage");
        code = 1;
    }
    // get the control bus
    if (code != 3) {
        auto cind = numeric_conversion<int>(strvec[3], 0);
        if (cind > 0) {
            if (cind == ind1) {
                adjTX->setControlBus(1);
            } else if (cind == ind2) {
                adjTX->setControlBus(2);
            } else {
                adjTX->setControlBus(
                    static_cast<GridBus*>(adjTX->getParent()->findByUserID("bus", cind)));
            }
        } else {
            if (-cind == ind1) {
                adjTX->setControlBus(1);
            } else if (-cind == ind2) {
                adjTX->setControlBus(2);
            } else {
                adjTX->setControlBus(
                    static_cast<GridBus*>(adjTX->getParent()->findByUserID("bus", -cind)));
                adjTX->set("direction", -1);
            }
        }
    }
    //
    auto maxTap = numeric_conversion<double>(strvec[4], 0.0);
    auto minTap = numeric_conversion<double>(strvec[5], 0.0);
    if ((maxTap - minTap > 1.0) && (code != 3)) {
        adjTX->set("mode", "mw");
        adjTX->set("stepmode", "continuous");
        code = 3;
    }
    if (code == 3) {
        // not sure why I need this but
        tapAngle = tapAngle * 180 / kPI;
        maxTap = (std::max)(tapAngle, maxTap);
        minTap = (std::min)(tapAngle, minTap);
        adjTX->set("maxtapangle", maxTap, deg);
        adjTX->set("mintapangle", minTap, deg);
    } else {
        if (maxTap < minTap) {
            std::swap(maxTap, minTap);
        }
        adjTX->set("maxtap", maxTap);
        adjTX->set("mintap", minTap);
    }
    maxTap = numeric_conversion<double>(strvec[6], 0.0);
    minTap = numeric_conversion<double>(strvec[7], 0.0);
    if ((maxTap - minTap > 1.0) && (code == 1)) {
        adjTX->set("mode", "mvar");
        code = 2;
    }
    if (code == 1) {
        if (maxTap - minTap > 0.00001) {
            adjTX->set("vmax", maxTap);
            adjTX->set("vmin", minTap);
        }
    } else if (code == 3) {
        if (maxTap - minTap > 0.00001) {
            adjTX->set("pmax", maxTap, MW);
            adjTX->set("pmin", minTap, MW);
        }
    } else {
        if (maxTap - minTap > 0.00001) {
            adjTX->set("qmax", maxTap, MVAR);
            adjTX->set("qmin", minTap, MVAR);
        }
    }
    if (code != 3)  // get the stepsize
    {
        auto val = numeric_conversion<double>(strvec[8], 0.0);
        if (val != 0) {
            // abs required since for some reason the file can have negative step sizes
            // I think just to do reverse indexing which I don't do.
            adjTX->set("step", std::abs(val));
        } else {
            adjTX->set("stepmode", "continuous");
        }
    }
    auto cind = numeric_conversion<int>(strvec[9], 0);
    if (cind != 0) {
        parentObject->log(parentObject,
                          PrintLevel::WARNING,
                          "transformer impedance tables not implemented yet ");
    }
    cind = numeric_conversion<int>(strvec[10], 0);
    {
        if (cind == 0) {
            adjTX->set("no_pflow_adjustments", 1);
        }
    }
    maxTap = numeric_conversion<double>(strvec[11], 0.0);
    minTap = numeric_conversion<double>(strvec[12], 0.0);
    if ((maxTap != 0) || (minTap != 0)) {
        parentObject->log(parentObject,
                          PrintLevel::WARNING,
                          "load drop compensation not implemented yet ");
    }
}

static int rawReadTxV33(CoreObject* parentObject,
                        stringVec& txlines,
                        std::vector<GridBus*>& busList,
                        BasicReaderInfo& opt,
                        const ImpedanceCorrectionTables& correctionTables)
{
    /* version 33
    # """
    # I,J,K,CKT,CW,CZ,CM,MAG1,MAG2,NMETR,'NAME',STAT,O1,F1,...,O4,F4
    # R1-2,X1-2,SBASE1-2
    # WINDV1,NOMV1,ANG1,RATA1,RATB1,RATC1,COD1,CONT1,RMA1,RMI1,VMA1,VMI1,NTP1,TAB1,CR1,CX1
    # WINDV2,NOMV2
    #
    # """

    110, 70401,     0,'1 ',1,2,1, 0.00000E+00, 0.00000E+00,2,'            ',1,   1,1.0000, 0,1.0000,
    0,1.0000,   0,1.0000,'            ' 0.00000E+0, 8.00000E-2,   100.00 1.00000,   0.000,   0.000,
    0.00,     0.00,     0.00, 0,      0, 1.10000, 0.90000, 1.10000, 0.90000,  33, 0, 0.00000,
    0.00000,  0.000 1.00000,   0.000

    */
    // GridBus *bus3;
    AcLine* lnk = nullptr;

    stringVec strvec5;
    auto strvec = splitline(txlines[0]);

    auto strvec2 = splitline(txlines[1]);
    auto strvec3 = splitline(txlines[2]);
    auto strvec4 = splitline(txlines[3]);
    // v35 expands RATA/B/C to RATE1 through RATE12, then adds NOD after
    // CONT. Fields through RATC are unchanged; control fields move by 9
    // and the tap-limit/table fields after NOD move by 10.
    const size_t windingControlOffset = (opt.version >= 35) ? 9U : 0U;
    const size_t windingTailOffset = (opt.version >= 35) ? 10U : 0U;

    std::string name;
    int ind1;
    int ind2;
    std::tie(name, ind1, ind2) =
        generateBranchName(strvec, busList, (opt.prefix.empty()) ? "tx_" : opt.prefix + "_tx_", 3);

    const int ind3 = gmlc::utilities::numConv<int>(strvec[2]);
    int tline = 4;
    if (ind3 != 0) {
        tline = 5;
        strvec5 = splitline(txlines[4]);
        rawReadThreeWindingTransformer(parentObject,
                                       strvec,
                                       strvec2,
                                       {strvec3, strvec4, strvec5},
                                       busList,
                                       opt,
                                       correctionTables);
        return tline;
    }

    auto* bus1 = busList[ind1];
    auto* bus2 = busList[ind2];
    const int code = gmlc::utilities::numConv<int>(strvec3[6 + windingControlOffset]);
    const int controlCode = std::abs(code);
    // In RAW v35, a negative winding-control code specifies a fixed/manual
    // control. Do not construct an AdjustableTransformer for those records:
    // its adjustment state is inappropriate for a fixed phase shifter.
    const bool fixedV35Control = (opt.version >= 35 && code < 0);
    if (fixedV35Control) {
        lnk = gLinkfactory->makeDirectObject(name);
    } else {
        switch (controlCode) {
            case 0:
            default:
                lnk = gLinkfactory->makeDirectObject(name);
                break;
            case 1:
                if (opt.prefix.empty()) {
                    name.insert(0, "vadj");
                }
                lnk = new links::AdjustableTransformer(name);
                lnk->set("mode", "voltage");
                break;
            case 2:
                if (opt.prefix.empty()) {
                    name.insert(0, "qadj");
                }
                lnk = new links::AdjustableTransformer(name);
                lnk->set("mode", "mvar");
                break;
            case 3:
                if (opt.prefix.empty()) {
                    name.insert(0, "padj");
                }
                lnk = new links::AdjustableTransformer(name);
                lnk->set("mode", "mw");
                break;
        }
    }
    auto* adjTX = dynamic_cast<links::AdjustableTransformer*>(lnk);
    if (code < 0 && !fixedV35Control && adjTX != nullptr)  // account for older negative code values
    {
        adjTX->set("mode", "manual");
    }
    lnk->set("basepower", opt.base);
    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);

    try {
        parentObject->add(lnk);
    }
    catch (const ObjectAddFailure&) {
        // must be a parallel branch
        const auto& sub = lnk->getName();
        char suffix = 'a';
        while (lnk->isRoot()) {
            lnk->setName(sub + '_' + suffix);
            suffix = suffix + 1;
            try {
                parentObject->add(lnk);
            }
            catch (const ObjectAddFailure& e) {
                if (suffix > 'z') {
                    throw e;
                }
            }
        }
    }

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    const auto impedanceType = numeric_conversion<int>(strvec[5], 1);

    auto resistance = numeric_conversion<double>(strvec2[0], 0.0);
    auto reactance = numeric_conversion<double>(strvec2[1], 0.0);
    const auto impedanceCorrection =
        correctionFactor(correctionTables,
                         numeric_conversion<int>(strvec3[13 + windingTailOffset], 0),
                         numeric_conversion<double>(strvec3[2], 0.0));
    resistance *= impedanceCorrection;
    reactance *= impedanceCorrection;

    auto vn1 = numeric_conversion<double>(strvec3[1], 0.0);
    auto vn2 = numeric_conversion<double>(strvec4[1], 0.0);

    auto bv1 = bus1->get("basevoltage");
    auto bv2 = bus2->get("basevoltage");

    auto base = numeric_conversion<double>(strvec2[2], 0.0);

    if (impedanceType == 1) {
        lnk->set("r", resistance);
        lnk->set("x", reactance);
    } else if (impedanceType == 2) {
        if (vn2 != 0.0) {
            const auto secondaryResistance =
                resistance * opt.base / base * (vn2 / bv2) * (vn2 / bv2);
            const auto secondaryReactance = reactance * opt.base / base * (vn2 / bv2) * (vn2 / bv2);
            lnk->set("r", secondaryResistance);
            lnk->set("x", secondaryReactance);
        }

        // lnk->set("r", R*base/opt.base*(vn2/bv2)*(vn2/bv2));
        // lnk->set("x", X*base/opt.base*(vn2/bv2)*(vn2/bv2));
    } else {
    }
    // get line capacitance

    auto status = gmlc::utilities::numConv<int>(strvec[11]);
    if (status == 0) {
        lnk->disable();
    } else if (status > 1) {
        // TODO(phlpt): Handle the other conditions for three-way transformers.
    }

    // TODO(phlpt): Get the other parameters; not critical for power flow.
    auto tap = numeric_conversion<double>(strvec3[0], 0.0);

    const int tapcode = gmlc::utilities::numConv<int>(strvec[4]);
    if (tapcode == 2) {
        auto wv2 = numeric_conversion<double>(strvec4[0], 0.0);
        tap = (tap / bv1 / (wv2 / bv2));
    } else if (tapcode == 3) {
        if (vn1 == 0.0) {
            vn1 = bv1;
        }
        if (vn2 == 0.0) {
            vn2 = bv2;
        }
        tap = tap * (vn1 / bv1) / (vn2 / bv2);
    }

    if (tap != 0) {
        lnk->set("tap", tap);
    }

    auto angle = numeric_conversion<double>(strvec3[2], 0.0);
    if (angle != 0) {
        lnk->set("tapangle", angle, deg);
    }

    // get the ratings
    auto ratA = numeric_conversion<double>(strvec3[3], 0.0);
    auto ratB = numeric_conversion<double>(strvec3[4], 0.0);
    auto ratC = numeric_conversion<double>(strvec3[5], 0.0);

    if (ratA != 0.0) {
        lnk->set("ratinga", ratA, MW);
    }
    if (ratB != 0.0 && ratB != ratA) {
        lnk->set("ratingb", ratB, MW);
    }
    if (ratC != 0.0 && ratC != ratB && ratC != ratA) {
        lnk->set("ratingc", ratC, MW);
    }
    // now get the stuff for the adjustable transformers
    // SGS set this for adjustable transformers....is this correct?
    if (controlCode > 0 && !fixedV35Control && adjTX != nullptr) {
        auto cbus = numeric_conversion<int>(strvec3[7 + windingControlOffset], 0);
        if (cbus != 0) {
            if (abs(cbus) == ind1) {
                adjTX->setControlBus(1);
            } else if (abs(cbus) == ind2) {
                adjTX->setControlBus(2);
            }

            else {
                adjTX->setControlBus(busList[abs(cbus)]);
            }

            if (tapcode == 2) {
                if (abs(cbus) == ind1) {
                    auto tap1 = (bus1->getVoltage() / bus2->getVoltage());
                    [[maybe_unused]] auto tap2 = (bus1->getVoltage() / (vn1 / bv1));
                    auto tap3 = (bus1->getVoltage() / (vn2 / bv2));
                    [[maybe_unused]] auto tap4 = ((vn1 / bv1) / bus2->getVoltage());

                    [[maybe_unused]] auto tap5 = (bus2->getVoltage() / bus1->getVoltage());
                    [[maybe_unused]] auto tap6 = (bus2->getVoltage() / (vn2 / bv2));
                    [[maybe_unused]] auto tap7 = (bus2->getVoltage() / (vn1 / bv1));
                    [[maybe_unused]] auto tap8 = ((vn2 / bv2) / bus1->getVoltage());

                    auto tap9 = (tap1 + tap3) / 2;
                    if (tap9 != 0) {
                        lnk->set("tap", tap9);
                    }
                }
            }
        }

        resistance = numeric_conversion<double>(strvec3[8 + windingTailOffset], 0.0);
        reactance = numeric_conversion<double>(strvec3[9 + windingTailOffset], 0.0);

        if (controlCode == 3) {
            adjTX->set("maxtapangle", resistance, deg);
            adjTX->set("mintapangle", reactance, deg);
        } else {
            if (reactance < 1.0) {
                adjTX->set("maxtap", resistance);
                adjTX->set("mintap", reactance);
            } else {
                adjTX->set("maxtap", resistance / vn1);
                adjTX->set("mintap", reactance / vn1);
            }
        }

        resistance = numeric_conversion<double>(strvec3[10 + windingTailOffset], 0.0);
        reactance = numeric_conversion<double>(strvec3[11 + windingTailOffset], 0.0);

        if (controlCode == 3) {
            adjTX->set("pmax", resistance, MW);
            adjTX->set("pmin", reactance, MW);
        } else if (controlCode == 2) {
            adjTX->set("qmax", resistance, MVAR);
            adjTX->set("qmin", reactance, MVAR);
        } else {
            adjTX->set("vmax", resistance);
            adjTX->set("vmin", reactance);
        }
        resistance = numeric_conversion<double>(strvec3[12 + windingTailOffset], 0.0);
        if (controlCode != 3) {
            adjTX->set("nsteps", resistance);
        }
    } else if (controlCode > 0 && !fixedV35Control) {
        parentObject->log(parentObject,
                          PrintLevel::WARNING,
                          "unsupported transformer control code " + std::to_string(controlCode) +
                              "; importing transformer as fixed");
    }
    return tline;
}

static int rawReadTX(CoreObject* parentObject,
                     stringVec& txlines,
                     std::vector<GridBus*>& busList,
                     BasicReaderInfo& opt,
                     const ImpedanceCorrectionTables& correctionTables)
{
    // GridBus *bus3;
    AcLine* lnk = nullptr;

    stringVec strvec5;
    auto strvec = splitline(txlines[0]);

    auto strvec2 = splitline(txlines[1]);
    auto strvec3 = splitline(txlines[2]);
    auto strvec4 = splitline(txlines[3]);

    std::string name;
    int ind1;
    int ind2;
    std::tie(name, ind1, ind2) =
        generateBranchName(strvec, busList, (opt.prefix.empty()) ? "tx_" : opt.prefix + "_tx_", 3);

    const int ind3 = gmlc::utilities::numConv<int>(strvec[2]);
    int tline = 4;
    if (ind3 != 0) {
        tline = 5;
        strvec5 = splitline(txlines[4]);
        rawReadThreeWindingTransformer(parentObject,
                                       strvec,
                                       strvec2,
                                       {strvec3, strvec4, strvec5},
                                       busList,
                                       opt,
                                       correctionTables);
        return tline;
    }

    auto* bus1 = busList[ind1];
    auto* bus2 = busList[ind2];
    const int code = gmlc::utilities::numConv<int>(strvec3[6]);
    const int controlCode = std::abs(code);
    switch (controlCode) {
        case 0:
        default:
            lnk = gLinkfactory->makeDirectObject(name);
            break;
        case 1:
            lnk = new links::AdjustableTransformer(name);
            lnk->set("mode", "voltage");
            break;
        case 2:
            lnk = new links::AdjustableTransformer(name);
            lnk->set("mode", "mvar");
            break;
        case 3:
            lnk = new links::AdjustableTransformer(name);
            lnk->set("mode", "mw");
            break;
    }
    auto* adjTX = dynamic_cast<links::AdjustableTransformer*>(lnk);
    if (code < 0 && adjTX != nullptr)  // account for negative code values
    {
        adjTX->set("mode", "manual");
    }
    lnk->set("basepower", opt.base);
    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);

    try {
        parentObject->add(lnk);
    }
    catch (const ObjectAddFailure&) {
        // must be a parallel branch
        const auto& sub = lnk->getName();
        char suffix = 'a';
        while (lnk->isRoot()) {
            lnk->setName(sub + '_' + suffix);
            suffix = suffix + 1;
            try {
                parentObject->add(lnk);
            }
            catch (const ObjectAddFailure& e) {
                if (suffix > 'z') {
                    throw e;
                }
            }
        }
    }

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    auto resistance = numeric_conversion<double>(strvec2[0], 0.0);
    auto reactance = numeric_conversion<double>(strvec2[1], 0.0);
    const auto impedanceCorrection = correctionFactor(correctionTables,
                                                      numeric_conversion<int>(strvec3[13], 0),
                                                      numeric_conversion<double>(strvec3[2], 0.0));
    resistance *= impedanceCorrection;
    reactance *= impedanceCorrection;

    const auto windingCode = numeric_conversion<int>(strvec[4], 1);
    const auto impedanceCode = numeric_conversion<int>(strvec[5], 1);
    const auto windingBase = numeric_conversion<double>(strvec2[2], opt.base);
    const auto nominalVoltage1 = numeric_conversion<double>(strvec3[1], 0.0);
    const auto nominalVoltage2 = numeric_conversion<double>(strvec4[1], 0.0);
    const auto busBaseVoltage1 = bus1->get("basevoltage");
    const auto busBaseVoltage2 = bus2->get("basevoltage");

    if ((impedanceCode == 2) || (impedanceCode == 3)) {
        if ((impedanceCode == 3) && (windingBase > 0.0)) {
            resistance /= windingBase * 1.0e6;
            reactance = std::sqrt(std::max(reactance * reactance - resistance * resistance, 0.0));
        }
        if (windingBase > 0.0) {
            const auto voltageScale = (nominalVoltage1 > 0.0 && busBaseVoltage1 > 0.0) ?
                nominalVoltage1 / busBaseVoltage1 :
                1.0;
            const auto impedanceScale = opt.base / windingBase * voltageScale * voltageScale;
            resistance *= impedanceScale;
            reactance *= impedanceScale;
        }
    }

    auto secondaryVoltageScale = 1.0;
    auto windingVoltage2 = numeric_conversion<double>(strvec4[0], 1.0);
    if (windingVoltage2 == 0.0) {
        windingVoltage2 = 1.0;
    }
    if (windingCode == 1) {
        secondaryVoltageScale = windingVoltage2;
    } else if ((windingCode == 2) && (busBaseVoltage2 > 0.0)) {
        secondaryVoltageScale = windingVoltage2 / busBaseVoltage2;
    } else if (windingCode == 3) {
        secondaryVoltageScale = (nominalVoltage2 > 0.0 && busBaseVoltage2 > 0.0) ?
            windingVoltage2 * nominalVoltage2 / busBaseVoltage2 :
            windingVoltage2;
    }
    resistance *= secondaryVoltageScale * secondaryVoltageScale;
    reactance *= secondaryVoltageScale * secondaryVoltageScale;

    lnk->set("r", resistance);
    lnk->set("x", reactance);
    // get line capacitance

    auto status = gmlc::utilities::numConv<int>(strvec[11]);
    if (status == 0) {
        lnk->disable();
    } else if (status > 1) {
        // TODO(phlpt): Handle the other conditions for three-way transformers.
    }

    // TODO(phlpt): Get the other parameters; not critical for power flow.
    auto tap = numeric_conversion<double>(strvec3[0], 0.0);
    tap /= windingVoltage2;
    if ((windingCode == 2) && (busBaseVoltage1 > 0.0) && (busBaseVoltage2 > 0.0)) {
        tap *= busBaseVoltage2 / busBaseVoltage1;
    } else if ((windingCode == 3) && (nominalVoltage1 > 0.0) && (nominalVoltage2 > 0.0) &&
               (busBaseVoltage1 > 0.0) && (busBaseVoltage2 > 0.0)) {
        tap *= nominalVoltage1 / nominalVoltage2 * busBaseVoltage2 / busBaseVoltage1;
    }
    if (tap != 0.0) {
        lnk->set("tap", tap);
    }
    auto val = numeric_conversion<double>(strvec3[2], 0.0);
    if (val != 0) {
        lnk->set("tapangle", val, deg);
    }
    // now get the stuff for the adjustable transformers
    // SGS set this for adjustable transformers....is this correct?
    if (controlCode > 0 && adjTX != nullptr) {
        auto cbus = numeric_conversion<int>(strvec3[7], 0);
        if (cbus != 0) {
            if (std::abs(cbus) == ind1) {
                adjTX->setControlBus(1);
            } else if (std::abs(cbus) == ind2) {
                adjTX->setControlBus(2);
            } else if (std::cmp_less(std::abs(cbus), busList.size())) {
                adjTX->setControlBus(busList[std::abs(cbus)]);
            }
        }

        resistance = numeric_conversion<double>(strvec3[8], 0.0);
        reactance = numeric_conversion<double>(strvec3[9], 0.0);

        if (controlCode == 3) {
            adjTX->set("maxtapangle", resistance, deg);
            adjTX->set("mintapangle", reactance, deg);
        } else {
            if ((windingCode == 2) && (busBaseVoltage1 > 0.0)) {
                resistance /= busBaseVoltage1;
                reactance /= busBaseVoltage1;
            } else if ((windingCode == 3) && (nominalVoltage1 > 0.0) && (busBaseVoltage1 > 0.0)) {
                resistance *= nominalVoltage1 / busBaseVoltage1;
                reactance *= nominalVoltage1 / busBaseVoltage1;
            }
            adjTX->set("maxtap", resistance);
            adjTX->set("mintap", reactance);
        }
        if (opt.version >= 33) {
            resistance = numeric_conversion<double>(strvec3[12], 0.0);
            reactance = numeric_conversion<double>(strvec3[13], 0.0);
        } else {
            resistance = numeric_conversion<double>(strvec3[10], 0.0);
            reactance = numeric_conversion<double>(strvec3[11], 0.0);
        }
        if (controlCode == 3) {
            adjTX->set("pmax", resistance, MW);
            adjTX->set("pmin", reactance, MW);
        } else if (controlCode == 2) {
            adjTX->set("qmax", resistance, MVAR);
            adjTX->set("qmin", reactance, MVAR);
        } else {
            adjTX->set("vmax", resistance);
            adjTX->set("vmin", reactance);
        }
        resistance = numeric_conversion<double>(strvec3[12], 0.0);
        if (controlCode != 3) {
            adjTX->set("nsteps", resistance);
        }
    } else if (controlCode > 0) {
        parentObject->log(parentObject,
                          PrintLevel::WARNING,
                          "unsupported transformer control code " + std::to_string(controlCode) +
                              "; importing transformer as fixed");
    }
    return tline;
}

// static int rawReadDCLine(CoreObject* /*parentObject*/,
//                          stringVec& /*txlines*/,
//                          std::vector<GridBus*>& /*busList*/,
//                          BasicReaderInfo& /*bri*/)
// {
//     return 0;
// }

static void rawReadSwitchedShunt(CoreObject* parentObject,
                                 const std::string& line,
                                 std::vector<GridBus*>& busList,
                                 BasicReaderInfo& opt)
{
    auto strvec = splitline(line);

    auto index = gmlc::utilities::numConv<std::size_t>(strvec[0]);
    GridBus* rbus = nullptr;
    loads::Svd* loadObject = nullptr;
    double temp;
    if (std::cmp_greater_equal(index, busList.size())) {
        throw std::runtime_error("Invalid bus number for load " + std::to_string(index));
    }
    if (busList[index] == nullptr) {
        throw std::runtime_error("Invalid bus number for load " + std::to_string(index));
    }

    loadObject = new loads::Svd();
    busList[index]->add(loadObject);

    auto mode = numeric_conversion<int>(strvec[1], 0);
    int shift = 0;
    int blockStart = 7;
    bool inService = true;
    // TODO(phlpt): Verify this logic; it may not be totally correct right now.
    // VERSION 32 has some ambiguity in the interpretation
    if (opt.version >= 32) {
        shift = 2;
        // In PSS/E v32+, ADJM and STAT follow MODSW. An out-of-service
        // switched shunt must retain its BINIT value but must not participate
        // in voltage/reactive-power control.
        inService = (numeric_conversion<int>(strvec[3], 1) != 0);
    }
    // This v35 export retains a quoted switched-shunt identifier and one
    // additional control field. Detect that layout rather than shifting all
    // v35 files, since the PSS/E field-definition card does not list it.
    if ((opt.version >= 35) && (strvec.size() > 11) && trim(strvec[1]).starts_with("'")) {
        shift = 3;
        blockStart = 11;
        mode = numeric_conversion<int>(strvec[2], 0);
        inService = (numeric_conversion<int>(strvec[4], 1) != 0);
    }
    auto high = numeric_conversion<double>(strvec[2 + shift], 0.0);
    auto low = numeric_conversion<double>(strvec[3 + shift], 0.0);
    // get the controlled bus
    auto cbus = numeric_conversion<int>(strvec[4 + shift], -1);

    if (cbus < 0) {
        trimString(strvec[4 + shift]);
        if ((strvec[4 + shift] == "I") || strvec[4 + shift].empty()) {
            cbus = index;
        } else {
            rbus = static_cast<GridBus*>(parentObject->find(strvec[4 + shift]));
            if (rbus != nullptr) {
                cbus = rbus->getUserID();
            }
        }
    } else if (cbus == 0) {
        cbus = index;
    } else {
        rbus = busList[cbus];
    }

    switch (mode) {
        case 0:
            loadObject->set("mode", "manual");
            break;
        case 1:
            loadObject->set("mode", "stepped");
            loadObject->set("vmax", high);
            loadObject->set("vmin", low);
            if (std::cmp_not_equal(cbus, index)) {
                loadObject->setControlBus(rbus);
            }

            temp = numeric_conversion<double>(strvec[5 + shift], 0.0);
            if (temp > 0) {
                loadObject->set("participation", temp / 100.0);
            }
            break;
        case 2:
            loadObject->set("mode", "cont");
            loadObject->set("vmax", high);
            loadObject->set("vmin", low);
            if (std::cmp_not_equal(cbus, index)) {
                loadObject->setControlBus(rbus);
            }
            temp = numeric_conversion<double>(strvec[5 + shift], 0.0);
            if (temp > 0) {
                loadObject->set("participation", temp / 100.0);
            }
            break;
        case 3:
        case 4:
        case 5:
        case 6:
            loadObject->set("mode", "stepped");
            loadObject->set("control", "reactive");
            loadObject->set("qmax", high);
            loadObject->set("qmin", low);
            if (std::cmp_not_equal(cbus, index)) {
                loadObject->setControlBus(rbus);
            }
            break;
        default:
            loadObject->set("mode", "manual");
            break;
    }
    // load the switched shunt blocks
    if (opt.version <= 27) {
        blockStart = 5;
    } else if (opt.version >= 32 && blockStart == 7) {
        blockStart = 9;
    }
    const size_t ksize = strvec.size() - 1;
    for (size_t kk = blockStart + 1; kk < ksize; kk += 2) {
        auto cnt = numeric_conversion<int>(strvec[kk], 0);
        auto block = numeric_conversion<double>(strvec[kk + 1], 0.0);
        if ((cnt > 0) && (block != 0.0)) {
            loadObject->addBlock(cnt, -block, MVAR);
        } else {
            break;
        }
    }
    // set the initial value
    auto initVal = numeric_conversion<double>(strvec[blockStart], 0.0);

    // BINIT is the present shunt susceptance, not a request to change the
    // discrete/continuous SVD setting.  Set it directly so voltage-controlled
    // records retain their PowerWorld/PSS/E initial reactive injection.
    loadObject->ZipLoad::set("yq", -initVal, MVAR);
    if (!inService) {
        loadObject->disable();
    }
}

}  // namespace griddyn
