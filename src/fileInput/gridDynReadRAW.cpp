/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ObjectFactoryTemplates.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Load.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
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
    return lower->second + fraction * (upper->second - lower->second);
}

static ImpedanceCorrectionTables readImpedanceCorrectionTables(const std::string& fileName)
{
    ImpedanceCorrectionTables tables;
    std::ifstream file(fileName, std::ios::in);
    std::string line;
    bool inCorrectionSection = false;
    while (std::getline(file, line)) {
        if (!inCorrectionSection) {
            inCorrectionSection = line.find("BEGIN IMPEDANCE CORRECTION DATA") != std::string::npos;
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
        if (fields.size() < 3) {
            continue;
        }
        const auto tableId = numeric_conversion<int>(fields[0], 0);
        ImpedanceCorrectionTable points;
        for (size_t ii = 1; ii + 1 < fields.size(); ii += 2) {
            points.emplace_back(numeric_conversion<double>(fields[ii], 0.0),
                                numeric_conversion<double>(fields[ii + 1], 1.0));
        }
        if ((tableId != 0) && !points.empty()) {
            tables.emplace(tableId, std::move(points));
        }
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
    const auto impedanceCode = numeric_conversion<int>(header[5], 1);
    for (size_t ii = 0; ii < 3; ++ii) {
        if (impedanceCode == 2) {
            if (windingBase[ii] > 0.0) {
                resistance[ii] *= opt.base / windingBase[ii];
                reactance[ii] *= opt.base / windingBase[ii];
            }
        } else if (impedanceCode == 3) {
            // CZ=3 uses load loss in W and impedance magnitude on winding base.
            if (windingBase[ii] > 0.0) {
                resistance[ii] /= windingBase[ii] * 1.0e6;
                reactance[ii] = std::sqrt(
                    std::max(reactance[ii] * reactance[ii] - resistance[ii] * resistance[ii], 0.0));
                resistance[ii] *= opt.base / windingBase[ii];
                reactance[ii] *= opt.base / windingBase[ii];
            }
        }
    }
    std::array<double, 3> starResistance{(resistance[0] + resistance[2] - resistance[1]) / 2.0,
                                         (resistance[0] + resistance[1] - resistance[2]) / 2.0,
                                         (resistance[1] + resistance[2] - resistance[0]) / 2.0};
    std::array<double, 3> starReactance{(reactance[0] + reactance[2] - reactance[1]) / 2.0,
                                        (reactance[0] + reactance[1] - reactance[2]) / 2.0,
                                        (reactance[1] + reactance[2] - reactance[0]) / 2.0};
    const auto impedanceCorrection =
        correctionFactor(correctionTables,
                         numeric_conversion<int>(windings[0][13], 0),
                         numeric_conversion<double>(windings[0][2], 0.0));
    starResistance[0] *= impedanceCorrection;
    starReactance[0] *= impedanceCorrection;
    const std::array<GridBus*, 3> exterior{busList[busNumber1],
                                           busList[busNumber2],
                                           busList[busNumber3]};

    const auto tapCode = numeric_conversion<int>(header[4], 1);
    for (size_t ii = 0; ii < 3; ++ii) {
        // CW=1 is the normal PSS/E per-unit WINDV form.  For the other
        // variants preserve the supplied normalized value; their uncommon
        // voltage-base conversion requires a dedicated validation fixture.
        auto tap = numeric_conversion<double>(windings[ii][0], 1.0);
        if (tap == 0.0) {
            tap = 1.0;
        }
        if (tapCode != 1) {
            std::cerr << "three-winding transformer uses CW=" << tapCode
                      << "; treating WINDV as a normalized fixed tap\n";
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
        if (numeric_conversion<int>(header[11], 1) == 0) {
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
        TXADJ
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
    if (std::getline(file, nextLine)) {
        trimString(nextLine);
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

void loadRaw(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    const auto impedanceCorrectionTables = readImpedanceCorrectionTables(fileName);
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string temp1;  // temporary storage for substrings
    std::vector<GridBus*> busList;
    BasicReaderInfo readerOptionsCopy(readerOptions);
    auto& opt = readerOptionsCopy;
    GridLoad* loadObject;
    Generator* gen;
    GridBus* bus;
    index_t index;
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
    // get the bus data section
    // bus data doesn't have a header but it is always first
    bool moreData = true;
    while (moreData) {
        if (checkNextLine(file, line)) {
            // get the index
            pos = line.find_first_of(',');
            temp1 = line.substr(0, pos);
            index = numeric_conversion<index_t>(temp1, 0);

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
                    auto prevName = busList[index]->getName();
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
        } else {
            moreData = false;
        }
    }

    stringVec txlines;
    txlines.resize(5);
    int tline = 5;

    bool moreSections = true;

    while (moreSections) {
        const SectionType currSection = findSectionType(line);
        moreData = true;
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
                    if (opt.version >= 33) {
                        tline = rawReadTxV33(
                            parentObject, txlines, busList, opt, impedanceCorrectionTables);
                    } else {
                        tline = rawReadTX(
                            parentObject, txlines, busList, opt, impedanceCorrectionTables);
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

static constexpr std::array<std::pair<std::string_view, SectionType>, 17> sectionNames{{
    {"BEGIN FIXED SHUNT", SectionType::FIXED_SHUNT},
    {"BEGIN SWITCHED SHUNT DATA", SectionType::SWITCHED_SHUNT},
    {"BEGIN AREA INTERCHANGE DATA", SectionType::UNKNOWN},
    {"BEGIN TWO-TERMINAL DC LINE DATA", SectionType::UNKNOWN},
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
        voltageMagnitude = numeric_conversion<double>(strvec[7], 0.0);
        bus->set("zone", voltageMagnitude);

        voltageAngle = numeric_conversion<double>(strvec[8], 0.0);
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
    // 0, 1, 2, 3, 4, 5, 6, 7,    8,   9,10,11, 12, 13, 14,   15, 16,17,18,19
    //  I,ID,PG,QG,QT,QB,VS,IREG,MBASE,ZR,ZX,RT,XT,GTAP,STAT,RMPCT,PT,PB,O1,F1
    auto strvec = splitline(line);

    // get the load index and name
    auto temp = trim(removeQuotes(strvec[1]));

    auto prefix = gen->getParent()->getName() + "_Gen_" + temp;
    gen->setName(prefix);
    // get the status
    auto status = gmlc::utilities::numConv<int>(strvec[14]);
    if (status == 0) {
        gen->disable();
    }

    auto machineBase = numeric_conversion<double>(strvec[8], 0.0);
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
    const auto pmax = numeric_conversion<double>(strvec[16], 0.0);
    const auto pmin = numeric_conversion<double>(strvec[17], 0.0);
    gen->set("pmax", pmax, MW);
    gen->set("pmin", pmin, MW);
    // get the Qmax and Qmin
    auto qmax = numeric_conversion<double>(strvec[4], 0.0);
    auto qmin = numeric_conversion<double>(strvec[5], 0.0);
    // A zero reactive limit is meaningful: it forces a generator operating at
    // that limit to behave as a PQ injection.  Dropping it leaves the bus as
    // PV and can produce a materially different PSS/E power-flow solution.
    gen->set("qmax", qmax, MVAR);
    gen->set("qmin", qmin, MVAR);
    const auto rbus = numeric_conversion<int>(strvec[7], 0);
    if (rbus != 0) {
        // PSS/E IREG remote voltage regulation requires coordinated reactive
        // participation among every generator controlling the same bus.  The
        // present Generator remote-control path registers each unit as an
        // independent constraint.  The bus record already preserved the
        // supplied voltage as its local target, so leave VS/IREG unapplied for
        // this fixed-control MATPOWER/EPC-equivalent power-flow model.
    }

    auto resistance = numeric_conversion<double>(strvec[9], 0.0);
    gen->set("rs", resistance);

    auto reactance = numeric_conversion<double>(strvec[10], 0.0);
    gen->set("xs", reactance);

    if (!opt.checkFlag(IGNORE_STEP_UP_TRANSFORMER)) {
        resistance = numeric_conversion<double>(strvec[11], 0.0);
        reactance = numeric_conversion<double>(strvec[12], 0.0);
        if ((resistance != 0) || (reactance != 0))  // need to add a step up transformer
        {
            auto* oBus = static_cast<GridBus*>(gen->find("bus"));
            GridBus* nBus = gBusfactory->makeTypeObject();
            auto* lnk = new AcLine(resistance * opt.base / machineBase,
                                   reactance * opt.base /
                                       machineBase);  // we need to adjust to the simulation base as
                                                      // opposed to the machine base

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
            if (gen->getName().starts_with(oBus->getName())) {
                lnk->setName(gen->getName() + "_TX");
                nBus->setName(gen->getName() + "_TXBUS");
            } else {
                lnk->setName(oBus->getName() + '_' + gen->getName() + "_TX");
                nBus->setName(oBus->getName() + '_' + gen->getName() + "_TXBUS");
            }
            // get the tap ratio
            const auto tapRatio = numeric_conversion<double>(strvec[13], 0.0);
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
    // get the line ratings
    // get the ratings
    auto ratA = numeric_conversion<double>(strvec[6], 0.0);
    auto ratB = numeric_conversion<double>(strvec[7], 0.0);
    auto ratC = numeric_conversion<double>(strvec[8], 0.0);

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
    if (opt.version >= 29) {
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
    if (code < 0)  // account for negative code values
    {
        lnk->set("mode", "manual");
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
    const auto impedanceCorrection = correctionFactor(correctionTables,
                                                      numeric_conversion<int>(strvec3[13], 0),
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
    if (controlCode > 0) {
        auto cbus = numeric_conversion<int>(strvec3[7], 0);
        if (cbus != 0) {
            if (abs(cbus) == ind1) {
                static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(1);
            } else if (abs(cbus) == ind2) {
                static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(2);
            }

            else {
                static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(busList[abs(cbus)]);
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

        resistance = numeric_conversion<double>(strvec3[8], 0.0);
        reactance = numeric_conversion<double>(strvec3[9], 0.0);

        if (controlCode == 3) {
            lnk->set("maxtapangle", resistance, deg);
            lnk->set("mintapangle", reactance, deg);
        } else {
            if (reactance < 1.0) {
                lnk->set("maxtap", resistance);
                lnk->set("mintap", reactance);
            } else {
                lnk->set("maxtap", resistance / vn1);
                lnk->set("mintap", reactance / vn1);
            }
        }

        resistance = numeric_conversion<double>(strvec3[10], 0.0);
        reactance = numeric_conversion<double>(strvec3[11], 0.0);

        if (controlCode == 3) {
            lnk->set("pmax", resistance, MW);
            lnk->set("pmin", reactance, MW);
        } else if (controlCode == 2) {
            lnk->set("qmax", resistance, MVAR);
            lnk->set("qmin", reactance, MVAR);
        } else {
            lnk->set("vmax", resistance);
            lnk->set("vmin", reactance);
        }
        resistance = numeric_conversion<double>(strvec3[12], 0.0);
        if (controlCode != 3) {
            lnk->set("nsteps", resistance);
        }
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
    if (code < 0)  // account for negative code values
    {
        lnk->set("mode", "manual");
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
    auto val = numeric_conversion<double>(strvec3[0], 0.0);
    if (val != 0) {
        lnk->set("tap", val);
    }
    val = numeric_conversion<double>(strvec3[2], 0.0);
    if (val != 0) {
        lnk->set("tapangle", val, deg);
    }
    // now get the stuff for the adjustable transformers
    // SGS set this for adjustable transformers....is this correct?
    if (controlCode > 0) {
        auto cbus = numeric_conversion<int>(strvec3[7], 0);
        if (cbus != 0) {
            /*if (abs(cbus) == ind1)
             {
                 static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(1);
             }
             else if (abs(cbus) == ind2)
             {
                 static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(2);
             }

             else */
            {
                //    static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(busList[abs(cbus)]);
            }
        }

        resistance = numeric_conversion<double>(strvec3[8], 0.0);
        reactance = numeric_conversion<double>(strvec3[9], 0.0);

        if (controlCode == 3) {
            lnk->set("maxtapangle", resistance, deg);
            lnk->set("mintapangle", reactance, deg);
        } else {
            lnk->set("maxtap", resistance);
            lnk->set("mintap", reactance);
        }
        if (opt.version >= 33) {
            resistance = numeric_conversion<double>(strvec3[12], 0.0);
            reactance = numeric_conversion<double>(strvec3[13], 0.0);
        } else {
            resistance = numeric_conversion<double>(strvec3[10], 0.0);
            reactance = numeric_conversion<double>(strvec3[11], 0.0);
        }
        if (controlCode == 3) {
            lnk->set("pmax", resistance, MW);
            lnk->set("pmin", reactance, MW);
        } else if (controlCode == 2) {
            lnk->set("qmax", resistance, MVAR);
            lnk->set("qmin", reactance, MVAR);
        } else {
            lnk->set("vmax", resistance);
            lnk->set("vmin", reactance);
        }
        resistance = numeric_conversion<double>(strvec3[12], 0.0);
        if (controlCode != 3) {
            lnk->set("nsteps", resistance);
        }
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
    // TODO(phlpt): Verify this logic; it may not be totally correct right now.
    // VERSION 32 has some ambiguity in the interpretation
    if (opt.version >= 32) {
        shift = 2;
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
    int start = 7;
    if (opt.version <= 27) {
        start = 5;
    } else if (opt.version >= 32) {
        start = 9;
    }
    const size_t ksize = strvec.size() - 1;
    for (size_t kk = start + 1; kk < ksize; kk += 2) {
        auto cnt = numeric_conversion<int>(strvec[kk], 0);
        auto block = numeric_conversion<double>(strvec[kk + 1], 0.0);
        if ((cnt > 0) && (block != 0.0)) {
            loadObject->addBlock(cnt, -block, MVAR);
        } else {
            break;
        }
    }
    // set the initial value
    auto initVal = numeric_conversion<double>(strvec[start], 0.0);

    // BINIT is the present shunt susceptance, not a request to change the
    // discrete/continuous SVD setting.  Set it directly so voltage-controlled
    // records retain their PowerWorld/PSS/E initial reactive injection.
    loadObject->ZipLoad::set("yq", -initVal, MVAR);
}

}  // namespace griddyn
