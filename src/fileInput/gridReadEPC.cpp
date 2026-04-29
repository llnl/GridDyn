/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/links/acLine.h"
#include "griddyn/links/adjustableTransformer.h"
#include "griddyn/links/dcLink.h"
#include "griddyn/loads/svd.h"
#include "griddyn/loads/zipLoad.h"
#include "griddyn/primary/acBus.h"
#include "griddyn/primary/dcBus.h"
#include "readerHelper.h"
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
// NOLINTBEGIN(misc-use-internal-linkage,readability-identifier-length,misc-const-correctness,modernize-use-integer-sign-comparison,bugprone-implicit-widening-of-multiplication-result,readability-isolate-declaration,modernize-use-starts-ends-with,bugprone-branch-clone,readability-qualified-auto)

using gmlc::utilities::numeric_conversion;
using gmlc::utilities::string_viewVector;
using gmlc::utilities::string_viewOps::default_bracket_chars;
using gmlc::utilities::string_viewOps::delimiter_compression;
using gmlc::utilities::string_viewOps::removeQuotes;
using gmlc::utilities::string_viewOps::split;
using gmlc::utilities::string_viewOps::splitlineBracket;
using gmlc::utilities::string_viewOps::toIntSimple;
using gmlc::utilities::string_viewOps::trim;
using gmlc::utilities::stringOps::trimString;
using std::string_view;
using units::deg;
using units::km;
using units::MVAR;
using units::MW;
using units::pu;
using units::puMW;

void epcReadBus(gridBus* bus, string_view line, double base, const basicReaderInfo& bri);
void epcReadDCBus(dcBus* bus, string_view line, double base, const basicReaderInfo& bri);
void epcReadLoad(zipLoad* ld, string_view line, double base);
void epcReadFixedShunt(zipLoad* ld, string_view line, double base);
void epcReadSwitchShunt(loads::svd* ld, string_view line, double /* base */);
void epcReadGen(Generator* gen, string_view line, double base);
void epcReadBranch(coreObject* parentObject,
                   string_view line,
                   double base,
                   std::vector<gridBus*>& busList,
                   const basicReaderInfo& bri);
void epcReadDCBranch(coreObject* parentObject,
                     string_view line,
                     double base,
                     std::vector<dcBus*>& dcbusList,
                     const basicReaderInfo& bri);
void epcReadTX(coreObject* parentObject,
               string_view line,
               double base,
               std::vector<gridBus*>& busList,
               const basicReaderInfo& bri);

double epcReadSolutionParamters(coreObject* parentObject, string_view line);

bool nextLine(std::ifstream& file, std::string& line)
{
    bool ret = true;
    while (ret) {
        if (std::getline(file, line)) {
            if (line[0] == '#')  // ignore comment lines
            {
                continue;
            }
            trimString(line);
            if (line.empty())  // continue over empty lines
            {
                continue;
            }
            while (line.back() == '/')  // get line continuation
            {
                line.pop_back();
                std::string temp1;
                if (std::getline(file, temp1)) {
                    line += " " + temp1;
                } else {
                    ret = false;
                }
            }
        } else {
            ret = false;
        }
        break;
    }
    return ret;
}

int getSectionCount(string_view line)
{
    auto bbegin = line.find_first_of('[');
    int cnt = -1;
    if (bbegin != std::string_view::npos) {
        auto bend = line.find_first_of(']', bbegin);
        cnt = numeric_conversion<int>(line.substr(bbegin + 1, (bend - bbegin - 1)), 0);
    }
    return cnt;
}

int getLineIndex(string_view line)
{
    gmlc::utilities::string_viewOps::trimString(line);
    auto pos = line.find_first_not_of("0123456789");
    return numeric_conversion<int>(line.substr(0, pos), -1);
}

void ignoreSection(std::string line, std::ifstream& file)
{
    int cnt = getSectionCount(line);

    int bcount = 0;
    if (cnt < 0) {
        cnt = kBigINT;
    }
    while (bcount < cnt) {
        nextLine(file, line);
        int index = getLineIndex(line);
        if (index < 0) {
        }
        ++bcount;
    }
}

void ProcessSection(std::string line,
                    std::ifstream& file,
                    const std::function<void(string_view)>& Func)
{
    int cnt = getSectionCount(line);

    int bcount = 0;
    if (cnt < 0) {
        cnt = kBigINT;
    }
    while (bcount < cnt) {
        nextLine(file, line);
        int index = getLineIndex(line);
        if (index < 0) {
        }
        ++bcount;
        Func(line);
    }
}

template<class X>
void ProcessSectionObject(std::string line,
                          std::ifstream& file,
                          const std::string& oname,
                          std::vector<gridBus*>& busList,
                          const std::function<void(X*, string_view)>& Func)
{
    int cnt = getSectionCount(line);

    int bcount = 0;
    if (cnt < 0) {
        cnt = kBigINT;
    }
    while (bcount < cnt) {
        nextLine(file, line);
        int index = getLineIndex(line);
        if (index < 0) {
        }
        ++bcount;

        if (index > static_cast<int>(busList.size())) {
            std::cerr << "Invalid bus number for " << oname << " " << index << '\n';
        }
        if (busList[index - 1] == nullptr) {
            std::cerr << "Invalid bus number for " << oname << " " << index << '\n';
        } else {
            auto obj = new X();
            busList[index - 1]->add(obj);
            Func(obj, line);
        }
    }
}

void loadEPC(coreObject* parentObject, const std::string& fileName, const basicReaderInfo& bri)
{
    std::ifstream file(fileName.c_str(), std::ios::in);

    std::string temp1;  // temporary storage for substrings
    std::vector<gridBus*> busList;
    std::vector<dcBus*> dcbusList;
    int index;
    double base = 100;
    int cnt, bcount;

    /* Process the first line
    First card in file.
    */

    std::string line;  // line storage
    while (nextLine(file, line)) {
        auto tokens = split(line, " \t");
        gmlc::utilities::string_viewOps::trimString(tokens[0]);
        if (tokens[0] == "title") {
            std::string title;
            while (std::getline(file, temp1)) {
                if (temp1[0] == '!') {
                    break;
                }
                title += temp1;
            }
            if (title.size() > 50) {
                parentObject->setName(std::string{trim(title.substr(0, 50))});
                parentObject->setDescription(title);
            } else {
                parentObject->setName(title);
            }
        } else if (tokens[0] == "comments") {
            std::string comments;
            while (std::getline(file, temp1)) {
                if (temp1[0] == '!') {
                    break;
                }
                comments += temp1;
            }
            trimString(comments);
            if (!comments.empty()) {
                parentObject->set("description", parentObject->getDescription() + comments);
            }
        } else if (tokens[0] == "bus") {
            cnt = getSectionCount(line);

            bcount = 0;
            if (cnt < 0) {
                cnt = kBigINT;
            } else if (cnt > static_cast<int>(busList.size())) {
                busList.resize(cnt + 2);
            }
            while (bcount < cnt) {
                nextLine(file, line);
                index = getLineIndex(line);
                if (index < 0) {
                }
                ++bcount;
                if (index > static_cast<int>(busList.size())) {
                    if (index < 100000000) {
                        busList.resize(2 * index, nullptr);
                    } else {
                        std::cerr << "Bus index overload " << index << '\n';
                    }
                }
                if (busList[index - 1] == nullptr) {
                    busList[index - 1] = new acBus();
                    busList[index - 1]->set("basepower", base);
                    epcReadBus(busList[index - 1], line, base, bri);
                    try {
                        parentObject->add(busList[index - 1]);
                    }
                    catch (const objectAddFailure&) {
                        addToParentRename(busList[index - 1], parentObject);
                    }
                } else {
                    std::cerr << "Invalid bus code " << index << '\n';
                }
            }
        } else if (tokens[0] == "solution") {
            nextLine(file, line);
            while (line[0] != '!') {
                if (line.compare(0, 5, "sbase") == 0) {
                    base = epcReadSolutionParamters(parentObject, line);
                } else {
                    epcReadSolutionParamters(parentObject, line);
                }
                nextLine(file, line);
            }
        } else if (tokens[0] == "branch") {
            ProcessSection(line, file, [&](string_view config) {
                epcReadBranch(parentObject, config, base, busList, bri);
            });
        } else if (tokens[0] == "transformer") {
            ProcessSection(line, file, [&](string_view config) {
                epcReadTX(parentObject, config, base, busList, bri);
            });
        } else if (tokens[0] == "generator") {
            ProcessSectionObject<Generator>(
                line, file, "generator", busList, [base](Generator* gen, string_view config) {
                    epcReadGen(gen, config, base);
                });
        } else if (tokens[0] == "load") {
            ProcessSectionObject<zipLoad>(
                line, file, "load", busList, [base](zipLoad* ld, string_view config) {
                    epcReadLoad(ld, config, base);
                });
        } else if (tokens[0] == "shunt") {
            ProcessSectionObject<zipLoad>(
                line, file, "shunt", busList, [base](zipLoad* ld, string_view config) {
                    epcReadFixedShunt(ld, config, base);
                });
        } else if (tokens[0] == "svd") {
            ProcessSectionObject<loads::svd>(
                line, file, "svd", busList, [base](loads::svd* ld, string_view config) {
                    epcReadSwitchShunt(ld, config, base);
                });
        } else if (tokens[0] == "area") {
            ignoreSection(line, file);
        } else if (tokens[0] == "zone") {
            ignoreSection(line, file);
        } else if (tokens[0] == "interface") {
            ignoreSection(line, file);
        } else if (tokens[0] == "z") {
            ignoreSection(line, file);
        } else if (tokens[0] == "dc") {
            if (tokens[1] == "bus") {
                cnt = getSectionCount(line);

                bcount = 0;
                if (cnt < 0) {
                    cnt = kBigINT;
                } else if (cnt > static_cast<int>(dcbusList.size())) {
                    dcbusList.resize(cnt + 2);
                }
                while (bcount < cnt) {
                    nextLine(file, line);
                    index = getLineIndex(line);
                    if (index < 0) {
                    }
                    ++bcount;
                    if (index > static_cast<int>(dcbusList.size())) {
                        if (index < 100000000) {
                            dcbusList.resize(2 * index, nullptr);
                        } else {
                            std::cerr << "Bus index overload " << index << '\n';
                        }
                    }
                    if (dcbusList[index - 1] == nullptr) {
                        dcbusList[index - 1] = new dcBus();
                        dcbusList[index - 1]->set("basepower", base);
                        epcReadDCBus(dcbusList[index - 1], line, base, bri);
                        try {
                            parentObject->add(dcbusList[index - 1]);
                        }
                        catch (const objectAddFailure&) {
                            addToParentRename(dcbusList[index - 1], parentObject);
                        }
                    } else {
                        std::cerr << "Invalid bus code " << index << '\n';
                    }
                }
            } else if (tokens[1] == "line") {
                ProcessSection(line, file, [&](string_view config) {
                    epcReadDCBranch(parentObject, config, base, dcbusList, bri);
                });
            } else if (tokens[1] == "converter") {
            }
        } else if (tokens[0] == "gcd") {
            ignoreSection(line, file);
        } else if (tokens[0] == "owner") {
            ignoreSection(line, file);
        } else if (tokens[0] == "transaction") {
            ignoreSection(line, file);
        } else if (tokens[0] == "qtable") {
            ignoreSection(line, file);
        } else if (tokens[0] == "end") {
            break;
        } else {
            std::cerr << "unrecognized token " << tokens[0] << '\n';
        }
    }
    file.close();
}

/**
tap
<1 or 0>
TCUL adjustment flag
phas
<1 or 0>
Phase shifter adjustment flag
area
<1 or 0>
Area interchange control flag
svd
<1 or 0>
Control shunt adjustment flag
dctap
<1 or 0>
DC converter control flag
gcd
<1 or 0>
GCD control flag
jump
<value>
Jumper threshold impedance, pu
toler
<value>
Newton solution tolerance, MVA
sbase
<value>
System base, MVA
*/

double epcReadSolutionParamters(coreObject* parentObject, string_view line)
{
    auto tokens = split(line, " ", delimiter_compression::on);
    auto val = numeric_conversion<double>(tokens[1], 0.0);
    if (tokens[0] == "tap") {
    } else if (tokens[0] == "phas") {
    } else if (tokens[0] == "area") {
    } else if (tokens[0] == "svd") {
    } else if (tokens[0] == "dctap") {
    } else if (tokens[0] == "gcd") {
    } else if (tokens[0] == "jump") {
    } else if (tokens[0] == "toler") {
        parentObject->set("tolerance", val);
    } else if (tokens[0] == "sbase") {
        parentObject->set("basepower", val);
    } else {
        std::cerr << "unknown solution parameter\n";
    }

    return val;
}

void epcReadBus(gridBus* bus, string_view line, double /*base*/, const basicReaderInfo& bri)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
    // get the bus name
    auto temp = strvec[0];
    std::string temp2 = std::string{trim(removeQuotes(strvec[1]))};

    if (bri.prefix.empty()) {
        if (temp2.empty())  // 12 spaces is default value which would all get trimmed
        {
            temp2 = "BUS_" + std::string{temp};
        }
    } else {
        if (temp2.empty())  // 12 spaces is default value which would all get trimmed
        {
            temp2 = bri.prefix + '_' + std::string{temp};
        } else {
            temp2 = bri.prefix + '_' + temp2;
        }
    }
    bus->setName(temp2);

    // get the localBaseVoltage
    auto bv = numeric_conversion<double>(strvec[2], -1.0);
    if (bv > 0.0) {
        bus->set("basevoltage", bv);
    }

    auto type = numeric_conversion<int>(strvec[3], 1);

    switch (type) {
        case 1:
            temp = "PQ";
            break;
        case 2:
        case -2:
            temp = "PV";
            break;
        case 0:
            temp = "swing";
            break;
        default:
            temp = "PQ";
            break;
    }
    bus->set("type", std::string{temp});
    // skip the load flow area and loss zone for now
    // skip the owner information
    // get the voltage and angle specifications
    auto vm = numeric_conversion<double>(strvec[4], 0.0);
    if (vm != 0) {
        bus->set("vtarget", vm);
    }
    vm = numeric_conversion<double>(strvec[5], 0.0);
    auto va = numeric_conversion<double>(strvec[6], 0.0);
    if (va != 0) {
        bus->set("angle", va, deg);
    }
    if (vm != 0) {
        bus->set("voltage", vm);
    }

    // auto area = numeric_conversion<int>(strvec[7], 0);
    auto zone = numeric_conversion<int>(strvec[8], 0);
    if (zone != 0) {
        bus->set("zone", static_cast<double>(zone));
    }
    vm = numeric_conversion<double>(strvec[9], 0.0);
    va = numeric_conversion<double>(strvec[10], 0.0);
    if (va != 0) {
        bus->set("vmin", va);
    }
    if (vm != 0) {
        bus->set("vmax", vm);
    }
}

void epcReadDCBus(dcBus* bus, string_view line, double /*base*/, const basicReaderInfo& bri)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
    // get the bus name
    auto temp = strvec[0];
    std::string temp2 = std::string{trim(removeQuotes(strvec[1]))};

    if (bri.prefix.empty()) {
        if (temp2.empty())  // 12 spaces is default value which would all get trimmed
        {
            temp2 = "BUS_" + std::string{temp};
        }
    } else {
        if (temp2.empty())  // 12 spaces is default value which would all get trimmed
        {
            temp2 = bri.prefix + '_' + std::string{temp};
        } else {
            temp2 = bri.prefix + '_' + temp2;
        }
    }
    bus->setName(temp2);

    // get the localBaseVoltage
    auto bv = numeric_conversion<double>(strvec[2], -1.0);
    if (bv > 0.0) {
        bus->set("basevoltage", bv);
    }
    auto type = numeric_conversion<int>(strvec[3], 1);
    switch (type) {
        case 1:
            temp = "PQ";
            break;
        case 2:
        case -2:
            temp = "PV";
            break;
        case 0:
            temp = "swing";
            break;
        default:
            temp = "PQ";
            break;
    }
    bus->set("type", std::string{temp});

    // skip the load flow area and loss zone for now
    // skip the owner information
    // get the voltage and angle specifications
    auto vm = numeric_conversion<double>(strvec[7], 0.0);
    if (vm != 0) {
        bus->set("voltage", vm);
    }

    // auto area = numeric_conversion<int>(strvec[7], 0);
    auto zone = numeric_conversion<int>(strvec[4], 0);
    if (zone != 0) {
        bus->set("zone", static_cast<double>(zone));
    }
}

// #load data  [10485]          id   ------------long_id_------------     st      mw      mvar mw_i
//  mvar_i
//  mw_z      mvar_z  ar zone  date_in date_out pid N own sdmon nonc ithbus ithflag
void epcReadLoad(zipLoad* ld, string_view line, double /*base*/)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);

    // get the load index and name
    std::string prefix = ld->getParent()->getName() + "_Load";
    if (!strvec[3].empty()) {
        prefix += '_' + std::string{strvec[3]};
    }
    ld->setName(prefix);
    auto long_id = trim(removeQuotes(strvec[4]));
    if (!long_id.empty()) {
        ld->setDescription(std::string{long_id});
    }
    // get the status
    int status = toIntSimple(strvec[5]);
    if (status == 0) {
        ld->disable();
    }
    // skip the area and zone information for now

    // get the constant power part of the load
    auto p = numeric_conversion<double>(strvec[6], 0.0);
    auto q = numeric_conversion<double>(strvec[7], 0.0);
    if (p != 0.0) {
        ld->set("p", p, MW);
    }
    if (q != 0.0) {
        ld->set("q", q, MVAR);
    }
    // get the constant current part of the load
    p = numeric_conversion<double>(strvec[8], 0.0);
    q = numeric_conversion<double>(strvec[9], 0.0);
    if (p != 0.0) {
        ld->set("ip", p, MW);
    }
    if (q != 0.0) {
        ld->set("iq", q, MVAR);
    }
    // get the impedance part of the load
    // note:: in PU power units, need to convert to Pu resistance
    p = numeric_conversion<double>(strvec[10], 0.0);
    q = numeric_conversion<double>(strvec[11], 0.0);
    if (p != 0.0) {
        ld->set("r", p, MW);
    }
    if (q != 0.0) {
        ld->set("x", q, MVAR);
    }
    // ignore the owner field
}

// #shunt data  [1988]         id                               ck  se  long_id_     st ar zone
// pu_mw
//  pu_mvar
//  date_in date_out pid N own part1 own part2 own part3 own part4 --num--  --name--  --kv--

void epcReadFixedShunt(zipLoad* ld, string_view line, double /*base*/)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);

    // get the load index and name
    std::string prefix = ld->getParent()->getName() + "_Shunt";
    if (!strvec[7].empty()) {
        prefix += '_' + std::string{trim(strvec[7])};
    }

    auto long_id = trim(removeQuotes(strvec[4]));
    if (!long_id.empty()) {
        ld->setDescription(std::string{long_id});
    }

    ld->setName(prefix);

    // get the status
    int status = toIntSimple(strvec[10]);
    if (status == 0) {
        ld->disable();
    }
    // skip the area and zone information for now

    // get the constant power part of the load
    auto p = numeric_conversion<double>(strvec[13], 0.0);
    auto q = numeric_conversion<double>(strvec[14], 0.0);
    if (p != 0.0) {
        ld->set("yp", p, puMW);
    }
    if (q != 0.0) {
        ld->set("yq", -q, puMW);
    }
}

// #svd data[1253]            id  ------------long_id_------------  st ty --no-- - reg_name
//  ar zone      g      b  min_c  max_c  vband   bmin   bmax  date_in date_out pid N
//  own part1 own part2 own part3 own part4
void epcReadSwitchShunt(loads::svd* ld, string_view line, double /*base*/)
{
    auto strvec = splitlineBracket(line, " ", default_bracket_chars, delimiter_compression::on);
    auto sz = strvec.size();
    // get the load index and name
    std::string prefix = ld->getParent()->getName() + "_svd";

    auto long_id = trim(removeQuotes(strvec[1]));
    if (!long_id.empty()) {
        ld->setDescription(std::string{long_id});
    }

    ld->setName(prefix);

    int offset = 2;
    while (strvec[offset] != ":") {
        ++offset;
    }
    // get the status
    int status = toIntSimple(strvec[offset + 1]);
    if (status == 0) {
        ld->disable();
    }
    // skip the area and zone information for now

    auto cbus = numeric_conversion<int>(strvec[offset + 3], -1);
    gridBus* rbus = nullptr;
    if (cbus <= 0) {
        rbus = static_cast<gridBus*>(ld->getParent());
    } else {
        rbus = static_cast<gridBus*>(ld->getRoot()->find(std::string("#") + std::to_string(cbus)));
    }
    int mode = toIntSimple(strvec[offset + 2]);
    double high;
    double low;
    int bsize = 6;
    switch (mode) {
        case 0:
            ld->set("mode", "manual");
            bsize = 4;
            break;
        case 1:
            ld->set("mode", "stepped");
            // ld->set("vmax", high);
            // ld->set("vmin", low);
            if (rbus != nullptr) {
                ld->setControlBus(rbus);
            }

            break;
        case 2:
            bsize = 4;
            ld->set("mode", "cont");
            //    ld->set("vmax", high);
            //    ld->set("vmin", low);
            if (rbus != nullptr) {
                ld->setControlBus(rbus);
            }
            break;
        case 3:
            ld->set("mode", "stepped");
            ld->set("control", "reactive");
            //    ld->set("qmax", high);
            //    ld->set("qmin", low);
            if (rbus != nullptr) {
                ld->setControlBus(rbus);
            }
            break;
        case 4:
            ld->set("mode", "stepped");
            ld->set("control", "reactive");
            high = numeric_conversion<double>(strvec[sz - 5], 0.0);
            low = numeric_conversion<double>(strvec[sz - 6], 0.0);
            ld->set("qmax", high);
            ld->set("qmin", low);
            if (rbus != nullptr) {
                ld->setControlBus(rbus);
            }
            // TODO(phlpt): Handle the unusual PT load target object condition.
            break;
        case 5:
            ld->set("mode", "stepped");
            ld->set("control", "reactive");
            //    ld->set("qmax", high);
            //    ld->set("qmin", low);
            if (rbus != nullptr) {
                ld->setControlBus(rbus);
            }
            break;
        case 6:
            ld->set("mode", "stepped");
            ld->set("control", "reactive");
            //    ld->set("qmax", high);
            //    ld->set("qmin", low);
            if (rbus != nullptr) {
                ld->setControlBus(rbus);
            }
            // TODO(phlpt): Handle the unusual PT load target object condition.
            break;
        default:
            ld->set("mode", "manual");
            break;
    }
    // load the switched shunt blocks

    for (size_t kk = offset + 26; kk < sz - bsize; kk += 2) {
        auto cnt = numeric_conversion<int>(strvec[kk], 0);
        auto block = numeric_conversion<double>(strvec[kk + 1], 0.0);
        if ((cnt > 0) && (block != 0.0)) {
            ld->addBlock(cnt, -block, pu);
        } else {
            break;
        }
    }
    // set the initial value
    auto initVal = numeric_conversion<double>(strvec[offset + 8], 0.0);

    ld->set("yq", -initVal, pu);
}
// #generator data  [XXX]    id   ------------long_id_------------    st ---no--     reg_name prf
// qrf
//  ar
//  zone   pgen   pmax   pmin   qgen   qmax   qmin   mbase   cmp_r cmp_x gen_r gen_x           hbus
//  tbus           date_in date_out pid N
// #-rtran -xtran -gtap- ow1 part1 ow2 part2 ow3 part3 ow4 part4 ow5 part5 ow6 part6 ow7 part7 ow8
//  part8 gov agc
//  disp basld air turb qtab pmax2 sdmon

void epcReadGen(Generator* gen, string_view line, double /*base*/)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);

    // get the gen index and name
    std::string prefix = gen->getParent()->getName() + "_Gen";
    if (!trim(removeQuotes(strvec[3])).empty()) {
        prefix += '_' + std::string{strvec[3]};
    }
    if (!trim(removeQuotes(strvec[4])).empty()) {
        gen->setName(std::string{trim(removeQuotes(strvec[4]))});
    } else {
        gen->setName(prefix);
    }

    // get the status
    int status = toIntSimple(strvec[5]);
    if (status == 0) {
        gen->disable();
    }

    // get the power generation
    auto p = numeric_conversion<double>(strvec[13], 0.0);
    auto q = numeric_conversion<double>(strvec[16], 0.0);
    if (p != 0.0) {
        gen->set("p", p, MW);
    }
    if (q != 0.0) {
        gen->set("q", q, MVAR);
    }
    // get the Pmax and Pmin
    p = numeric_conversion<double>(strvec[14], 0.0);
    q = numeric_conversion<double>(strvec[15], 0.0);
    if (p != 0.0) {
        gen->set("pmax", p, MW);
    }
    if (q != 0.0) {
        gen->set("pmin", q, MW);
    }
    // get the Qmax and Qmin
    p = numeric_conversion<double>(strvec[17], 0.0);
    q = numeric_conversion<double>(strvec[18], 0.0);
    if (p != 0.0) {
        gen->set("qmax", p, MVAR);
    }
    if (q != 0.0) {
        gen->set("qmin", q, MVAR);
    }
    // get the machine base
    auto mb = numeric_conversion<double>(strvec[19], 0.0);
    gen->set("mbase", mb);

    mb = numeric_conversion<double>(strvec[22], 0.0);
    gen->set("rs", mb);

    mb = numeric_conversion<double>(strvec[23], 0.0);
    gen->set("xs", mb);

    auto rbus = numeric_conversion<int>(strvec[6], 0);

    if (rbus != 0) {
        // TODO(phlpt): Handle the remote-controlled bus case.
    }
    // TODO(phlpt): Get the impedance fields and other data.
}

/** function to generate a name for a line based on the input data*/
std::string generateLineName(const string_viewVector& svec, const std::string& prefix)
{
    std::string temp = std::string{trim(removeQuotes(svec[1]))};
    std::string temp2;
    if (temp.empty()) {
        temp = std::string{trim(svec[0])};
    }
    if (prefix.empty()) {
        temp2 = temp + "_to_";
    } else {
        temp2 = prefix + '_' + temp + "_to_";
    }

    temp = std::string{trim(removeQuotes(svec[4]))};
    if (temp.empty()) {
        temp = std::string{trim(svec[3])};
    }
    temp2 = temp2 + temp;
    if (trim(svec[7]) != "1") {
        temp2.push_back('_');
        temp2.push_back(trim(svec[7])[0]);
    }
    return temp2;
}

// #branch data[17003]                                ck  se------------long_id_------------st
// resist
//  react
//  charge   rate1  rate2  rate3  rate4 aloss  lngth #ar zone trangi tap_f tap_t  date_in date_out
//  pid N ty  rate5 rate6  rate7  rate8 ow1 part1 ow2 part2 ow3 part3 ow4 part4 ow5 part5 ow6 part6
//  ow7 part7 ow8 part8 ohm sdmon
// #
void epcReadBranch(coreObject* parentObject,
                   string_view line,
                   double base,
                   std::vector<gridBus*>& busList,
                   const basicReaderInfo& bri)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);

    // get the name of the from bus

    auto ind1 = numeric_conversion<int>(strvec[0], 0);

    auto ind2 = numeric_conversion<int>(strvec[3], 0);

    gridBus* bus1 = busList[ind1 - 1];
    gridBus* bus2 = busList[ind2 - 1];

    // check the circuit identifier
    auto name = generateLineName(strvec, bri.prefix);
    auto lnk = new acLine(name);
    auto long_id = trim(removeQuotes(strvec[8]));
    if (!long_id.empty()) {
        lnk->setDescription(std::string{long_id});
    }

    // set the base power to that used this model
    lnk->set("basepower", base);
    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);

    addToParentRename(lnk, parentObject);
    // get the branch parameters
    int status = toIntSimple(strvec[9]);
    if (status == 0) {
        lnk->disable();
    }

    auto R = numeric_conversion<double>(strvec[10], 0.0);
    auto X = numeric_conversion<double>(strvec[11], 0.0);

    lnk->set("r", R);
    lnk->set("x", X);

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    // get line capacitance
    auto val = numeric_conversion<double>(strvec[12], 0.0);
    if (val != 0) {
        lnk->set("b", val);
    }
    val = numeric_conversion<double>(strvec[13], 0.0);
    if (val != 0) {
        lnk->set("ratinga", val);
    }
    val = numeric_conversion<double>(strvec[14], 0.0);
    if (val != 0) {
        lnk->set("ratingb", val);
    }
    val = numeric_conversion<double>(strvec[15], 0.0);
    if (val != 0) {
        lnk->set("erating", val);
    }

    val = numeric_conversion<double>(strvec[18], 0.0);
    if (val != 0) {
        lnk->set("length", val, km);
    }
}

// #dc line data[0]                                  ck------------long_id_------------st ar zone
//  resist   react
//  capac   rate1  rate2  rate3  rate4  len  aloss    date_in date_out PID N  rate5  rate6  rate7
//  rate8 #len-- - loss - date_in date_out pid N  rate5  rate6  rate7  rate8 ow1 part1 ow2 part2 ow3
//  part3 ow4 part4 ow5 part5 ow6 part6 ow7 part7 ow8 part8

void epcReadDCBranch(coreObject* parentObject,
                     string_view line,
                     double base,
                     std::vector<dcBus*>& dcbusList,
                     const basicReaderInfo& bri)
{
    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);

    // get the name of the from bus

    auto ind1 = numeric_conversion<int>(strvec[0], 0);

    auto ind2 = numeric_conversion<int>(strvec[3], 0);

    dcBus* bus1 = dcbusList[ind1 - 1];
    dcBus* bus2 = dcbusList[ind2 - 1];

    // check the circuit identifier
    auto name = generateLineName(strvec, bri.prefix);
    auto lnk = new links::dcLink(name);
    auto long_id = trim(removeQuotes(strvec[7]));
    if (!long_id.empty()) {
        lnk->setDescription(std::string{long_id});
    }

    // set the base power to that used this model
    lnk->set("basepower", base);
    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);

    addToParentRename(lnk, parentObject);
    // get the branch parameters
    int status = toIntSimple(strvec[8]);
    if (status == 0) {
        lnk->disable();
    }

    auto R = numeric_conversion<double>(strvec[11], 0.0);
    auto X = numeric_conversion<double>(strvec[12], 0.0);

    lnk->set("r", R);
    lnk->set("x", X);

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    // get line capacitance
    // not sure what to do with capacitance
    // double val = numeric_conversion<double>(strvec[13], 0.0);
    // if (val != 0)
    {
        // lnk->set("b", val);
    }
    auto val = numeric_conversion<double>(strvec[14], 0.0);
    if (val != 0) {
        lnk->set("ratinga", val);
    }
    val = numeric_conversion<double>(strvec[15], 0.0);
    if (val != 0) {
        lnk->set("ratingb", val);
    }
    val = numeric_conversion<double>(strvec[16], 0.0);
    if (val != 0) {
        lnk->set("erating", val);
    }

    val = numeric_conversion<double>(strvec[18], 0.0);
    if (val != 0) {
        lnk->set("length", val, km);
    }
}
void epcReadTX(coreObject* parentObject,
               string_view line,
               double base,
               std::vector<gridBus*>& busList,
               const basicReaderInfo& bri)
{
    Link* lnk;
    int code;
    double val;
    int status;
    int cbus;

    auto strvec = splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
    // get the name of the from bus

    auto ind1 = numeric_conversion<int>(strvec[0], 0);

    auto ind2 = numeric_conversion<int>(strvec[3], 0);

    gridBus* bus1 = busList[ind1 - 1];
    gridBus* bus2 = busList[ind2 - 1];

    // check the circuit identifier

    auto name = generateLineName(strvec, (bri.prefix.empty()) ? "TX_" : (bri.prefix + "_TX_"));
    code = numeric_conversion<int>(strvec[9], 1);
    switch (code) {
        case 1:
        case 11:
            code = 1;
            lnk = new acLine(name);
            // lnk->set ("type", "transformer");
            break;
        case 2:
        case 12:
            code = 2;
            lnk = new links::adjustableTransformer(name);
            lnk->set("mode", "voltage");
            break;
        case 3:
        case 13:
            code = 3;
            lnk = new links::adjustableTransformer(name);
            lnk->set("mode", "mvar");
            break;
        case 4:
        case 14:
            code = 4;
            lnk = new links::adjustableTransformer(name);
            lnk->set("mode", "mw");
            break;
        default:
            std::cerr << "unrecognized transformer code\n";
            return;
    }
    // set the base power to that used this model
    lnk->set("basepower", base);
    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);

    auto long_id = trim(removeQuotes(strvec[7]));
    if (!long_id.empty()) {
        lnk->setDescription(std::string{long_id});
    }

    addToParentRename(lnk, parentObject);
    // get the branch parameters
    status = toIntSimple(strvec[9]);
    if (status == 0) {
        lnk->disable();
    }

    double tbase = base;
    tbase = numeric_conversion<double>(strvec[22], 0.0);
    // primary and secondary winding resistance
    auto R = numeric_conversion<double>(strvec[23], 0.0);
    auto X = numeric_conversion<double>(strvec[24], 0.0);

    lnk->set("r", R * tbase / base);
    lnk->set("x", X * tbase / base);

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    // get line capacitance

    val = numeric_conversion<double>(strvec[35], 0.0);
    if (val != 0) {
        lnk->set("ratinga", val);
    }
    val = numeric_conversion<double>(strvec[36], 0.0);
    if (val != 0) {
        lnk->set("ratingb", val);
    }
    val = numeric_conversion<double>(strvec[37], 0.0);
    if (val != 0) {
        lnk->set("erating", val);
    }

    val = numeric_conversion<double>(strvec[45], 0.0);
    if (val != 0) {
        lnk->set("tap", val);
    }
    val = numeric_conversion<double>(strvec[32], 0.0);
    if (val != 0) {
        lnk->set("tapangle", val, deg);
    }
    // now get the stuff for the adjustable transformers
    if (code > 1) {
        cbus = numeric_conversion<int>(strvec[10], 0);
        if (cbus != 0) {
            static_cast<links::adjustableTransformer*>(lnk)->setControlBus(busList[cbus - 1]);
        }
        R = numeric_conversion<double>(strvec[40], 0.0);
        X = numeric_conversion<double>(strvec[41], 0.0);
        if (code == 4) {
            lnk->set("maxtapangle", R, deg);
            lnk->set("mintapangle", X, deg);
        } else {
            lnk->set("maxtap", R);
            lnk->set("mintap", X);
        }
        R = numeric_conversion<double>(strvec[42], 0.0);
        X = numeric_conversion<double>(strvec[43], 0.0);
        if (code == 4) {
            lnk->set("pmax", R, MW);
            lnk->set("pmin", X, MW);
        } else if (code == 3) {
            lnk->set("qmax", R, MVAR);
            lnk->set("qmin", X, MVAR);
        } else {
            lnk->set("vmax", R);
            lnk->set("vmin", X);
        }
        R = numeric_conversion<double>(strvec[44], 0.0);
        if (code == 4) {
            lnk->set("stepsize", R, deg);
        } else {
            lnk->set("stepsize", R);
        }
    }
}

// NOLINTEND(misc-use-internal-linkage,readability-identifier-length,misc-const-correctness,modernize-use-integer-sign-comparison,bugprone-implicit-widening-of-multiplication-result,readability-isolate-declaration,modernize-use-starts-ends-with,bugprone-branch-clone,readability-qualified-auto)
}  // namespace griddyn
