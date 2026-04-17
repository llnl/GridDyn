/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "core/objectFactoryTemplates.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/gridBus.h"
#include "griddyn/links/acLine.h"
#include "griddyn/links/adjustableTransformer.h"
#include "griddyn/loads/zipLoad.h"
#include "readerHelper.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::stringOps::removeQuotes;
using gmlc::utilities::stringOps::trim;
using units::deg;
using units::MVAR;
using units::MW;

void ptiReadBus(gridBus* bus, const std::string& line, basicReaderInfo& opt);
void ptiReadLoad(Load* ld, const std::string& line, basicReaderInfo& opt);
void ptiReadFixedShunt(Load* ld, const std::string& line, basicReaderInfo& opt);
void ptiReadGen(Generator* gen, const std::string& line, basicReaderInfo& opt);
void ptiReadBranch(coreObject* parentObject,
                   const std::string& line,
                   std::vector<gridBus*>& busList,
                   basicReaderInfo& opt);
int ptiReadTX(coreObject* parentObject,
              stringVec& txlines,
              std::vector<gridBus*>& busList,
              basicReaderInfo& opt);

// static variables with the factories
// get the basic busFactory
static typeFactory<gridBus>* busfactory = nullptr;

// get the basic load Factory
static typeFactory<Load>* ldfactory = nullptr;
// get the basic Link Factory
static typeFactory<Link>* linkfactory = nullptr;
// get the basic Generator Factory
static typeFactory<Generator>* genfactory = nullptr;

void loadPTI(coreObject* parentObject, const std::string& fileName, const basicReaderInfo& bri)
{
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string temp1;  // temporary storage for substrings
    std::string pref2;  // temp storage to 2nd order prefix.
    std::vector<gridBus*> busList;
    Load* ld;
    Generator* gen;
    index_t index;
    size_t pos;
    basicReaderInfo opt(bri);

    /*load up the factories*/
    if (busfactory == nullptr) {
        // get the basic busFactory
        busfactory = static_cast<decltype(busfactory)>(
            coreObjectFactory::instance()->getFactory("bus")->getFactory(""));

        // get the basic load Factory
        ldfactory = static_cast<decltype(ldfactory)>(
            coreObjectFactory::instance()->getFactory("load")->getFactory(""));

        // get the basic load Factory
        genfactory = static_cast<decltype(genfactory)>(
            coreObjectFactory::instance()->getFactory("generator")->getFactory(""));
        // get the basic link Factory
        linkfactory = static_cast<decltype(linkfactory)>(
            coreObjectFactory::instance()->getFactory("link")->getFactory(""));
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
        auto res = sscanf(line.c_str(), "%*d, %lf,%*d,%*d,%*d,%lf", &(opt.base), &(opt.basefreq));
        if (res > 0) {
            parentObject->set("systemBasePower", opt.base);
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

            if (index >= static_cast<index_t>(busList.size())) {
                if (index < 100000000) {
                    busList.resize(2 * index + 1, nullptr);
                } else {
                    std::cerr << "Bus index overload " << index << '\n';
                }
            }
            if (busList[index] == nullptr) {
                busList[index] = busfactory->makeTypeObject();
                busList[index]->setUserID(index);
                ptiReadBus(busList[index], line, opt);
                try {
                    parentObject->add(busList[index]);
                }
                catch (const objectAddFailure&) {
                    addToParentRename(busList[index], parentObject);
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

            if (index >= static_cast<index_t>(busList.size())) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            }
            if (busList[index] == nullptr) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            } else {
                ld = ldfactory->makeTypeObject();
                busList[index]->add(ld);
                ptiReadLoad(ld, line, opt);
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

            if (index >= static_cast<index_t>(busList.size())) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            }
            if (busList[index] == nullptr) {
                std::cerr << "Invalid bus number for load " << index << '\n';
            } else {
                ld = ldfactory->makeTypeObject();
                busList[index]->add(ld);
                ptiReadFixedShunt(ld, line, opt);
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

            if (index >= static_cast<index_t>(busList.size())) {
                std::cerr << "Invalid bus number for generator " << index << '\n';
            }
            if (busList[index] == nullptr) {
                std::cerr << "Invalid bus number for generator " << index << '\n';
            } else {
                gen = genfactory->makeTypeObject();
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

void ptiReadBus(gridBus* bus, const std::string& line, basicReaderInfo& opt)
{
    std::string temp, temp2;
    double bv;
    double vm;
    double va;
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
    bv = std::stod(strvec[2]);
    if (bv > 0.0) {
        bus->set("basevoltage", bv);
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
    }
    bus->set("type", temp);
    // skip the load flow area and loss zone for now
    // skip the owner information
    // get the voltage and angle specifications
    vm = numeric_conversion<double>(strvec[7], 0.0);
    va = numeric_conversion<double>(strvec[8], 0.0);
    if (va != 0) {
        bus->set("angle", va / 180 * kPI);
    }
    if (vm != 0) {
        bus->set("voltage", vm);
    }
}

void ptiReadLoad(Load* ld, const std::string& line, basicReaderInfo& /*opt*/)
{
    std::string temp;
    std::string prefix;
    double p;
    double q;
    int status;

    auto strvec = gmlc::utilities::stringOps::splitline(line);

    // get the load index and name
    temp = strvec[1];
    gmlc::utilities::stringOps::trimString(temp);
    prefix = ld->getParent()->getName() + "_load_" + temp;
    ld->setName(prefix);

    // get the status
    status = std::stoi(strvec[2]);
    if (status == 0) {
        ld->disable();
    }
    // skip the area and zone information for now

    // get the constant power part of the load
    p = numeric_conversion<double>(strvec[5], 0.0);
    q = numeric_conversion<double>(strvec[6], 0.0);
    if (p != 0.0) {
        ld->set("p", p, MW);
    }
    if (q != 0.0) {
        ld->set("q", q, MVAR);
    }
    // get the constant current part of the load
    p = numeric_conversion<double>(strvec[7], 0.0);
    q = numeric_conversion<double>(strvec[8], 0.0);
    if (p != 0.0) {
        ld->set("ip", p, MW);
    }
    if (q != 0.0) {
        ld->set("iq", q, MVAR);
    }
    // get the impedance part of the load
    // note:: in PU power units, need to convert to Pu resistance
    p = numeric_conversion<double>(strvec[9], 0.0);
    q = numeric_conversion<double>(strvec[10], 0.0);
    if (p != 0.0) {
        ld->set("r", p, MW);
    }
    if (q != 0.0) {
        ld->set("x", q, MVAR);
    }
    // ignore the owner field
}

void ptiReadFixedShunt(Load* ld, const std::string& line, basicReaderInfo& /*opt*/)
{
    std::string temp;
    std::string prefix;
    double p;
    double q;
    int status;

    auto strvec = gmlc::utilities::stringOps::splitline(line);

    // get the load index and name
    temp = strvec[1];
    gmlc::utilities::stringOps::trimString(temp);
    prefix = ld->getParent()->getName() + "_shunt_" + temp;
    ld->setName(prefix);

    // get the status
    status = std::stoi(strvec[2]);
    if (status == 0) {
        ld->disable();
    }
    // skip the area and zone information for now

    // get the constant power part of the load
    p = numeric_conversion<double>(strvec[3], 0.0);
    q = numeric_conversion<double>(strvec[4], 0.0);
    if (p != 0.0) {
        ld->set("yp", p, MW);
    }
    if (q != 0.0) {
        ld->set("yq", -q, MVAR);
    }
}

void ptiReadGen(Generator* gen, const std::string& line, basicReaderInfo& /*opt*/)
{
    int rbus;

    auto strvec = gmlc::utilities::stringOps::splitline(line);

    // get the load index and name
    std::string temp = trim(strvec[1]);
    std::string prefix = gen->getParent()->getName() + "_Gen_" + temp;
    gen->setName(prefix);
    // get the status
    auto status = std::stoi(strvec[14]);
    if (status == 0) {
        gen->disable();
    }

    // get the power generation
    auto p = numeric_conversion<double>(strvec[2], 0.0);
    auto q = numeric_conversion<double>(strvec[3], 0.0);
    if (p != 0.0) {
        gen->set("p", p, MW);
    }
    if (q != 0.0) {
        gen->set("q", q, MVAR);
    }
    // get the Qmax and Qmin
    p = numeric_conversion<double>(strvec[4], 0.0);
    q = numeric_conversion<double>(strvec[5], 0.0);
    if (p != 0.0) {
        gen->set("qmax", p, MW);
    }
    if (q != 0.0) {
        gen->set("qmin", q, MVAR);
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

void ptiReadBranch(coreObject* parentObject,
                   const std::string& line,
                   std::vector<gridBus*>& busList,
                   basicReaderInfo& opt)
{
    std::string temp, temp2;
    gridBus *bus1, *bus2;
    Link* lnk;
    int ind1, ind2;
    double R, X;
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

    lnk = linkfactory->makeTypeObject();
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

    R = numeric_conversion<double>(strvec[3], 0.0);
    X = numeric_conversion<double>(strvec[4], 0.0);

    lnk->set("r", R);
    lnk->set("x", X);
    // get line capacitance
    val = numeric_conversion<double>(strvec[5], 0.0);
    lnk->set("b", val);

    // TODO(phlpt): Get the other parameters; not critical for power flow.
}

int ptiReadTX(coreObject* parentObject,
              stringVec& txlines,
              std::vector<gridBus*>& busList,
              basicReaderInfo& opt)
{
    int tline = 4;
    std::string temp, temp2;
    gridBus *bus1, *bus2;
    // gridBus *bus3;
    Link* lnk;
    int code;
    int ind1, ind2, ind3;
    double R, X;
    double val;
    int status;

    stringVec strvec, strvec2, strvec3, strvec4, strvec5;
    strvec = gmlc::utilities::stringOps::splitline(txlines[0]);

    strvec2 = gmlc::utilities::stringOps::splitline(txlines[1]);
    strvec3 = gmlc::utilities::stringOps::splitline(txlines[2]);
    strvec4 = gmlc::utilities::stringOps::splitline(txlines[3]);

    temp = strvec[0];
    ind1 = std::stoi(temp);

    temp = strvec[2];
    ind3 = std::stoi(temp);
    if (ind3 != 0) {
        tline = 5;
        strvec5 = gmlc::utilities::stringOps::splitline(txlines[4]);
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
            lnk = linkfactory->makeTypeObject();
            lnk->set("type", "transformer");
            break;
        case 1:
            lnk = new links::adjustableTransformer();
            lnk->set("mode", "voltage");
            break;
        case 2:
            lnk = new links::adjustableTransformer();
            lnk->set("mode", "mvar");
            break;
        case 3:
            lnk = new links::adjustableTransformer();
            lnk->set("mode", "mw");
            break;
        default:
            parentObject->log(parentObject,
                              print_level::warning,
                              "Unrecognized link code assuming transformer" + std::to_string(code));
            lnk = linkfactory->makeTypeObject();
            lnk->set("type", "transformer");
            break;
    }

    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);
    lnk->setName(temp2);

    parentObject->add(lnk);

    // skip the load flow area and loss zone and circuit for now

    // get the branch impedance

    R = numeric_conversion<double>(strvec2[0], 0.0);
    X = numeric_conversion<double>(strvec2[1], 0.0);

    lnk->set("r", R);
    lnk->set("x", X);
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

}  // namespace griddyn
