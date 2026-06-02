/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ObjectFactoryTemplates.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/ZipLoad.h"
#include "readerHelper.h"
#include <compare>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::stringOps::removeQuotes;
using gmlc::utilities::stringOps::trim;
using units::MVAR;
using units::MW;
namespace {
    void ptiReadBus(GridBus* bus, const std::string& line, BasicReaderInfo& opt);
    void ptiReadLoad(GridLoad* load, const std::string& line, BasicReaderInfo& opt);
    void ptiReadFixedShunt(GridLoad* load, const std::string& line, BasicReaderInfo& opt);
    void ptiReadGen(Generator* gen, const std::string& line, BasicReaderInfo& opt);
    void ptiReadBranch(CoreObject* parentObject,
                       const std::string& line,
                       std::vector<GridBus*>& busList,
                       BasicReaderInfo& opt);
    int ptiReadTX(CoreObject* parentObject,
                  stringVec& txlines,
                  std::vector<GridBus*>& busList,
                  BasicReaderInfo& opt);
}  // namespace

// static variables with the factories
// get the basic busFactory
static TypeFactory<GridBus>* gBusfactory = nullptr;

// get the basic load Factory
static TypeFactory<GridLoad>* gLdfactory = nullptr;
// get the basic Link Factory
static TypeFactory<Link>* gLinkfactory = nullptr;
// get the basic Generator Factory
static TypeFactory<Generator>* gGenfactory = nullptr;

void loadPti(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string temp1;  // temporary storage for substrings
    std::vector<GridBus*> busList;
    GridLoad* load;
    Generator* gen;
    index_t index;
    size_t pos;
    BasicReaderInfo readerOptionsCopy(readerOptions);
    auto& opt = readerOptionsCopy;

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

    if (std::getline(file, line)) {
        const auto headerFields = gmlc::utilities::stringOps::splitline(line);
        if (headerFields.size() > 1) {
            readerOptionsCopy.base =
                numeric_conversion<double>(headerFields[1], readerOptionsCopy.base);
            parentObject->set("systemBasePower", readerOptionsCopy.base);
        }
        if (headerFields.size() > 5) {
            readerOptionsCopy.basefreq =
                numeric_conversion<double>(headerFields[5], readerOptionsCopy.basefreq);
        }
        // temp1=line.substr(45,27);
        // parentObject->setName(temp1);
    }
    if (std::getline(file, line)) {
        pos = line.find_first_of(',');
        temp1 = trim(line.substr(0, pos));
        parentObject->setName(temp1);
    }
    // get the second comment line and ignore it
    std::getline(file, line);
    // get the bus data section

    bool moreData = true;
    while (moreData) {
        if (std::getline(file, line)) {
            if (line[0] == '0') {
                moreData = false;
                continue;
            }
            // get the index
            pos = line.find_first_of(',');
            temp1 = trim(line.substr(0, pos));
            index = gmlc::utilities::numeric_conversion<index_t>(temp1, 0);

            if (std::cmp_greater_equal(index, busList.size())) {
                if (index < 100000000) {
                    busList.resize((2 * index) + 1, nullptr);
                } else {
                    std::cerr << "Bus index overload " << index << '\n';
                }
            }
            if (busList[index] == nullptr) {
                busList[index] = gBusfactory->makeTypeObject();
                busList[index]->setUserID(index);
                ptiReadBus(busList[index], line, readerOptionsCopy);
                try {
                    parentObject->add(busList[index]);
                }
                catch (const ObjectAddFailure&) {
                    addToParentWithRename(busList[index], parentObject);
                }
            } else {
                std::cerr << "Invalid bus code " << index << '\n';
            }
        } else {
            moreData = false;
        }
    }
    moreData = true;
    // get the load data section data
    while (moreData) {
        if (std::getline(file, line)) {
            if (line[0] == '0') {
                moreData = false;
                continue;
            }
            // get the bus index
            pos = line.find_first_of(',');
            temp1 = trim(line.substr(0, pos));
            index = gmlc::utilities::numeric_conversion<index_t>(temp1, 0);

            if (std::cmp_greater_equal(index, busList.size())) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            }
            if (busList[index] == nullptr) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            } else {
                load = gLdfactory->makeTypeObject();
                busList[index]->add(load);
                ptiReadLoad(load, line, readerOptionsCopy);
            }
        } else {
            moreData = false;
        }
    }
    // get the Fixed Shunt data
    moreData = true;
    while (moreData) {
        if (std::getline(file, line)) {
            if (line[0] == '0') {
                moreData = false;
                continue;
            }
            // get the bus index
            pos = line.find_first_of(',');
            temp1 = trim(line.substr(0, pos));
            index = gmlc::utilities::numeric_conversion<index_t>(temp1, 0);

            if (std::cmp_greater_equal(index, busList.size())) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            }
            if (busList[index] == nullptr) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            } else {
                load = gLdfactory->makeTypeObject();
                busList[index]->add(load);
                ptiReadFixedShunt(load, line, readerOptionsCopy);
            }
        } else {
            moreData = false;
        }
    }
    // get the generator Data
    moreData = true;
    while (moreData) {
        if (std::getline(file, line)) {
            if (line[0] == '0') {
                moreData = false;
                continue;
            }
            // get the bus index
            pos = line.find_first_of(',');
            temp1 = trim(line.substr(0, pos));
            index = gmlc::utilities::numeric_conversion<index_t>(temp1, 0);

            if (std::cmp_greater_equal(index, busList.size())) {
                std::cerr << "Invalid bus number for generator " << index << '\n';
            }
            if (busList[index] == nullptr) {
                std::cerr << "Invalid bus number for generator " << index << '\n';
            } else {
                gen = gGenfactory->makeTypeObject();
                busList[index]->add(gen);
                ptiReadGen(gen, line, opt);
            }
        } else {
            moreData = false;
        }
    }
    // get the transmission line data
    moreData = true;
    while (moreData) {
        if (std::getline(file, line)) {
            if (line[0] == '0') {
                moreData = false;
                continue;
            }
            ptiReadBranch(parentObject, line, busList, opt);
        } else {
            moreData = false;
        }
    }
    // read the transformer data
    moreData = true;
    stringVec txlines;
    txlines.resize(5);
    int tline = 5;
    while (moreData) {
        if (std::getline(file, line)) {
            if (line[0] == '0') {
                moreData = false;
                continue;
            }
            if (tline == 5) {
                txlines[0] = line;
                std::getline(file, txlines[1]);
                std::getline(file, txlines[2]);
                std::getline(file, txlines[3]);
                std::getline(file, txlines[4]);
            } else {
                if (txlines[4][0] == '0') {
                    moreData = false;
                    continue;
                }
                txlines[0] = txlines[4];
                txlines[1] = line;
                std::getline(file, txlines[2]);
                std::getline(file, txlines[3]);
                std::getline(file, txlines[4]);
            }
            tline = ptiReadTX(parentObject, txlines, busList, opt);
        } else {
            moreData = false;
        }
    }
    file.close();
}

namespace {

    void ptiReadBus(GridBus* bus, const std::string& line, BasicReaderInfo& opt)
    {
        std::string temp;
        std::string temp2;
        double baseVoltage;
        double voltageMagnitude;
        double voltageAngle;
        int type;

        auto strvec = gmlc::utilities::stringOps::splitline(line);
        // get the bus name
        temp = strvec[0];
        gmlc::utilities::stringOps::trimString(temp);
        temp2 = strvec[1];
        // check for quotes on the name
        removeQuotes(temp2);
        if (opt.prefix.empty()) {
            if (temp2.empty())  // 12 spaces is default value which would all get trimmed
            {
                temp2 = "BUS_" + temp;
            }
        } else {
            if (temp2.empty())  // 12 spaces is default value which would all get trimmed
            {
                temp2 = opt.prefix + '_' + temp;
            } else {
                temp2 = opt.prefix + '_' + temp2;
            }
        }
        bus->setName(temp2);

        // get the localBaseVoltage
        baseVoltage = std::stod(strvec[2]);
        if (baseVoltage > 0.0) {
            bus->set("basevoltage", baseVoltage);
        }

        // get the bus type
        if (strvec[3].empty()) {
            type = 1;
        } else {
            type = std::stoi(strvec[3]);
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
                temp = "PQ";
                break;
        }
        bus->set("type", temp);
        // skip the load flow area and loss zone for now
        // skip the owner information
        // get the voltage and angle specifications
        voltageMagnitude = numeric_conversion<double>(strvec[7], 0.0);
        voltageAngle = numeric_conversion<double>(strvec[8], 0.0);
        if (voltageAngle != 0) {
            bus->set("angle", voltageAngle / 180 * kPI);
        }
        if (voltageMagnitude != 0) {
            bus->set("voltage", voltageMagnitude);
        }
    }

    void ptiReadLoad(GridLoad* load, const std::string& line, BasicReaderInfo& /*opt*/)
    {
        std::string temp;
        std::string prefix;
        double pValue;
        double qValue;
        int status;

        auto strvec = gmlc::utilities::stringOps::splitline(line);

        // get the load index and name
        temp = strvec[1];
        gmlc::utilities::stringOps::trimString(temp);
        prefix = load->getParent()->getName() + "_load_" + temp;
        load->setName(prefix);

        // get the status
        status = std::stoi(strvec[2]);
        if (status == 0) {
            load->disable();
        }
        // skip the area and zone information for now

        // get the constant power part of the load
        pValue = numeric_conversion<double>(strvec[5], 0.0);
        qValue = numeric_conversion<double>(strvec[6], 0.0);
        if (pValue != 0.0) {
            load->set("p", pValue, MW);
        }
        if (qValue != 0.0) {
            load->set("q", qValue, MVAR);
        }
        // get the constant current part of the load
        pValue = numeric_conversion<double>(strvec[7], 0.0);
        qValue = numeric_conversion<double>(strvec[8], 0.0);
        if (pValue != 0.0) {
            load->set("ip", pValue, MW);
        }
        if (qValue != 0.0) {
            load->set("iq", qValue, MVAR);
        }
        // get the impedance part of the load
        // note:: in PU power units, need to convert to Pu resistance
        pValue = numeric_conversion<double>(strvec[9], 0.0);
        qValue = numeric_conversion<double>(strvec[10], 0.0);
        if (pValue != 0.0) {
            load->set("r", pValue, MW);
        }
        if (qValue != 0.0) {
            load->set("x", qValue, MVAR);
        }
        // ignore the owner field
    }

    void ptiReadFixedShunt(GridLoad* load, const std::string& line, BasicReaderInfo& /*opt*/)
    {
        std::string temp;
        std::string prefix;
        double pValue;
        double qValue;
        int status;

        auto strvec = gmlc::utilities::stringOps::splitline(line);

        // get the load index and name
        temp = strvec[1];
        gmlc::utilities::stringOps::trimString(temp);
        prefix = load->getParent()->getName() + "_shunt_" + temp;
        load->setName(prefix);

        // get the status
        status = std::stoi(strvec[2]);
        if (status == 0) {
            load->disable();
        }
        // skip the area and zone information for now

        // get the constant power part of the load
        pValue = numeric_conversion<double>(strvec[3], 0.0);
        qValue = numeric_conversion<double>(strvec[4], 0.0);
        if (pValue != 0.0) {
            load->set("yp", pValue, MW);
        }
        if (qValue != 0.0) {
            load->set("yq", -qValue, MVAR);
        }
    }

    void ptiReadGen(Generator* gen, const std::string& line, BasicReaderInfo& /*opt*/)
    {
        int rbus;

        auto strvec = gmlc::utilities::stringOps::splitline(line);

        // get the load index and name
        const std::string temp = trim(strvec[1]);
        const std::string prefix = gen->getParent()->getName() + "_Gen_" + temp;
        gen->setName(prefix);
        // get the status
        auto status = std::stoi(strvec[14]);
        if (status == 0) {
            gen->disable();
        }

        // get the power generation
        auto pValue = numeric_conversion<double>(strvec[2], 0.0);
        auto qValue = numeric_conversion<double>(strvec[3], 0.0);
        if (pValue != 0.0) {
            gen->set("p", pValue, MW);
        }
        if (qValue != 0.0) {
            gen->set("q", qValue, MVAR);
        }
        // get the Qmax and Qmin
        pValue = numeric_conversion<double>(strvec[4], 0.0);
        qValue = numeric_conversion<double>(strvec[5], 0.0);
        if (pValue != 0.0) {
            gen->set("qmax", pValue, MW);
        }
        if (qValue != 0.0) {
            gen->set("qmin", qValue, MVAR);
        }
        auto voltage = numeric_conversion<double>(strvec[6], 0.0);
        if (voltage > 0) {
            gen->set("vset", voltage);
        }
        rbus = numeric_conversion<int>(strvec[7], 0);

        if (rbus != 0) {
            // TODO(phlpt): Handle the remote-controlled bus case.
        }
        // TODO(phlpt): Get the impedance fields and other data.
    }

    void ptiReadBranch(CoreObject* parentObject,
                       const std::string& line,
                       std::vector<GridBus*>& busList,
                       BasicReaderInfo& opt)
    {
        std::string temp;
        std::string temp2;
        GridBus* bus1;
        GridBus* bus2;
        Link* lnk;
        int ind1;
        int ind2;
        double resistance;
        double reactance;
        double val;
        int status;

        auto strvec = gmlc::utilities::stringOps::splitline(line);

        temp = strvec[0];
        ind1 = std::stoi(temp);
        if (opt.prefix.empty()) {
            temp2 = temp + "_to_";
        } else {
            temp2 = opt.prefix + '_' + temp + "_to_";
        }

        temp = strvec[1];
        ind2 = std::stoi(temp);

        temp2 = temp2 + temp;
        bus1 = busList[ind1];
        bus2 = busList[ind2];

        lnk = gLinkfactory->makeTypeObject();
        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);
        lnk->setName(temp2);

        parentObject->add(lnk);

        status = std::stoi(strvec[13]);
        if (status == 0) {
            lnk->disable();
        }

        // skip the load flow area and loss zone and circuit for now

        // get the branch impedance

        resistance = numeric_conversion<double>(strvec[3], 0.0);
        reactance = numeric_conversion<double>(strvec[4], 0.0);

        lnk->set("r", resistance);
        lnk->set("x", reactance);
        // get line capacitance
        val = numeric_conversion<double>(strvec[5], 0.0);
        lnk->set("b", val);

        // TODO(phlpt): Get the other parameters; not critical for power flow.
    }

    int ptiReadTX(CoreObject* parentObject,
                  stringVec& txlines,
                  std::vector<GridBus*>& busList,
                  BasicReaderInfo& opt)
    {
        int tline = 4;
        std::string temp;
        std::string temp2;
        GridBus* bus1;
        GridBus* bus2;
        // GridBus *bus3;
        Link* lnk;
        int code;
        int ind1;
        int ind2;
        int ind3;
        double resistance;
        double reactance;
        double val;
        int status;

        stringVec strvec = gmlc::utilities::stringOps::splitline(txlines[0]);
        stringVec strvec2 = gmlc::utilities::stringOps::splitline(txlines[1]);
        stringVec strvec3 = gmlc::utilities::stringOps::splitline(txlines[2]);
        const stringVec strvec4 = gmlc::utilities::stringOps::splitline(txlines[3]);

        temp = strvec[0];
        ind1 = std::stoi(temp);

        temp = strvec[2];
        ind3 = std::stoi(temp);
        if (ind3 != 0) {
            tline = 5;
            const stringVec strvec5 = gmlc::utilities::stringOps::splitline(txlines[4]);
            // TODO(phlpt): Handle three-way transformers.
            std::cout << "3 winding transformers not supported at this time\n";
            return tline;
        }

        if (opt.prefix.empty()) {
            temp2 = "tx_" + temp + "_to_";
        } else {
            temp2 = opt.prefix + "_tx_" + temp + "_to_";
        }
        temp = strvec[1];
        ind2 = std::stoi(temp);

        temp2 = temp2 + temp;
        bus1 = busList[ind1];
        bus2 = busList[ind2];
        code = std::stoi(strvec3[6]);
        switch (code) {
            case 0:
                lnk = gLinkfactory->makeTypeObject();
                lnk->set("type", "transformer");
                break;
            case 1:
                lnk = new links::AdjustableTransformer();
                lnk->set("mode", "voltage");
                break;
            case 2:
                lnk = new links::AdjustableTransformer();
                lnk->set("mode", "mvar");
                break;
            case 3:
                lnk = new links::AdjustableTransformer();
                lnk->set("mode", "mw");
                break;
            default:
                parentObject->log(parentObject,
                                  PrintLevel::WARNING,
                                  "Unrecognized link code assuming transformer" +
                                      std::to_string(code));
                lnk = gLinkfactory->makeTypeObject();
                lnk->set("type", "transformer");
                break;
        }

        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);
        lnk->setName(temp2);

        parentObject->add(lnk);

        // skip the load flow area and loss zone and circuit for now

        // get the branch impedance

        resistance = numeric_conversion<double>(strvec2[0], 0.0);
        reactance = numeric_conversion<double>(strvec2[1], 0.0);

        lnk->set("r", resistance);
        lnk->set("x", reactance);
        // get line capacitance
        val = numeric_conversion<double>(strvec[5], 0.0);
        lnk->set("b", val);

        status = std::stoi(strvec[11]);
        if (status == 0) {
            lnk->disable();
        } else if (status > 1) {
            // TODO(phlpt): Handle the other conditions for three-way transformers.
        }

        // TODO(phlpt): Get the other parameters; not critical for power flow.

        val = numeric_conversion<double>(strvec3[0], 0.0);
        if (val != 0) {
            lnk->set("tap", val);
        }
        val = numeric_conversion<double>(strvec3[2], 0.0);
        if (val != 0) {
            lnk->set("tapangle", val);
        }
        // now get the stuff for the adjustable transformers
        if (code > 0) {
        }
        return tline;
    }

}  // namespace
}  // namespace griddyn
