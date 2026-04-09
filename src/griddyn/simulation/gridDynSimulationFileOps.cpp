/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynSimulationFileOps.h"

#include "../gridBus.h"
#include "../gridDynSimulation.h"
#include "../links/acLine.h"
#include "../links/adjustableTransformer.h"
#include "../solvers/solverInterface.h"
#include "contingency.h"
#include "core/coreExceptions.h"

#ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Woverloaded-virtual"
#    include <ticpp/ticpp.h>
#    pragma GCC diagnostic pop
#else
#    include <ticpp/ticpp.h>
#endif

#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "units/units.hpp"
#include "utilities/matrixDataSparse.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::convertToLowerCase;

void savePowerFlow(gridDynSimulation* gds, const std::string& fileName)
{
    std::filesystem::path filePath(fileName);
    if (fileName.empty()) {
        auto powerFlowFile = gds->getString("powerflowfile");
        if (powerFlowFile.empty()) {
            auto sourceFile = gds->getString("sourcefile");
            filePath = std::filesystem::path(sourceFile);
            filePath = std::filesystem::path(filePath.filename().string() + ".csv");
        } else {
            filePath = std::filesystem::path(powerFlowFile);
        }
    }

    std::string ext = convertToLowerCase(filePath.extension().string());

    // get rid of the . on the extension if it has one
    if (ext == ".xml") {
        savePowerFlowXML(gds, fileName);
    } else if (ext == ".csv") {
        savePowerFlowCSV(gds, fileName);
    } else if (ext == ".cdf") {
        savePowerFlowCdf(gds, fileName);
    } else if ((ext == ".txt") || (ext == ".out")) {
        savePowerFlowTXT(gds, fileName);
    } else {
        savePowerFlowBinary(gds, fileName);
    }
}

void savePowerFlowCSV(gridDynSimulation* gds, const std::string& fileName)
{
    FILE* fp = fopen(fileName.c_str(), "w");
    if (fp == nullptr) {
        throw(fileOperationError("unable to open file " + fileName));
    }
    double basePower = gds->get("basepower");
    fprintf(fp, "basepower=%f\n", basePower);
    fprintf(
        fp,
        "\"Area #\",\"Bus #\",\"Bus "
        "name\",\"voltage(pu)\",\"angle(deg)\",\"Pgen(MW)\",\"Qgen(MW)\",\"Pload(MW)\",\"Qload(MW)\","
        "\"Plink(MW)\",\"Qlink(MW)\"\n");
    Area* Area = gds->getArea(0);
    index_t mmm = 0;
    while (Area != nullptr) {
        index_t nn = 0;
        gridBus* bus = Area->getBus(nn);
        while (bus != nullptr) {
            fprintf(fp,
                    "%d, %d,\"%s\", %7.6f, %+8.4f, %7.5f, %7.5f, %7.5f, %7.5f, %7.5f, %7.5f\n",
                    Area->getUserID(),
                    bus->getUserID(),
                    bus->getName().c_str(),
                    bus->getVoltage(),
                    units::convert(bus->getAngle(), units::rad, units::deg),
                    bus->getGenerationReal() * basePower,
                    bus->getGenerationReactive() * basePower,
                    bus->getLoadReal() * basePower,
                    bus->getLoadReactive() * basePower,
                    bus->getLinkReal() * basePower,
                    bus->getLinkReactive() * basePower);

            ++nn;
            bus = Area->getBus(nn);
        }
        ++mmm;
        Area = gds->getArea(mmm);
    }

    index_t nn = 0;
    gridBus* bus = gds->getBus(nn);
    while (bus != nullptr) {
        fprintf(fp,
                "%d, %d, \"%s\", %7.6f, %+8.4f, %7.5f, %7.5f, %7.5f, %7.5f, %7.5f, %7.5f\n",
                1,
                bus->locIndex + 1,
                bus->getName().c_str(),
                bus->getVoltage(),
                units::convert(bus->getAngle(), units::rad, units::deg),
                bus->getGenerationReal() * basePower,
                bus->getGenerationReactive() * basePower,
                bus->getLoadReal() * basePower,
                bus->getLoadReactive() * basePower,
                bus->getLinkReal() * basePower,
                bus->getLinkReactive() * basePower);

        ++nn;
        bus = gds->getBus(nn);
    }
    gds->log(gds, print_level::normal, "saving csv powerflow to " + fileName);
    fclose(fp);
}

void savePowerFlowTXT(gridDynSimulation* gds, const std::string& fileName)
{
    FILE* fp = fopen(fileName.c_str(), "w");
    if (fp == nullptr) {
        throw(fileOperationError("unable to open file " + fileName));
    }
    double basePower = gds->get("basepower");
    fprintf(fp, "%s basepower=%f\n", gds->getName().c_str(), basePower);
    fprintf(fp,
            "Simulation %d buses %d lines\n",
            gds->getInt("totalbuscount"),
            gds->getInt("totallinkcount"));

    fprintf(fp, "===============BUS INFORMATION=====================\n");
    fprintf(
        fp,
        "Area#\tBus#\tBus "
        "name\t\t\t\tvoltage(pu)\tangle(deg)\tPgen(MW)\tQgen(MW)\tPload(MW)\tQload(MW)\tPlink(MW)\tQlink("
        "MW)\n");
    Area* Area = gds->getArea(0);
    gridBus* bus;
    index_t nn = 0;
    index_t mmm = 0;
    while (Area != nullptr) {
        nn = 0;
        bus = Area->getBus(nn);
        while (bus != nullptr) {
            fprintf(
                fp,
                "%d\t\t %d\t\t\"%-20s\"\t\t %7.6f\t %+8.4f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\n",
                Area->getUserID(),
                bus->getUserID(),
                bus->getName().c_str(),
                bus->getVoltage(),
                units::convert(bus->getAngle(), units::rad, units::deg),
                bus->getGenerationReal() * basePower,
                bus->getGenerationReactive() * basePower,
                bus->getLoadReal() * basePower,
                bus->getLoadReactive() * basePower,
                bus->getLinkReal() * basePower,
                bus->getLinkReactive() * basePower);

            ++nn;
            bus = Area->getBus(nn);
        }
        ++mmm;
        Area = gds->getArea(mmm);
    }

    nn = 0;
    bus = gds->getBus(nn);
    while (bus != nullptr) {
        fprintf(
            fp,
            "%d\t\t %d\t\t \"%-20s\"\t %7.6f\t %8.4f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\n",
            1,
            bus->getUserID(),
            bus->getName().c_str(),
            bus->getVoltage(),
            units::convert(bus->getAngle(), units::rad, units::deg),
            bus->getGenerationReal() * basePower,
            bus->getGenerationReactive() * basePower,
            bus->getLoadReal() * basePower,
            bus->getLoadReactive() * basePower,
            bus->getLinkReal() * basePower,
            bus->getLinkReactive() * basePower);

        ++nn;
        bus = gds->getBus(nn);
    }
    fprintf(fp, "===============LINE INFORMATION=====================\n");
    fprintf(fp,
            "Area#\tLine #\tLine Name\t\t\t\t\tfrom\tto\t\tP1_2\t\tQ1_2\t\tP2_1\t\tQ2_1\t\tLoss\n");
    Link* lnk;
    mmm = 0;
    Area = gds->getArea(0);
    while (Area != nullptr) {
        nn = 0;
        lnk = Area->getLink(nn);
        while (bus != nullptr) {
            fprintf(fp,
                    "%d\t\t%d\t\t\"%-20s\"\t %5d\t%5d\t %7.5f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\n",
                    Area->getUserID(),
                    lnk->getUserID(),
                    lnk->getName().c_str(),
                    lnk->getBus(1)->getUserID(),
                    lnk->getBus(2)->getUserID(),
                    lnk->getRealPower(1) * basePower,
                    lnk->getReactivePower(1) * basePower,
                    lnk->getRealPower(2) * basePower,
                    lnk->getReactivePower(2) * basePower,
                    lnk->getLoss() * basePower);

            ++nn;
            lnk = Area->getLink(nn);
        }
        ++mmm;
        Area = gds->getArea(mmm);
    }

    nn = 0;
    lnk = gds->getLink(nn);
    while (lnk != nullptr) {
        fprintf(fp,
                "%d\t\t %d\t\t\"%-20s\"\t %5d, %5d\t %7.5f\t %7.5f\t %7.5f\t %7.5f\t %7.5f\n",
                1,
                lnk->getUserID(),
                lnk->getName().c_str(),
                lnk->getBus(1)->getUserID(),
                lnk->getBus(2)->getUserID(),
                lnk->getRealPower(1) * basePower,
                lnk->getReactivePower(1) * basePower,
                lnk->getRealPower(2) * basePower,
                lnk->getReactivePower(2) * basePower,
                lnk->getLoss() * basePower);

        ++nn;
        lnk = gds->getLink(nn);
    }

    fprintf(fp, "===============AREA INFORMATION=====================\n");
    fprintf(
        fp,
        "Area#\tArea Name\t\t\t\tGen Real\t Gen Reactive\t Load Real\t Load Reactive\t Loss\t Export\n");
    mmm = 0;
    Area = gds->getArea(0);
    while (Area != nullptr) {
        fprintf(fp,
                "%d\t\t\"%-20s\"\t %7.2f\t %7.2f\t %7.2f\t %7.2f\t %7.2f\t %7.2f\n",
                Area->getUserID(),
                Area->getName().c_str(),
                Area->getGenerationReal() * basePower,
                Area->getGenerationReactive() * basePower,
                Area->getLoadReal() * basePower,
                Area->getLoadReactive() * basePower,
                Area->getLoss() * basePower,
                -99999.0);

        ++mmm;
        Area = gds->getArea(mmm);
    }

    fprintf(fp,
            "%d\t\t\"%-20s\"\t %7.2f\t %7.2f\t %7.2f\t %7.2f\t %7.2f\t %7.2f\n",
            1,
            gds->getName().c_str(),
            gds->getGenerationReal() * basePower,
            gds->getGenerationReactive() * basePower,
            gds->getLoadReal() * basePower,
            gds->getLoadReactive() * basePower,
            gds->getLoss() * basePower,
            -99999.0);
    gds->log(gds, print_level::normal, "saving txt powerflow to " + fileName);
    fclose(fp);
}

static const double deflim1 = 99999.999;
// static const double deflim2 = -99999.99;

/*
Columns  1- 4   Bus number (I) *
Columns  6-17   Name (A) (left justify) *
Columns 19-20   Load flow area number (I) Don't use zero! *
Columns 21-23   Loss zone number (I)
Columns 25-26   Type (I) *
0 - Unregulated (load, PQ)
1 - Hold MVAR generation within voltage limits, (PQ)
2 - Hold voltage within VAR limits (gen, PV)
3 - Hold voltage and angle (swing, V-Theta) (must always
have one)
Columns 28-33   Final voltage, pu (F) *
Columns 34-40   Final angle, degrees (F) *
Columns 41-49   Load MW (F) *
Columns 50-58   Load MVAR (F) *
Columns 59-67   Generation MW (F) *
Columns 68-75   Generation MVAR (F) *
Columns 77-83   Base KV (F)
Columns 85-90   Desired volts (pu) (F) (This is desired remote voltage if
this bus is controlling another bus.
Columns 91-98   Maximum MVAR or voltage limit (F)
Columns 99-106  Minimum MVAR or voltage limit (F)
Columns 107-114 Shunt conductance G (per unit) (F) *
Columns 115-122 Shunt susceptance B (per unit) (F) *
Columns 124-127 Remote controlled bus number
*/
void cdfBusPrint(FILE* fp, int areaNum, gridBus* bus)
{
    auto type = static_cast<int>(bus->getType());
    fprintf(fp,
            "%4u %-12s %2u%3u %2d %6.4f%7.2f",
            bus->getUserID(),
            bus->getName().c_str(),
            areaNum,
            bus->getInt("zone"),
            type,
            bus->getVoltage(),
            bus->getAngle() * 180.0 / kPI);

    double P = bus->get("p", units::MW);
    double Q = bus->get("q", units::MW);
    double genP = -bus->get("general", units::MW);
    double genQ = -bus->get("genreactive", units::MW);

    fprintf(fp, "%9.2f%9.2f%9.2f%8.2f", P, Q, genP, genQ);
    double vmax = (std::min)(bus->get("vmax"), deflim1);
    double vmin = (std::max)(bus->get("vmin"), 0.0);
    if (type != 0) {
        vmax = bus->get("qmax");
        vmin = bus->get("qmin");
    }
    if (vmax >= deflim1) {
        vmax = 0.0;
    }
    if (vmin < -kHalfBigNum) {
        vmin = 0.0;
    }

    if ((type == 1) || (type == 3)) {
        fprintf(
            fp, " %7.1f %6.3f%8.2f%8.2f", bus->get("basevoltage"), bus->get("vtarget"), vmax, vmin);
    } else {
        fprintf(
            fp, " %7.1f %6.3f%8.4f%8.4f", bus->get("basevoltage"), bus->get("vtarget"), vmax, vmin);
    }

    double yp = bus->get("yp", units::MW);
    double yq = -bus->get("yq", units::MW);
    fprintf(fp, "%8.4f%8.4f %4d\n", yp, yq, 0);
}

/*
Columns  1- 4   Tap bus number (I) *
For transformers or phase shifters, the side of the model
the non-unity tap is on
Columns  6- 9   Z bus number (I) *
For transformers and phase shifters, the side of the model
the device impedance is on.
Columns 11-12   Load flow area (I)
Columns 14-15   Loss zone (I)
Column  17      Circuit (I) * (Use 1 for single lines)
Column  19      Type (I) *
0 - Transmission line
1 - Fixed tap
2 - Variable tap for voltage control (TCUL, LTC)
3 - Variable tap (turns ratio) for MVAR control
4 - Variable phase angle for MW control (phase shifter)
Columns 20-29   Branch resistance R, per unit (F) *
Columns 30-39   Branch reactance X, per unit (F) * No zero impedance lines
Columns 40-50   Line charging B, per unit (F) * (total line charging, +B)
Columns 51-55   Line MVA rating No 1 (I) Left justify!
Columns 57-61   Line MVA rating No 2 (I) Left justify!
Columns 63-67   Line MVA rating No 3 (I) Left justify!
Columns 69-72   Control bus number
Column  74      Side (I)
0 - Controlled bus is one of the terminals
1 - Controlled bus is near the tap side
2 - Controlled bus is near the impedance side (Z bus)
Columns 77-82   Transformer final turns ratio (F)
Columns 84-90   Transformer (phase shifter) final angle (F)
Columns 91-97   Minimum tap or phase shift (F)
Columns 98-104  Maximum tap or phase shift (F)
Columns 106-111 Step size (F)
Columns 113-119 Minimum voltage, MVAR or MW limit (F)
Columns 120-126 Maximum voltage, MVAR or MW limit (F)
*/

void cdfLinkPrint(FILE* fp, int areaNum, acLine* lnk)
{
    int type = 0;
    int cbus = 0;
    double maxVal = 0.0;
    double minVal = 0.0;
    double minAdj = 0.0;
    double maxAdj = 0.0;
    double ssize = 0.0;
    if (dynamic_cast<links::adjustableTransformer*>(lnk) != nullptr) {
        auto alnk = static_cast<links::adjustableTransformer*>(lnk);
        type = alnk->getInt("control_mode");
        switch (type) {
            case 0:
                type = 1;
                maxVal = 0;
                minVal = 0;
                break;
            case 1:
                type = 2;
                maxVal = alnk->get("vmax");
                minVal = alnk->get("vmin");
                minAdj = alnk->get("mintap");
                maxAdj = alnk->get("maxtap");
                ssize = alnk->get("stepsize");
                break;
            case 2:
                type = 3;
                maxVal = alnk->get("qmax", units::MW);
                minVal = alnk->get("qmin", units::MW);
                minAdj = alnk->get("mintap");
                maxAdj = alnk->get("maxtap");
                ssize = alnk->get("stepsize");
                break;
            case 3:
                type = 4;
                maxVal = alnk->get("pmax", units::MW);
                minVal = alnk->get("pmin", units::MW);
                minAdj = alnk->get("mintapangle");
                maxAdj = alnk->get("maxtapangle");
                ssize = alnk->get("stepsize");
                break;
        }
        cbus = alnk->getInt("controlbusid");
    } else {
        if (lnk->getTap() != 1.0) {
            type = 1;
        } else if (lnk->getTapAngle() != 0.0) {
            type = 4;
        }
    }

    fprintf(fp,
            "%4d %4d %2d %2d 1 %1d",
            lnk->getBus(1)->getUserID(),
            lnk->getBus(2)->getUserID(),
            lnk->getInt("zone"),
            areaNum,
            type);
    fprintf(fp, "%10.6f%10.6f%11.5f", lnk->get("r"), lnk->get("x"), lnk->get("b", units::MW));

    double rat1 = lnk->get("ratinga", units::MW);
    double rat2 = lnk->get("ratingb", units::MW);
    double rat3 = lnk->get("erating", units::MW);
    if ((rat1 < 0) || (rat1 > deflim1)) {
        rat1 = 0.0;
    }
    if ((rat2 < 0) || (rat2 > deflim1)) {
        rat2 = 0.0;
    }
    if ((rat3 < 0) || (rat3 > deflim1)) {
        rat3 = 0.0;
    }
    fprintf(fp,
            "%5d %5d %5d %4d %1d  ",
            static_cast<int>(rat1),
            static_cast<int>(rat2),
            static_cast<int>(rat3),
            cbus,
            0);
    switch (type) {
        case 0:
            fprintf(fp, "0.0       0.0 0.0    0.0     0.0    0.0   0.0\n");
            break;
        case 1:
            fprintf(fp, "%6.4f    0.0 0.0    0.0     0.0    0.0   0.0\n", lnk->getTap());
            break;
        case 2:
        case 3:
            fprintf(fp,
                    "%6.4f %6.1f %6.4f %6.4f%7.5f %6.4f %6.4f\n",
                    lnk->getTap(),
                    0.0,
                    minAdj,
                    maxAdj,
                    ssize,
                    minVal,
                    maxVal);
            break;
        case 4:
            fprintf(fp,
                    "%6.4f%7.2f%7.4f%7.2f %6.2f%7.1f%7.1f\n",
                    lnk->getTap(),
                    lnk->getTapAngle() * 180.0 / kPI,
                    minAdj,
                    maxAdj,
                    ssize,
                    minVal,
                    maxVal);
            break;
    }
}

void savePowerFlowCdf(gridDynSimulation* gds, const std::string& fileName)
{
    FILE* fp = fopen(fileName.c_str(), "w");
    if (fp == nullptr) {
        throw(fileOperationError("unable to open file " + fileName));
    }

    double basePower = gds->get("basepower");
    // Title Data
    fprintf(fp,
            " 0 /0 /0  %20s %5d 2016  %27s\n",
            "GridDyn " GRIDDYN_VERSION_STRING_SHORT,
            static_cast<int>(basePower),
            gds->getName().c_str());

    // Bus Data
    fprintf(fp, "BUS DATA FOLLOWS\n");
    index_t nn = 0;
    auto bus = gds->getBus(nn);
    while (bus != nullptr) {
        cdfBusPrint(fp, 1, bus);
        ++nn;
        bus = gds->getBus(nn);
    }

    auto Area = gds->getArea(0);

    index_t mm = 0;
    while (Area != nullptr) {
        nn = 0;
        bus = Area->getBus(nn);
        while (bus != nullptr) {
            cdfBusPrint(fp, Area->getUserID(), bus);
            ++nn;
            bus = Area->getBus(nn);
        }
        ++mm;
        Area = gds->getArea(mm);
    }
    fprintf(fp, "-999\n");

    // Line Data
    fprintf(fp, "BRANCH DATA FOLLOWS\n");
    nn = 0;
    Link* lnk = gds->getLink(nn);
    while (lnk != nullptr) {
        cdfLinkPrint(fp, 1, static_cast<acLine*>(lnk));
        ++nn;
        lnk = gds->getLink(nn);
    }
    mm = 0;
    Area = gds->getArea(mm);
    while (Area != nullptr) {
        nn = 0;
        lnk = Area->getLink(nn);
        while (lnk != nullptr) {
            cdfLinkPrint(fp, Area->getUserID(), static_cast<acLine*>(lnk));
            ++nn;
            lnk = Area->getLink(nn);
        }
        ++mm;
        Area = gds->getArea(mm);
    }

    fprintf(fp, "-999\n");
    fprintf(fp, "LOSS ZONES FOLLOWS \n");
    fprintf(fp, "1 %s\n", gds->getName().c_str());
    fprintf(fp, "-99\n");
    fprintf(fp, "INTERCHANGE DATA FOLLOWS ");
    fprintf(fp, "-9\n");
    fprintf(fp, "TIE LINES FOLLOWS                0 ITEMS\n");
    fprintf(fp, "-999\n");
    fprintf(fp, "END OF DATA");

    fclose(fp);
}

void savePowerFlowXML(gridDynSimulation* gds, const std::string& fileName)
{
    ticpp::Document doc(fileName);

    auto decl = new ticpp::Declaration("0.5", "", "");
    doc.LinkEndChild(decl);

    auto comment = new ticpp::Comment();
    comment->SetValue("Power Flow Result output");
    doc.LinkEndChild(comment);

    auto sol = new ticpp::Element("PowerFlow");
    doc.LinkEndChild(sol);

    auto buses = new ticpp::Element("buses");
    auto prop = new ticpp::Element("count");
    auto buscount = gds->get("buscount");
    auto propval = new ticpp::Text(buscount);
    prop->LinkEndChild(propval);
    buses->LinkEndChild(prop);

    sol->LinkEndChild(buses);
    index_t nn = 0;
    auto bus = gds->getBus(nn);
    while (bus != nullptr) {
        auto busE = new ticpp::Element("bus");
        prop = new ticpp::Element("name");
        propval = new ticpp::Text(bus->getName());
        prop->LinkEndChild(propval);
        busE->LinkEndChild(prop);

        prop = new ticpp::Element("index");
        propval = new ticpp::Text(nn);
        prop->LinkEndChild(propval);
        busE->LinkEndChild(prop);

        prop = new ticpp::Element("voltage");
        propval = new ticpp::Text(bus->getVoltage());
        prop->LinkEndChild(propval);
        busE->LinkEndChild(prop);

        prop = new ticpp::Element("angle");
        propval = new ticpp::Text(bus->getAngle());
        prop->LinkEndChild(propval);
        busE->LinkEndChild(prop);

        buses->LinkEndChild(busE);
        ++nn;
        bus = gds->getBus(nn);
    }

    auto links = new ticpp::Element("links");

    prop = new ticpp::Element("count");
    auto lnkcnt = gds->get("linkcount");
    propval = new ticpp::Text(lnkcnt);
    prop->LinkEndChild(propval);
    links->LinkEndChild(prop);

    sol->LinkEndChild(links);
    nn = 0;
    auto lnk = gds->getLink(nn);
    while (lnk != nullptr) {
        auto linkE = new ticpp::Element("link");
        prop = new ticpp::Element("name");
        propval = new ticpp::Text(lnk->getName());
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("index");
        propval = new ticpp::Text(nn);
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("Bus1");
        propval = new ticpp::Text(lnk->getBus(1)->getUserID());
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("Bus2");
        propval = new ticpp::Text(lnk->getBus(2)->getUserID());
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("RealImpedance");
        propval = new ticpp::Text(lnk->get("r"));
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("ImagImpedance");
        propval = new ticpp::Text(lnk->get("x"));
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("RealIn");
        propval = new ticpp::Text(lnk->getRealPower());
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        prop = new ticpp::Element("RealOut");
        propval = new ticpp::Text(lnk->getRealPower(2));
        prop->LinkEndChild(propval);
        linkE->LinkEndChild(prop);

        links->LinkEndChild(linkE);
        ++nn;
        lnk = gds->getLink(nn);
    }
    doc.SaveFile();
}

void saveBusData(gridDynSimulation* gds, const std::string& fileName)
{
    std::vector<double> V;
    std::vector<double> A;
    std::vector<double> Pgen;
    std::vector<double> Qgen;
    std::vector<double> Pload;
    std::vector<double> Qload;
    std::vector<std::string> bname;
    gds->getVoltage(V);
    gds->getAngle(A);
    gds->getBusGenerationReal(Pgen);
    gds->getBusGenerationReactive(Qgen);
    gds->getBusLoadReactive(Qload);
    gds->getBusLoadReal(Pload);
    gds->getBusName(bname);

    std::ofstream out(fileName);
    out << "Name, Voltage, angle, Pg, Qg, Pl, Ql\n";
    for (size_t aa = 0; aa < V.size(); ++aa) {
        out << bname[aa] << ", " << V[aa] << ", " << A[aa];
        out << ", " << Pgen[aa] << ", " << Qgen[aa] << ", ";
        out << Pload[aa] << ", " << Qload[aa] << "\n";
    }
}

void saveLineData(gridDynSimulation* gds, const std::string& fileName)
{
    std::vector<double> loss;
    std::vector<double> P1;
    std::vector<double> Q1;
    std::vector<double> P2;
    std::vector<double> Q2;
    std::vector<std::string> linkname;
    gds->getLinkLoss(loss);
    gds->getLinkRealPower(P1, 0, 1);
    gds->getLinkRealPower(P2, 0, 2);
    gds->getLinkReactivePower(Q1, 0, 1);
    gds->getLinkReactivePower(Q2, 0, 2);
    gds->getLinkName(linkname);

    std::ofstream out(fileName);
    out << "Name, P1, Q1, P2, Q2, loss\n";
    for (size_t aa = 0; aa < P1.size(); ++aa) {
        out << linkname[aa] << ", " << P1[aa] << ", " << Q1[aa];
        out << ", " << P2[aa] << ", " << Q2[aa] << ", ";
        out << loss[aa] << "\n";
    }
}

void savePowerFlowBinary(gridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void saveState(gridDynSimulation* gds,
               const std::string& fileName,
               const solverMode& sMode,
               bool append)
{
    std::filesystem::path filePath(fileName);
    if (fileName.empty()) {
        auto stateFile = gds->getString("statefile");
        if (stateFile.empty()) {
            std::cerr << "no file specified" << std::endl;
            return;
        }
        filePath = std::filesystem::path(stateFile);
    }

    std::string ext = convertToLowerCase(filePath.extension().string());

    if (ext == ".xml") {
        saveStateXML(gds, fileName, sMode);
    } else if ((ext == ".bin") || (ext == ".dat")) {
        saveStateBinary(gds, fileName, sMode, append);
    } else {
        saveStateBinary(gds, fileName, sMode, append);
    }
}

void saveStateXML(gridDynSimulation* /*gds*/,
                  const std::string& /*fileName*/,
                  const solverMode& /*sMode*/)
{
}

void saveStateBinary(gridDynSimulation* gds,
                     const std::string& fileName,
                     const solverMode& sMode,
                     bool append)
{
    auto& currentMode = gds->getCurrentMode(sMode);
    auto sd = gds->getSolverInterface(currentMode);
    if (!sd) {
        return;
    }
    auto statedata = sd->state_data();
    auto dsize = sd->size();

    auto index = gds->getInt("residcount");
    if (fileName.empty()) {
        auto stateFile = gds->getString("statefile");
        writeVector(gds->getSimulationTime(),
                    index,
                    STATE_INFORMATION,
                    currentMode.offsetIndex,
                    dsize,
                    statedata,
                    stateFile,
                    append);
        if (hasDifferential(currentMode)) {
            writeVector(gds->getSimulationTime(),
                        index,
                        DERIVATIVE_INFORMATION,
                        currentMode.offsetIndex,
                        dsize,
                        sd->deriv_data(),
                        stateFile);
        }
    } else {
        writeVector(gds->getSimulationTime(),
                    index,
                    STATE_INFORMATION,
                    currentMode.offsetIndex,
                    dsize,
                    statedata,
                    fileName,
                    append);
        if (hasDifferential(currentMode)) {
            writeVector(gds->getSimulationTime(),
                        index,
                        DERIVATIVE_INFORMATION,
                        currentMode.offsetIndex,
                        dsize,
                        sd->deriv_data(),
                        fileName);
        }
    }
}

void writeVector(coreTime time,
                 std::uint32_t code,
                 std::uint32_t index,
                 std::uint32_t key,
                 std::uint32_t numElements,
                 const double* data,
                 const std::string& fileName,
                 bool append)
{
    std::ofstream bFile;
    if (append) {
        bFile.open(fileName.c_str(), std::ios::out | std::ios::binary | std::ios::app);
    } else {
        bFile.open(fileName.c_str(), std::ios::out | std::ios::binary);
    }
    if (!bFile.is_open()) {
        throw(fileOperationError("unable to open file " + fileName));
    }
    code &= 0x0000FFFF;  // make sure we don't change the data type
    bFile.write(reinterpret_cast<char*>(&time), sizeof(double));
    bFile.write(reinterpret_cast<char*>(&code), sizeof(std::uint32_t));
    bFile.write(reinterpret_cast<char*>(&index), sizeof(std::uint32_t));
    bFile.write(reinterpret_cast<char*>(&key), sizeof(std::uint32_t));
    bFile.write(reinterpret_cast<char*>(&numElements), sizeof(std::uint32_t));
    bFile.write(reinterpret_cast<const char*>(data), sizeof(double) * numElements);
}

void writeArray(coreTime time,
                std::uint32_t code,
                std::uint32_t index,
                std::uint32_t key,
                matrixData<double>& a1,
                const std::string& fileName,
                bool append)
{
    std::ofstream bFile;
    if (append) {
        bFile.open(fileName.c_str(), std::ios::out | std::ios::binary | std::ios::app);
    } else {
        bFile.open(fileName.c_str(), std::ios::out | std::ios::binary);
    }
    if (!bFile.is_open()) {
        throw(fileOperationError("unable to open file " + fileName));
    }
    code &= 0x0000FFFF;
    code |= 0x00010000;
    bFile.write(reinterpret_cast<char*>(&time), sizeof(double));
    bFile.write(reinterpret_cast<char*>(&code), sizeof(std::uint32_t));
    bFile.write(reinterpret_cast<char*>(&index), sizeof(std::uint32_t));
    bFile.write(reinterpret_cast<char*>(&key), sizeof(std::uint32_t));
    std::uint32_t numElements = a1.size();
    bFile.write(reinterpret_cast<char*>(&numElements), sizeof(std::uint32_t));
    a1.start();
    for (size_t nn = 0; nn < numElements; ++nn) {
        auto el = a1.next();
        bFile.write(reinterpret_cast<char*>(&el), sizeof(matrixElement<double>));
    }
}

void loadState(gridDynSimulation* gds, const std::string& fileName, const solverMode& sMode)
{
    std::filesystem::path filePath(fileName);
    if (fileName.empty()) {
        auto stateFile = gds->getString("statefile");
        if (stateFile.empty()) {
            std::cerr << "no file specified" << std::endl;
            gds->log(gds, print_level::error, "no state file specified");
            throw(invalidFileName());
        }
        filePath = std::filesystem::path(stateFile);
    }

    if (!std::filesystem::exists(filePath)) {
        gds->log(gds, print_level::error, "file does not exist");
        throw(invalidFileName());
    }

    std::string ext = convertToLowerCase(filePath.extension().string());

    if (ext == ".xml") {
        loadStateXML(gds, fileName, sMode);
    } else if ((ext == ".bin") || (ext == ".dat")) {
        loadStateBinary(gds, fileName, sMode);
    } else {
        loadStateBinary(gds, fileName, sMode);
    }
}

void loadStateBinary(gridDynSimulation* gds, const std::string& fileName, const solverMode& sMode)
{
    const solverMode& currentMode = gds->getCurrentMode(sMode);
    auto sd = gds->getSolverInterface(currentMode);
    if (!sd) {
        return;
    }

    std::ifstream bFile;
    if (fileName.empty()) {
        auto stateFile = gds->getString("statefile");

        bFile.open(stateFile.c_str(), std::ios::in | std::ios::binary);
    } else {
        bFile.open(fileName.c_str(), std::ios::in | std::ios::binary);
    }
    if (!bFile.is_open()) {
        gds->log(gds, print_level::error, "Unable to open file for writing:" + fileName);
        throw(fileOperationError("unable to open file " + fileName));
    }
    count_t dsize;
    bFile.read(reinterpret_cast<char*>(&dsize), sizeof(int));
    if (sd->size() != dsize) {
        if (!sd->isInitialized()) {
            sd->allocate(dsize);
        } else {
            gds->log(gds, print_level::error, "statefile does not match solverMode in size");
            return;
        }
    }
    // TODO(phlpt): Check this index at some point; the right handling is still unclear,
    // might be used for automatic solverMode location  instead of what is done currently.
    unsigned int oi;
    bFile.read(reinterpret_cast<char*>(&(oi)), sizeof(int));
    bFile.read(reinterpret_cast<char*>(sd->state_data()), sizeof(double) * dsize);
    if (isDynamic(sMode)) {
        bFile.read(reinterpret_cast<char*>(sd->deriv_data()), sizeof(double) * dsize);
    }
}

void loadStateXML(gridDynSimulation* /*gds*/,
                  const std::string& /*fileName*/,
                  const solverMode& /*sMode*/)
{
}

void loadPowerFlow(gridDynSimulation* gds, const std::string& fileName)
{
    std::filesystem::path filePath(fileName);
    std::string ext = convertToLowerCase(filePath.extension().string());
    // get rid of the . on the extension if it has one
    if (ext[0] == '.') {
        ext.erase(0, 1);
    }
    if (ext == "xml") {
        loadPowerFlowXML(gds, fileName);
    } else if (ext == "csv") {
        loadPowerFlowCSV(gds, fileName);
    } else if ((ext == "cdf") || (ext == "txt")) {
        loadPowerFlowCdf(gds, fileName);
    } else {
        loadPowerFlowBinary(gds, fileName);
    }
}

void loadPowerFlowCdf(gridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void loadPowerFlowCSV(gridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void loadPowerFlowBinary(gridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void loadPowerFlowXML(gridDynSimulation* gds, const std::string& fileName)
{
    ticpp::Document doc(fileName);
    doc.LoadFile();
    auto flow = doc.FirstChildElement();
    auto buses = flow->FirstChildElement("buses");
    auto links = flow->FirstChildElement("links");
    // check to make sure the buscount is correct
    auto cd = buses->FirstChildElement("count");
    int count;
    cd->FirstChild()->GetValue(&count);
    auto busCount = gds->getInt("buscount");
    if (count != busCount) {
        return;
    }
    // check to make sure the link count is correct
    cd = links->FirstChildElement("count");
    cd->FirstChild()->GetValue(&count);
    auto linkCount = gds->getInt("linkcount");
    if (count != linkCount) {
        return;
    }
    // loop over the buses
    cd = buses->FirstChildElement("bus", false);
    while (cd != nullptr) {
        index_t kk;
        double val1, val2;
        auto prop = cd->FirstChildElement("index", false);
        prop->FirstChild()->GetValue(&kk);

        prop = cd->FirstChildElement("voltage", false);
        prop->FirstChild()->GetValue(&val1);

        prop = cd->FirstChildElement("angle", false);
        prop->FirstChild()->GetValue(&val2);
        auto bus = gds->getBus(kk);
        bus->setVoltageAngle(val1, val2);
        cd = cd->NextSiblingElement("bus", false);
    }
}

void captureJacState(gridDynSimulation* gds, const std::string& fileName, const solverMode& sMode)
{
    std::ofstream bFile(fileName.c_str(), std::ios::out | std::ios::binary);
    if (!bFile.is_open()) {
        gds->log(gds, print_level::error, "Unable to open file for writing:" + fileName);
        throw(fileOperationError("unable to open file " + fileName));
    }
    // writing the state vector
    auto& currentMode = gds->getCurrentMode(sMode);
    auto sd = gds->getSolverInterface(currentMode);
    matrixDataSparse<double> md;
    stateData sD(gds->getSimulationTime(), sd->state_data(), sd->deriv_data());

    sD.cj = 10000;

    gds->jacobianElements(noInputs, sD, md, noInputLocs, currentMode);

    stringVec stateNames;
    gds->getStateName(stateNames, currentMode);

    count_t dsize = sd->size();

    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(unsigned int));
    for (auto& stN : stateNames) {
        auto stnSize = static_cast<unsigned int>(stN.length());
        bFile.write(reinterpret_cast<char*>(&stnSize), sizeof(int));
        bFile.write(stN.c_str(), stnSize);
    }

    // write the state vector
    bFile.write(reinterpret_cast<char*>(sd->state_data()), dsize * sizeof(double));
    // writing the Jacobian Matrix
    dsize = md.size();
    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(count_t));

    for (index_t kk = 0; kk < dsize; ++kk) {
        auto el = md.element(kk);
        bFile.write(reinterpret_cast<char*>(&(el.row)), sizeof(index_t));
        bFile.write(reinterpret_cast<char*>(&(el.col)), sizeof(index_t));
        bFile.write(reinterpret_cast<char*>(&(el.data)), sizeof(double));
    }

    bFile.close();
}

void saveJacobian(gridDynSimulation* gds, const std::string& fileName, const solverMode& sMode)
{
    std::ofstream bFile(fileName.c_str(), std::ios::out | std::ios::binary);
    if (!bFile.is_open()) {
        gds->log(gds, print_level::error, "Unable to open file for writing:" + fileName);
        throw(fileOperationError("unable to open file " + fileName));
    }
    // writing the state vector
    auto& currentMode = gds->getCurrentMode(sMode);
    auto SolverInterface = gds->getSolverInterface(currentMode);

    matrixDataSparse<double> md;

    stateData sD(gds->getSimulationTime(),
                 SolverInterface->state_data(),
                 SolverInterface->deriv_data());

    sD.cj = 10000;
    gds->jacobianElements(noInputs, sD, md, noInputLocs, currentMode);

    stringVec stateNames;
    gds->getStateName(stateNames, currentMode);

    count_t dsize = SolverInterface->size();
    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(count_t));
    for (auto& stN : stateNames) {
        auto stnSize = static_cast<unsigned int>(stN.length());
        bFile.write(reinterpret_cast<char*>(&stnSize), sizeof(int));
        bFile.write(stN.c_str(), stnSize);
    }
    // writing the Jacobian Matrix
    dsize = md.size();
    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(count_t));

    for (index_t kk = 0; kk < dsize; ++kk) {
        auto el = md.element(kk);
        bFile.write(reinterpret_cast<char*>(&(el.row)), sizeof(index_t));
        bFile.write(reinterpret_cast<char*>(&(el.col)), sizeof(index_t));
        bFile.write(reinterpret_cast<char*>(&(el.data)), sizeof(double));
    }
}

void saveContingencyOutput(const std::vector<std::shared_ptr<Contingency>>& contList,
                           const std::string& fileName)
{
    if (contList.empty()) {
        return;
    }
    std::ofstream bFile(fileName.c_str(), std::ios::out);
    while (!contList[0]->isFinished()) {
        contList[0]->wait();
    }
    bFile << contList[0]->generateHeader() << "\n";
    for (auto& cont : contList) {
        while (!cont->isFinished()) {
            cont->wait();
        }
        bFile << cont->generateFullOutputLine() << "\n";
    }
}

}  // namespace griddyn
