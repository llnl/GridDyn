/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/Generator.h"
#include "griddyn/links/acLine.h"
#include "griddyn/links/adjustableTransformer.h"
#include "griddyn/loads/zipLoad.h"
#include "griddyn/primary/acBus.h"
#include "readerHelper.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::stringOps::removeQuotes;
using gmlc::utilities::stringOps::trim;
using gmlc::utilities::stringOps::trimString;
using units::MW;
using units::deg;

void pspReadBus(gridBus* bus, const std::string& line, double base, const basicReaderInfo& bri);
void pspReadBranch(coreObject* parentObject,
                   const std::string& line,
                   const std::string& line2,
                   double base,
                   std::vector<gridBus*> busList,
                   const basicReaderInfo& bri);

/*
The PECO PSAP File Format is fully described in the _PJM Power System
Analysis Package Use's Guide_, available from the Philadelphia
Electric Company. The following is a rough description of the
most important parts of the format.

A PSAP data file is divided into sections by code cards. The code is
in the first three columns. There are something like 60 codes, of
which only four are described in this document.

The 1 code indicates that the next card is the case title. Only one
title is allowed per case.

The 4 card indicates that line data follows. The line data ends with
a 9999 card.

The 5 card indicates that bus data follows. The bus data ends with
a 9999 card.

The 15 card indicates that area interchange data follows. The data ends with
a 9999 card.
*/
void loadPSP(coreObject* parentObject, const std::string& fileName, const basicReaderInfo& bri)
{
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string line2;  // line 2 storage for transformers
    std::string temp;  // temporary storage for substrings
    bool nobus = true;
    std::vector<gridBus*> busList;
    int index;
    const double base = 100;
    int code;
    // loop over the sections
    while (std::getline(file, line)) {
        temp = line.substr(0, 3);
        gmlc::utilities::stringOps::trimString(temp);
        if (temp.empty()) {
            continue;
        }

        try {
            code = std::stoi(temp);
        }
        catch (std::invalid_argument&) {
            continue;
        }
        switch (code) {
            case 1:
                std::getline(file, line);
                if (bri.prefix.empty()) {
                    parentObject->setName(line);
                }
                break;
            case 5: {
                bool morebus = true;
                while (morebus) {
                    if (std::getline(file, line)) {
                        temp = line.substr(0, 4);
                        if (temp == "9999") {
                            morebus = false;
                            continue;
                        }
                        // because we can read the line section before the bus section and that is
                        // confusing so we just make sure we read the bus section first
                        if (!nobus) {
                            continue;
                        }
                        if (temp.length() < 4) {
                            continue;
                        }
                        // trimString(temp);
                        index = std::stoi(temp);
                        if (static_cast<size_t>(index) >= busList.size()) {
                            busList.resize(2 * index + 1, nullptr);
                        }
                        if (busList[index] == nullptr) {
                            busList[index] = new acBus();
                            pspReadBus(busList[index], line, base, bri);
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
                        morebus = false;
                    }
                }
                if (nobus) {
                    nobus = false;
                    file.seekg(0);  // reset the file now
                }
            } break;
            case 4: {
                bool morebranch = true;
                while (morebranch) {
                    if (std::getline(file, line)) {
                        temp = line.substr(0, 4);
                        if (temp == "9999") {
                            morebranch = false;
                            continue;
                        }
                        if (nobus) {
                            continue;
                        }
                        if (temp.length() < 4) {
                            continue;
                        }
                        if (line[6] == 'C') {
                            std::getline(file, line2);
                        } else {
                            line2 = "";
                        }
                        pspReadBranch(parentObject, line, line2, base, busList, bri);
                    } else {
                        morebranch = false;
                    }
                }
            } break;
            case 15: {
                bool morebranch = true;
                while (morebranch) {
                    if (std::getline(file, line)) {
                        temp = line.substr(0, 4);
                        if (temp == "9999") {
                            morebranch = false;
                            continue;
                        }
                    } else {
                        morebranch = false;
                    }
                }
            } break;
        }
    }
    file.close();
}

/**********************************************************
Read a line from a PSP file corresponding to a bus specification
**********************************************************/

/*
Bus Cards (Code 5 cards)
========================

1-4     Bus number
6       Change code (blank in 5 section)
7       Continue code (blank in 5 section)
8       Regulated bus code:
Blank - load (PQ) bus
1     - gen (PV) bus
2     - swing (V-Theta) bus
10-21   Name
23-26   Bus voltage (control setpoint or solved value).
Three default decimal places.
27-30   Bus angle
31-35   Generation MW
36-40   Generation MVAR (from solution)
41-45   Generation MVAR low limit
46-50   Generation MVAR high limit
51-55   Bus at which generation controls voltage
56-60   Load MW
61-65   Load MVAR
66-70   Shunt MVAR. Reactors are minus.
71-72   Load flow area. (Used for area interchange and losses).

*/

void pspReadBus(gridBus* bus, const std::string& line, double base, const basicReaderInfo& bri)
{
    std::string temp, temp2;
    Load* ld = nullptr;
    Generator* gen = nullptr;
    int code;
    double P, Q;
    double val;
    temp = line.substr(9, 12);
    temp = removeQuotes(temp);
    if (bri.prefix.empty()) {
        if (temp.empty()) {
            temp = line.substr(0, 4);
            gmlc::utilities::stringOps::trimString(temp);
            temp = "BUS_" + temp;
        }
    } else {
        if (temp.empty()) {
            temp = line.substr(0, 4);
            gmlc::utilities::stringOps::trimString(temp);
            temp = bri.prefix + "BUS_" + temp;
        } else {
            temp = bri.prefix + '_' + temp;
        }
    }

    bus->setName(temp);  // set the name

    // get the localBaseVoltage
    temp = line.substr(18, 3);

    val = numeric_conversion<double>(temp, 0.0);

    if (val > 0.0) {
        bus->set("basevoltage", val);
    }
    // voltage and angle common to all bus types
    // get the actual voltage
    temp = line.substr(22, 4);
    val = numeric_conversion<double>(temp, 0.0);
    if (val > 0.0) {
        bus->set("voltage", val / 1000);
    }
    // get the angle
    temp = line.substr(26, 4);
    val = numeric_conversion<double>(temp, 0.0);
    if (val != 0.0) {
        bus->set("angle", val / 180 * kPI);
    }

    // get the bus type
    temp = line.substr(7, 1);
    code = (temp[0] == ' ') ? 0 : numeric_conversion<int>(temp, 0);
    switch (code) {
        case 0:  // PQ
            bus->set("type", "pq");

            break;
        case 1:  // pv bus
            bus->set("type", "pv");
            // get the Qmax and Qmin
            temp = line.substr(40, 5);
            P = numeric_conversion<double>(temp, 0.0);
            temp = line.substr(45, 5);
            Q = numeric_conversion<double>(temp, 0.0);
            if (P != 0.0) {
                bus->set("qmin", P / base);
            }
            if (Q != 0.0) {
                bus->set("qmax", Q / base);
            }
            // get the desired voltage
            temp = line.substr(22, 4);
            val = numeric_conversion<double>(temp, 0.0);
            bus->set("vtarget", val / 1000);
            break;
        case 2:  // swing bus
            bus->set("type", "slk");
            // get the desired voltage
            temp = line.substr(22, 4);
            val = numeric_conversion<double>(temp, 0.0);
            bus->set("vtarget", val / 1000);
            temp = line.substr(26, 4);
            val = numeric_conversion<double>(temp, 0.0);
            if (val != 0) {
                bus->set("atarget", val, deg);
            }
            break;
    }
    // load section
    temp = line.substr(55, 5);
    P = numeric_conversion<double>(temp, 0.0);
    temp = line.substr(60, 5);
    Q = numeric_conversion<double>(temp, 0.0);

    if ((P != 0) || (Q != 0)) {
        ld = new zipLoad(P / base, Q / base);
        bus->add(ld);
    }
    // get the shunt impedance
    temp = trim(line.substr(65, 5));
    Q = numeric_conversion<double>(temp, 0.0);
    if (Q != 0.0) {
        if (ld == nullptr) {
            ld = new zipLoad();
            bus->add(ld);
        }
        ld->set("yq", -Q / base);
    }
    // get the generation
    temp = trim(line.substr(30, 5));
    P = numeric_conversion<double>(temp, 0.0);
    temp = trim(line.substr(35, 5));
    Q = numeric_conversion<double>(temp, 0.0);

    if ((P != 0.0) || (Q != 0.0)) {
        gen = new Generator();
        bus->add(gen);
        gen->set("p", P / base);
        gen->set("q", Q / base);
        // get the Qmax and Qmin
        temp = line.substr(40, 5);
        P = numeric_conversion<double>(temp, 0.0);
        temp = line.substr(45, 5);
        Q = numeric_conversion<double>(temp, 0.0);
        if (P != 0.0) {
            gen->set("qmin", P / base);
        }
        if (Q != 0.0) {
            gen->set("qmax", Q / base);
        }
    } else if (bus->getType() != gridBus::busType::PQ) {
        temp = line.substr(40, 5);
        P = numeric_conversion<double>(temp, 0.0);
        temp = line.substr(45, 5);
        Q = numeric_conversion<double>(temp, 0.0);
        if ((P != 0.0) || (Q != 0.0)) {
            gen = new Generator();
            bus->add(gen);
            if (P != 0.0) {
                gen->set("qmin", P / base);
            }
            if (Q != 0.0) {
                gen->set("qmax", Q / base);
            }
        }
    }
}
/*
Line Data Card (Code 4 cards)
=============================

Cols    Data
1-4     From bus number
6       Change code (blank in 4 section)
7       'C' if second card present for same line. Used for transformers.
9-12    To bus number
14      Circuit number (blank in 4 section)
16      'T' or 'F' - Load flow area of bus at this end of line gets losses.
18-23   Line resistance in percent of base. (NOT per unit.)
(percent = 100 x per unit) Two default decimal places.
24-29   Line reactance, in percent. Two default decimal places.
30-35   Line charging MVAR (total). Three default decimal places.
36-40   Transformer tap (per unit turns ratio). Three default decimal
places, 1000 = 1.000.
41-45   Min tap, for OLTC. Three default decimal places.
46-50   Max tap, for OLTC. Three default decimal places.
51-55   Phase shift angle, for OL phase shifter. Two default decimal places.
56-60   Remote voltage control bus number. Negative if lower tap increases
voltage of this bus.
61-64   Normal MVA rating
65-68   Emergency MVA rating
69-72   MVA Base. Default value 100 MVA if blank.

Second Line Card (follows 'C' in first card)
============================================

1-17    Same as first card, except no 'C'. Can be left blank.
35-40   Desired MVAR flow or Min voltage setpoint for OLTC.
41-45   Min phase shifter degrees. Two default decimal places.
46-50   Max phase shifter degrees. Two default decimal places.
51-55   Desired MW flow for phase shifter.
57-60   Controlled line from bus.
62-65   Controlled line to bus.
67-70   Available taps (number of taps)
71-75   Maximum voltage setpoint. Three default decimal places.

*/

void pspReadBranch(coreObject* parentObject,
                   const std::string& line,
                   const std::string& line2,
                   double base,
                   std::vector<gridBus*> busList,
                   const basicReaderInfo& bri)
{
    gridBus *bus1, *bus2;
    Link* lnk;
    int ind1, ind2;
    double val;
    bool istransformer = false;
    std::string temp = line.substr(0, 4);
    ind1 = std::stoi(temp);
    std::string temp2;
    if (!bri.prefix.empty()) {
        temp2 = bri.prefix + '_' + temp + "_to_";
    } else {
        temp2 = temp + "_to_";
    }

    temp = line.substr(8, 4);
    ind2 = std::stoi(temp);

    temp2 = temp2 + temp;
    bus1 = busList[ind1];
    bus2 = busList[ind2];
    if (line[6] == 'C') {
        istransformer = true;
        temp = line2.substr(66, 4);
        gmlc::utilities::stringOps::trimString(temp);
        if (temp.empty()) {
            lnk = new acLine();
            // lnk->set ("type", "transformer");
        } else {
            lnk = new links::adjustableTransformer();

            int numTaps = std::stoi(temp);
            lnk->set("steps", static_cast<double>(numTaps));
            /*TODO add the controls for an adjustable transformer*/
        }
    } else {
        lnk = new acLine();
    }
    lnk->updateBus(bus1, 1);
    lnk->updateBus(bus2, 2);
    // get the circuit number
    if (line[13] != ' ') {
        if (line[13] != '1') {
            temp2.push_back('_');
            temp2.push_back(line[13]);
        }
    }
    lnk->setName(temp2);
    addToParentRename(lnk, parentObject);

    // skip the load flow area and loss zone and circuit for now

    // get the branch type
    temp = trim(line.substr(6, 1));

    // get the branch impedance
    temp = line.substr(17, 6);
    auto R = numeric_conversion<double>(temp, 0.0);
    temp = line.substr(23, 6);
    auto X = numeric_conversion<double>(temp, 0.0);

    lnk->set("r", R / 100.0);
    lnk->set("x", X / 100.0);
    // get line capacitance
    temp = line.substr(29, 6);
    val = numeric_conversion<double>(temp, 0.0);
    lnk->set("b", val / base);

    // turns ratio
    if (istransformer) {
        temp = line.substr(35, 5);
        val = numeric_conversion<double>(temp, 0.0);
        if (val > 0.0) {
            lnk->set("tap", val / 1000.0);
        }
        // tapAngle
        val = numeric_conversion(trim(line.substr(50, 5)), 0.0);
        if (val != 0.0) {
            lnk->set("tapangle", val / 1000.0);
        }
    }
}

}  // namespace griddyn
