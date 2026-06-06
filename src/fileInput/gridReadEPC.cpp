/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include "readerHelper.h"
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {

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

namespace {
    void epcReadBus(GridBus* bus, string_view line, double base, const BasicReaderInfo& bri);
    void epcReadDCBus(DcBus* bus, string_view line, double base, const BasicReaderInfo& bri);
    void epcReadLoad(ZipLoad* load, string_view line, double base);
    void epcReadFixedShunt(ZipLoad* load, string_view line, double base);
    void epcReadSwitchShunt(loads::Svd* load, string_view line, double /* base */);
    void epcReadGen(Generator* gen, string_view line, double base);
    void epcReadBranch(CoreObject* parentObject,
                       string_view line,
                       double base,
                       std::vector<GridBus*>& busList,
                       const BasicReaderInfo& bri);
    void epcReadDCBranch(CoreObject* parentObject,
                         string_view line,
                         double base,
                         std::vector<DcBus*>& dcbusList,
                         const BasicReaderInfo& bri);
    void epcReadTX(CoreObject* parentObject,
                   string_view line,
                   double base,
                   std::vector<GridBus*>& busList,
                   const BasicReaderInfo& bri);

    double epcReadSolutionParamters(CoreObject* parentObject, string_view line);

    bool nextLine(std::ifstream& file, std::string& line)
    {
        bool ret = true;
        while (ret) {
            if (std::getline(file, line)) {
                if (line.empty()) {
                    continue;
                }
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
            if ((bend != std::string_view::npos) && (bend > bbegin + 1)) {
                const auto countText = trim(line.substr(bbegin + 1, bend - bbegin - 1));
                if (!countText.empty()) {
                    cnt = numeric_conversion<int>(countText, 0);
                } else {
                    cnt = 0;
                }
        }
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
            const int index = getLineIndex(line);
            if (index < 0) {
            }
            ++bcount;
        }
    }

    void processSection(std::string line,
                        std::ifstream& file,
                        const std::function<void(string_view)>& func)
    {
        int cnt = getSectionCount(line);

        int bcount = 0;
        if (cnt < 0) {
            cnt = kBigINT;
        }
        while (bcount < cnt) {
            if (!nextLine(file, line)) {
                break;
            }
            const int index = getLineIndex(line);
            if (index < 0) {
            }
            ++bcount;
            func(line);
        }
    }

    template<class X>
    void processSectionObject(std::string line,
                              std::ifstream& file,
                              const std::string& oname,
                              std::vector<GridBus*>& busList,
                              const std::function<void(X*, string_view)>& func)
    {
        int cnt = getSectionCount(line);

        int bcount = 0;
        if (cnt < 0) {
            cnt = kBigINT;
        }
        while (bcount < cnt) {
            if (!nextLine(file, line)) {
                break;
            }
            const int index = getLineIndex(line);
            if (index < 0) {
            }
            ++bcount;

            if (std::cmp_greater(index, busList.size())) {
                std::cerr << "Invalid bus number for " << oname << " " << index << '\n';
            }
            if (busList[index - 1] == nullptr) {
                std::cerr << "Invalid bus number for " << oname << " " << index << '\n';
            } else {
                auto* obj = new X();
                busList[index - 1]->add(obj);
                func(obj, line);
            }
        }
    }

}  // namespace

void loadEpc(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    const auto& bri = readerOptions;
    std::ifstream file(fileName.c_str(), std::ios::in);

    std::string temp1;  // temporary storage for substrings
    std::vector<GridBus*> busList;
    std::vector<DcBus*> dcbusList;
    int index;
    double base = 100;
    int cnt;
    int bcount;

    /* Process the first line
    First card in file.
    */

    std::string line;  // line storage
    while (nextLine(file, line)) {
        auto tokens = split(line, " \t");
        if (tokens.empty()) {
            continue;
        }
        gmlc::utilities::string_viewOps::trimString(tokens[0]);
        if (tokens[0] == "title") {
            std::string title;
            while (std::getline(file, temp1)) {
                if (temp1.empty()) {
                    continue;
                }
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
                if (temp1.empty()) {
                    continue;
                }
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
            } else if (std::cmp_greater(cnt, busList.size())) {
                busList.resize(cnt + 2);
            }
            while (bcount < cnt) {
                nextLine(file, line);
                index = getLineIndex(line);
                if (index < 0) {
                }
                ++bcount;
                if (std::cmp_greater(index, busList.size())) {
                    if (index < 100000000) {
                        busList.resize(static_cast<std::vector<GridBus*>::size_type>(
                                           static_cast<std::int64_t>(2) * index),
                                       nullptr);
                    } else {
                        std::cerr << "Bus index overload " << index << '\n';
                    }
                }
                if (busList[index - 1] == nullptr) {
                    busList[index - 1] = new AcBus();
                    busList[index - 1]->set("basepower", base);
                    epcReadBus(busList[index - 1], line, base, bri);
                    try {
                        parentObject->add(busList[index - 1]);
                    }
                    catch (const ObjectAddFailure&) {
                        addToParentWithRename(busList[index - 1], parentObject);
                    }
                } else {
                    std::cerr << "Invalid bus code " << index << '\n';
                }
            }
        } else if (tokens[0] == "solution") {
            if (!nextLine(file, line)) {
                break;
            }
            while (!line.empty() && (line[0] != '!')) {
                if (line.starts_with("sbase")) {
                    base = epcReadSolutionParamters(parentObject, line);
                } else {
                    epcReadSolutionParamters(parentObject, line);
                }
                if (!nextLine(file, line)) {
                    break;
                }
            }
        } else if (tokens[0] == "branch") {
            processSection(line, file, [&](string_view config) {
                epcReadBranch(parentObject, config, base, busList, bri);
            });
        } else if (tokens[0] == "transformer") {
            processSection(line, file, [&](string_view config) {
                epcReadTX(parentObject, config, base, busList, bri);
            });
        } else if (tokens[0] == "generator") {
            processSectionObject<Generator>(
                line, file, "generator", busList, [base](Generator* gen, string_view config) {
                    epcReadGen(gen, config, base);
                });
        } else if (tokens[0] == "load") {
            processSectionObject<ZipLoad>(
                line, file, "load", busList, [base](ZipLoad* load, string_view config) {
                    epcReadLoad(load, config, base);
                });
        } else if (tokens[0] == "shunt") {
            processSectionObject<ZipLoad>(
                line, file, "shunt", busList, [base](ZipLoad* load, string_view config) {
                    epcReadFixedShunt(load, config, base);
                });
        } else if (tokens[0] == "svd") {
            processSectionObject<loads::Svd>(
                line, file, "svd", busList, [base](loads::Svd* load, string_view config) {
                    epcReadSwitchShunt(load, config, base);
                });
        } else if ((tokens[0] == "area") || (tokens[0] == "zone") || (tokens[0] == "interface") ||
                   (tokens[0] == "z") || (tokens[0] == "gcd") || (tokens[0] == "owner") ||
                   (tokens[0] == "transaction") || (tokens[0] == "qtable")) {
            ignoreSection(line, file);
        } else if (tokens[0] == "dc") {
            if (tokens.size() > 1) {
                std::cerr << ' ' << tokens[1];
            }
            std::cerr << '\n';
            if (tokens.size() < 2) {
                std::cerr << "invalid dc section header\n";
                continue;
            }
            if (tokens[1] == "bus") {
                cnt = getSectionCount(line);

                bcount = 0;
                if (cnt < 0) {
                    cnt = kBigINT;
                } else if (std::cmp_greater(cnt, dcbusList.size())) {
                    dcbusList.resize(cnt + 2);
                }
                while (bcount < cnt) {
                    nextLine(file, line);
                    index = getLineIndex(line);
                    if (index < 0) {
                    }
                    ++bcount;
                    if (std::cmp_greater(index, dcbusList.size())) {
                        if (index < 100000000) {
                            dcbusList.resize(static_cast<std::vector<DcBus*>::size_type>(
                                                 static_cast<std::int64_t>(2) * index),
                                             nullptr);
                        } else {
                            std::cerr << "Bus index overload " << index << '\n';
                        }
                    }
                    if (dcbusList[index - 1] == nullptr) {
                        dcbusList[index - 1] = new DcBus();
                        dcbusList[index - 1]->set("basepower", base);
                        epcReadDCBus(dcbusList[index - 1], line, base, bri);
                        try {
                            parentObject->add(dcbusList[index - 1]);
                        }
                        catch (const ObjectAddFailure&) {
                            addToParentWithRename(dcbusList[index - 1], parentObject);
                        }
                    } else {
                        std::cerr << "Invalid bus code " << index << '\n';
                    }
                }
            } else if (tokens[1] == "line") {
                processSection(line, file, [&](string_view config) {
                    epcReadDCBranch(parentObject, config, base, dcbusList, bri);
                });
            } else if (tokens[1] == "converter") {
            }
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
Svd
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

namespace {

    double epcReadSolutionParamters(CoreObject* parentObject, string_view line)
    {
        auto tokens = split(line, " ", delimiter_compression::on);
        if (tokens.size() < 2) {
            std::cerr << "invalid solution parameter line\n";
            return 0.0;
        }
        auto val = numeric_conversion<double>(tokens[1], 0.0);
        if ((tokens[0] == "tap") || (tokens[0] == "phas") || (tokens[0] == "area") ||
            (tokens[0] == "svd") || (tokens[0] == "dctap") || (tokens[0] == "gcd") ||
            (tokens[0] == "jump")) {
        } else if (tokens[0] == "toler") {
            parentObject->set("tolerance", val);
        } else if (tokens[0] == "sbase") {
            parentObject->set("basepower", val);
        } else {
            std::cerr << "unknown solution parameter\n";
        }

        return val;
    }

    void epcReadBus(GridBus* bus, string_view line, double /*base*/, const BasicReaderInfo& bri)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 11) {
            std::cerr << "invalid epc bus record\n";
            return;
        }
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
        auto baseVoltage = numeric_conversion<double>(strvec[2], -1.0);
        if (baseVoltage > 0.0) {
            bus->set("basevoltage", baseVoltage);
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
        auto voltageMagnitude = numeric_conversion<double>(strvec[4], 0.0);
        if (voltageMagnitude != 0) {
            bus->set("vtarget", voltageMagnitude);
        }
        voltageMagnitude = numeric_conversion<double>(strvec[5], 0.0);
        auto voltageAngle = numeric_conversion<double>(strvec[6], 0.0);
        if (voltageAngle != 0) {
            bus->set("angle", voltageAngle, deg);
        }
        if (voltageMagnitude != 0) {
            bus->set("voltage", voltageMagnitude);
        }

        // auto area = numeric_conversion<int>(strvec[7], 0);
        auto zone = numeric_conversion<int>(strvec[8], 0);
        if (zone != 0) {
            bus->set("zone", static_cast<double>(zone));
        }
        voltageMagnitude = numeric_conversion<double>(strvec[9], 0.0);
        voltageAngle = numeric_conversion<double>(strvec[10], 0.0);
        if (voltageAngle != 0) {
            bus->set("vmin", voltageAngle);
        }
        if (voltageMagnitude != 0) {
            bus->set("vmax", voltageMagnitude);
        }
    }

    void epcReadDCBus(DcBus* bus, string_view line, double /*base*/, const BasicReaderInfo& bri)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 8) {
            std::cerr << "invalid epc dc bus record\n";
            return;
        }
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
        auto baseVoltage = numeric_conversion<double>(strvec[2], -1.0);
        if (baseVoltage > 0.0) {
            bus->set("basevoltage", baseVoltage);
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
        auto voltageMagnitude = numeric_conversion<double>(strvec[7], 0.0);
        if (voltageMagnitude != 0) {
            bus->set("voltage", voltageMagnitude);
        }

        // auto area = numeric_conversion<int>(strvec[7], 0);
        auto zone = numeric_conversion<int>(strvec[4], 0);
        if (zone != 0) {
            bus->set("zone", static_cast<double>(zone));
        }
    }

    // #load data  [10485]          id   ------------long_id_------------     st      mw      mvar
    // mw_i
    //  mvar_i
    //  mw_z      mvar_z  ar zone  date_in date_out pid N own sdmon nonc ithbus ithflag
    void epcReadLoad(ZipLoad* load, string_view line, double /*base*/)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 12) {
            std::cerr << "invalid epc load record\n";
            return;
        }

        // get the load index and name
        std::string prefix = load->getParent()->getName() + "_Load";
        if (!strvec[3].empty()) {
            prefix += '_' + std::string{strvec[3]};
        }
        load->setName(prefix);
        auto longId = trim(removeQuotes(strvec[4]));
        if (!longId.empty()) {
            load->setDescription(std::string{longId});
        }
        // get the status
        const int status = toIntSimple(strvec[5]);
        if (status == 0) {
            load->disable();
        }
        // skip the area and zone information for now

        // get the constant power part of the load
        auto activePower = numeric_conversion<double>(strvec[6], 0.0);
        auto reactivePower = numeric_conversion<double>(strvec[7], 0.0);
        if (activePower != 0.0) {
            load->set("p", activePower, MW);
        }
        if (reactivePower != 0.0) {
            load->set("q", reactivePower, MVAR);
        }
        // get the constant current part of the load
        activePower = numeric_conversion<double>(strvec[8], 0.0);
        reactivePower = numeric_conversion<double>(strvec[9], 0.0);
        if (activePower != 0.0) {
            load->set("ip", activePower, MW);
        }
        if (reactivePower != 0.0) {
            load->set("iq", reactivePower, MVAR);
        }
        // get the impedance part of the load
        // note:: in PU power units, need to convert to Pu resistance
        activePower = numeric_conversion<double>(strvec[10], 0.0);
        reactivePower = numeric_conversion<double>(strvec[11], 0.0);
        if (activePower != 0.0) {
            load->set("r", activePower, MW);
        }
        if (reactivePower != 0.0) {
            load->set("x", reactivePower, MVAR);
        }
        // ignore the owner field
    }

    // #shunt data  [1988]         id                               ck  se  long_id_     st ar zone
    // pu_mw
    //  pu_mvar
    //  date_in date_out pid N own part1 own part2 own part3 own part4 --num--  --name--  --kv--

    void epcReadFixedShunt(ZipLoad* load, string_view line, double /*base*/)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 15) {
            std::cerr << "invalid epc shunt record\n";
            return;
        }

        // get the load index and name
        std::string prefix = load->getParent()->getName() + "_Shunt";
        if (!strvec[7].empty()) {
            prefix += '_' + std::string{trim(strvec[7])};
        }

        auto longId = trim(removeQuotes(strvec[4]));
        if (!longId.empty()) {
            load->setDescription(std::string{longId});
        }

        load->setName(prefix);

        // get the status
        const int status = toIntSimple(strvec[10]);
        if (status == 0) {
            load->disable();
        }
        // skip the area and zone information for now

        // get the constant power part of the load
        auto activePower = numeric_conversion<double>(strvec[13], 0.0);
        auto reactivePower = numeric_conversion<double>(strvec[14], 0.0);
        if (activePower != 0.0) {
            load->set("yp", activePower, puMW);
        }
        if (reactivePower != 0.0) {
            load->set("yq", -reactivePower, puMW);
        }
    }

    // #Svd data[1253]            id  ------------long_id_------------  st ty --no-- - reg_name
    //  ar zone      g      b  min_c  max_c  vband   bmin   bmax  date_in date_out pid N
    //  own part1 own part2 own part3 own part4
    void epcReadSwitchShunt(loads::Svd* load, string_view line, double /*base*/)
    {
        auto strvec = splitlineBracket(line, " ", default_bracket_chars, delimiter_compression::on);
        const auto vectorSize = strvec.size();
        if (vectorSize < 11) {
            std::cerr << "invalid epc svd record\n";
            return;
        }
        // get the load index and name
        const std::string prefix = load->getParent()->getName() + "_svd";

        auto longId = trim(removeQuotes(strvec[1]));
        if (!longId.empty()) {
            load->setDescription(std::string{longId});
        }

        load->setName(prefix);

        size_t offset = 2;
        while ((offset < vectorSize) && (strvec[offset] != ":")) {
            ++offset;
        }
        if ((offset >= vectorSize) || (offset + 8 >= vectorSize)) {
            std::cerr << "invalid epc svd field layout\n";
            return;
        }
        // get the status
        const int status = toIntSimple(strvec[offset + 1]);
        if (status == 0) {
            load->disable();
        }
        // skip the area and zone information for now

        auto cbus = numeric_conversion<int>(strvec[offset + 3], -1);
        GridBus* rbus = nullptr;
        if (cbus <= 0) {
            rbus = static_cast<GridBus*>(load->getParent());
        } else {
            rbus = static_cast<GridBus*>(
                load->getRoot()->find(std::string("#") + std::to_string(cbus)));
        }
        const int mode = toIntSimple(strvec[offset + 2]);
        double high;
        double low;
        int bsize = 6;
        switch (mode) {
            case 0:
                load->set("mode", "manual");
                bsize = 4;
                break;
            case 1:
                load->set("mode", "stepped");
                // ld->set("vmax", high);
                // ld->set("vmin", low);
                if (rbus != nullptr) {
                    load->setControlBus(rbus);
                }

                break;
            case 2:
                bsize = 4;
                load->set("mode", "cont");
                //    ld->set("vmax", high);
                //    ld->set("vmin", low);
                if (rbus != nullptr) {
                    load->setControlBus(rbus);
                }
                break;
            case 3:
                load->set("mode", "stepped");
                load->set("control", "reactive");
                //    ld->set("qmax", high);
                //    ld->set("qmin", low);
                if (rbus != nullptr) {
                    load->setControlBus(rbus);
                }
                break;
            case 4:
                load->set("mode", "stepped");
                load->set("control", "reactive");
                high = numeric_conversion<double>(strvec[vectorSize - 5], 0.0);
                low = numeric_conversion<double>(strvec[vectorSize - 6], 0.0);
                load->set("qmax", high);
                load->set("qmin", low);
                if (rbus != nullptr) {
                    load->setControlBus(rbus);
                }
                // TODO(phlpt): Handle the unusual PT load target object condition.
                break;
            case 5:
            case 6:
                load->set("mode", "stepped");
                load->set("control", "reactive");
                //    ld->set("qmax", high);
                //    ld->set("qmin", low);
                if (rbus != nullptr) {
                    load->setControlBus(rbus);
                }
                // TODO(phlpt): Handle the unusual PT load target object condition.
                break;
            default:
                load->set("mode", "manual");
                break;
        }
        // load the switched shunt blocks

        for (size_t kk = offset + 26; kk < vectorSize - bsize; kk += 2) {
            auto cnt = numeric_conversion<int>(strvec[kk], 0);
            auto block = numeric_conversion<double>(strvec[kk + 1], 0.0);
            if ((cnt > 0) && (block != 0.0)) {
                load->addBlock(cnt, -block, pu);
            } else {
                break;
            }
        }
        // set the initial value
        auto initVal = numeric_conversion<double>(strvec[offset + 8], 0.0);

        load->set("yq", -initVal, pu);
    }
    // #generator data  [XXX]    id   ------------long_id_------------    st ---no--     reg_name
    // prf qrf
    //  ar
    //  zone   pgen   pmax   pmin   qgen   qmax   qmin   mbase   cmp_r cmp_x gen_r gen_x hbus tbus
    //  date_in date_out pid N
    // #-rtran -xtran -gtap- ow1 part1 ow2 part2 ow3 part3 ow4 part4 ow5 part5 ow6 part6 ow7 part7
    // ow8
    //  part8 gov agc
    //  disp basld air turb qtab pmax2 sdmon

    void epcReadGen(Generator* gen, string_view line, double /*base*/)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 24) {
            std::cerr << "invalid epc generator record\n";
            return;
        }

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
        const int status = toIntSimple(strvec[5]);
        if (status == 0) {
            gen->disable();
        }

        // get the power generation
        auto activePower = numeric_conversion<double>(strvec[13], 0.0);
        auto reactivePower = numeric_conversion<double>(strvec[16], 0.0);
        if (activePower != 0.0) {
            gen->set("p", activePower, MW);
        }
        if (reactivePower != 0.0) {
            gen->set("q", reactivePower, MVAR);
        }
        // get the Pmax and Pmin
        activePower = numeric_conversion<double>(strvec[14], 0.0);
        reactivePower = numeric_conversion<double>(strvec[15], 0.0);
        if (activePower != 0.0) {
            gen->set("pmax", activePower, MW);
        }
        if (reactivePower != 0.0) {
            gen->set("pmin", reactivePower, MW);
        }
        // get the Qmax and Qmin
        activePower = numeric_conversion<double>(strvec[17], 0.0);
        reactivePower = numeric_conversion<double>(strvec[18], 0.0);
        if (activePower != 0.0) {
            gen->set("qmax", activePower, MVAR);
        }
        if (reactivePower != 0.0) {
            gen->set("qmin", reactivePower, MVAR);
        }
        // get the machine base
        auto machineBase = numeric_conversion<double>(strvec[19], 0.0);
        gen->set("mbase", machineBase);

        machineBase = numeric_conversion<double>(strvec[22], 0.0);
        gen->set("rs", machineBase);

        machineBase = numeric_conversion<double>(strvec[23], 0.0);
        gen->set("xs", machineBase);

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
        const auto circuitId = trim(svec[7]);
        if (!circuitId.empty() && (circuitId != "1")) {
            temp2.push_back('_');
            temp2.push_back(circuitId[0]);
        }
        return temp2;
    }

    // #branch data[17003]                                ck  se------------long_id_------------st
    // resist
    //  react
    //  charge   rate1  rate2  rate3  rate4 aloss  lngth #ar zone trangi tap_f tap_t  date_in
    //  date_out pid N ty  rate5 rate6  rate7  rate8 ow1 part1 ow2 part2 ow3 part3 ow4 part4 ow5
    //  part5 ow6 part6 ow7 part7 ow8 part8 ohm sdmon
    // #
    void epcReadBranch(CoreObject* parentObject,
                       string_view line,
                       double base,
                       std::vector<GridBus*>& busList,
                       const BasicReaderInfo& bri)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 19) {
            std::cerr << "invalid epc branch record\n";
            return;
        }

        // get the name of the from bus

        auto ind1 = numeric_conversion<int>(strvec[0], 0);

        auto ind2 = numeric_conversion<int>(strvec[3], 0);

        GridBus* bus1 = busList[ind1 - 1];
        GridBus* bus2 = busList[ind2 - 1];

        // check the circuit identifier
        auto name = generateLineName(strvec, bri.prefix);
        auto* lnk = new AcLine(name);
        auto longId = trim(removeQuotes(strvec[8]));
        if (!longId.empty()) {
            lnk->setDescription(std::string{longId});
        }

        // set the base power to that used this model
        lnk->set("basepower", base);
        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);

        addToParentWithRename(lnk, parentObject);
        // get the branch parameters
        const int status = toIntSimple(strvec[9]);
        if (status == 0) {
            lnk->disable();
        }

        auto resistance = numeric_conversion<double>(strvec[10], 0.0);
        auto reactance = numeric_conversion<double>(strvec[11], 0.0);

        lnk->set("r", resistance);
        lnk->set("x", reactance);

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

    // #dc line data[0]                                  ck------------long_id_------------st ar
    // zone
    //  resist   react
    //  capac   rate1  rate2  rate3  rate4  len  aloss    date_in date_out PID N  rate5  rate6 rate7
    //  rate8 #len-- - loss - date_in date_out pid N  rate5  rate6  rate7  rate8 ow1 part1 ow2 part2
    //  ow3 part3 ow4 part4 ow5 part5 ow6 part6 ow7 part7 ow8 part8

    void epcReadDCBranch(CoreObject* parentObject,
                         string_view line,
                         double base,
                         std::vector<DcBus*>& dcbusList,
                         const BasicReaderInfo& bri)
    {
        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 19) {
            std::cerr << "invalid epc dc branch record\n";
            return;
        }

        // get the name of the from bus

        auto ind1 = numeric_conversion<int>(strvec[0], 0);

        auto ind2 = numeric_conversion<int>(strvec[3], 0);

        DcBus* bus1 = dcbusList[ind1 - 1];
        DcBus* bus2 = dcbusList[ind2 - 1];

        // check the circuit identifier
        auto name = generateLineName(strvec, bri.prefix);
        auto* lnk = new links::DcLink(name);
        auto longId = trim(removeQuotes(strvec[7]));
        if (!longId.empty()) {
            lnk->setDescription(std::string{longId});
        }

        // set the base power to that used this model
        lnk->set("basepower", base);
        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);

        addToParentWithRename(lnk, parentObject);
        // get the branch parameters
        const int status = toIntSimple(strvec[8]);
        if (status == 0) {
            lnk->disable();
        }

        auto resistance = numeric_conversion<double>(strvec[11], 0.0);
        auto reactance = numeric_conversion<double>(strvec[12], 0.0);

        lnk->set("r", resistance);
        lnk->set("x", reactance);

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
    void epcReadTX(CoreObject* parentObject,
                   string_view line,
                   double base,
                   std::vector<GridBus*>& busList,
                   const BasicReaderInfo& bri)
    {
        Link* lnk;
        int code;
        double val;
        int status;
        int cbus;

        auto strvec =
            splitlineBracket(line, " :", default_bracket_chars, delimiter_compression::on);
        if (strvec.size() < 46) {
            std::cerr << "invalid epc transformer record\n";
            return;
        }
        // get the name of the from bus

        auto ind1 = numeric_conversion<int>(strvec[0], 0);

        auto ind2 = numeric_conversion<int>(strvec[3], 0);

        GridBus* bus1 = busList[ind1 - 1];
        GridBus* bus2 = busList[ind2 - 1];

        // check the circuit identifier

        auto name = generateLineName(strvec, (bri.prefix.empty()) ? "TX_" : (bri.prefix + "_TX_"));
        code = numeric_conversion<int>(strvec[9], 1);
        switch (code) {
            case 1:
            case 11:
                code = 1;
                lnk = new AcLine(name);
                // lnk->set ("type", "transformer");
                break;
            case 2:
            case 12:
                code = 2;
                lnk = new links::AdjustableTransformer(name);
                lnk->set("mode", "voltage");
                break;
            case 3:
            case 13:
                code = 3;
                lnk = new links::AdjustableTransformer(name);
                lnk->set("mode", "mvar");
                break;
            case 4:
            case 14:
                code = 4;
                lnk = new links::AdjustableTransformer(name);
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

        auto longId = trim(removeQuotes(strvec[7]));
        if (!longId.empty()) {
            lnk->setDescription(std::string{longId});
        }

        addToParentWithRename(lnk, parentObject);
        // get the branch parameters
        status = toIntSimple(strvec[9]);
        if (status == 0) {
            lnk->disable();
        }

        double tbase = base;
        tbase = numeric_conversion<double>(strvec[22], 0.0);
        // primary and secondary winding resistance
        auto resistance = numeric_conversion<double>(strvec[23], 0.0);
        auto reactance = numeric_conversion<double>(strvec[24], 0.0);

        lnk->set("r", resistance * tbase / base);
        lnk->set("x", reactance * tbase / base);

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
                static_cast<links::AdjustableTransformer*>(lnk)->setControlBus(busList[cbus - 1]);
            }
            resistance = numeric_conversion<double>(strvec[40], 0.0);
            reactance = numeric_conversion<double>(strvec[41], 0.0);
            if (code == 4) {
                lnk->set("maxtapangle", resistance, deg);
                lnk->set("mintapangle", reactance, deg);
            } else {
                lnk->set("maxtap", resistance);
                lnk->set("mintap", reactance);
            }
            resistance = numeric_conversion<double>(strvec[42], 0.0);
            reactance = numeric_conversion<double>(strvec[43], 0.0);
            if (code == 4) {
                lnk->set("pmax", resistance, MW);
                lnk->set("pmin", reactance, MW);
            } else if (code == 3) {
                lnk->set("qmax", resistance, MVAR);
                lnk->set("qmin", reactance, MVAR);
            } else {
                lnk->set("vmax", resistance);
                lnk->set("vmin", reactance);
            }
            resistance = numeric_conversion<double>(strvec[44], 0.0);
            if (code == 4) {
                lnk->set("stepsize", resistance, deg);
            } else {
                lnk->set("stepsize", resistance);
            }
        }
    }

}  // namespace
}  // namespace griddyn
