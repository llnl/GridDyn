/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
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
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "units/units.hpp"
#include "utilities/matrixDataSparse.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <pugixml.hpp>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::convertToLowerCase;

namespace {
constexpr double defaultLimitValue = 99999.999;

double degreesFromRadians(double angleRadians)
{
    return angleRadians * 180.0 / kPI;
}

std::ofstream makeOutputFile(const std::string& fileName)
{
    std::ofstream output(fileName);
    if (!output.is_open()) {
        throw(fileOperationError("unable to open file " + fileName));
    }
    return output;
}

std::string formatCsvBusRecord(int areaNumber, int busNumber, const gridBus& bus, double basePower)
{
    std::ostringstream output;
    output << areaNumber << ", " << busNumber << ", " << bus.getUserID() << ", "
           << std::quoted(bus.getName()) << ", " << std::fixed << std::setw(7)
           << std::setprecision(6) << bus.getVoltage() << ", " << std::showpos << std::setw(8)
           << std::setprecision(4) << degreesFromRadians(bus.getAngle()) << std::noshowpos << ", "
           << std::setw(7) << std::setprecision(5) << bus.getGenerationReal() * basePower << ", "
           << std::setw(7) << std::setprecision(5) << bus.getGenerationReactive() * basePower
           << ", " << std::setw(7) << std::setprecision(5) << bus.getLoadReal() * basePower
           << ", " << std::setw(7) << std::setprecision(5) << bus.getLoadReactive() * basePower
           << ", " << std::setw(7) << std::setprecision(5) << bus.getLinkReal() * basePower
           << ", " << std::setw(7) << std::setprecision(5) << bus.getLinkReactive() * basePower
           << ", " << std::setw(7) << std::setprecision(5)
           << (bus.getGenerationReal() + bus.getLoadReal() + bus.getLinkReal()) * basePower
           << ", " << std::setw(7) << std::setprecision(5)
           << (bus.getGenerationReactive() + bus.getLoadReactive() + bus.getLinkReactive()) *
                  basePower;
    return output.str();
}

std::string formatTxtBusRecord(int areaNumber, const gridBus& bus, double basePower)
{
    std::ostringstream output;
    output << areaNumber << "\t\t " << bus.getUserID() << "\t\t" << std::quoted(bus.getName())
           << "\t " << std::fixed << std::setw(7) << std::setprecision(6) << bus.getVoltage()
           << "\t " << std::showpos << std::setw(8) << std::setprecision(4)
           << degreesFromRadians(bus.getAngle()) << std::noshowpos << "\t " << std::setw(7)
           << std::setprecision(5) << bus.getGenerationReal() * basePower << "\t " << std::setw(7)
           << std::setprecision(5) << bus.getGenerationReactive() * basePower << "\t "
           << std::setw(7) << std::setprecision(5) << bus.getLoadReal() * basePower << "\t "
           << std::setw(7) << std::setprecision(5) << bus.getLoadReactive() * basePower << "\t "
           << std::setw(7) << std::setprecision(5) << bus.getLinkReal() * basePower << "\t "
           << std::setw(7) << std::setprecision(5) << bus.getLinkReactive() * basePower;
    return output.str();
}

std::string formatTxtLinkRecord(int areaNumber, const Link& link, double basePower)
{
    std::ostringstream output;
    output << areaNumber << "\t\t" << link.getUserID() << "\t\t" << std::quoted(link.getName())
           << "\t " << std::setw(5) << link.getBus(1)->getUserID() << "\t" << std::setw(5)
           << link.getBus(2)->getUserID() << "\t " << std::fixed << std::setw(7)
           << std::setprecision(5) << link.getRealPower(1) * basePower << "\t " << std::setw(7)
           << std::setprecision(5) << link.getReactivePower(1) * basePower << "\t "
           << std::setw(7) << std::setprecision(5) << link.getRealPower(2) * basePower << "\t "
           << std::setw(7) << std::setprecision(5) << link.getReactivePower(2) * basePower
           << "\t " << std::setw(7) << std::setprecision(5) << link.getLoss() * basePower;
    return output.str();
}

std::string formatTxtAreaRecord(int areaNumber,
                                const std::string& areaName,
                                double generationReal,
                                double generationReactive,
                                double loadReal,
                                double loadReactive,
                                double loss)
{
    std::ostringstream output;
    output << areaNumber << "\t\t" << std::quoted(areaName) << "\t " << std::fixed
           << std::setw(7) << std::setprecision(2) << generationReal << "\t " << std::setw(7)
           << std::setprecision(2) << generationReactive << "\t " << std::setw(7)
           << std::setprecision(2) << loadReal << "\t " << std::setw(7) << std::setprecision(2)
           << loadReactive << "\t " << std::setw(7) << std::setprecision(2) << loss << "\t "
           << std::setw(7) << std::setprecision(2) << -99999.0;
    return output.str();
}
}  // namespace

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

    const std::string ext = convertToLowerCase(filePath.extension().string());

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
    auto output = makeOutputFile(fileName);
    const double basePower = gds->get("basepower");
    output << std::fixed << "basepower=" << basePower << '\n';
    output << "\"Area #\",\"Bus #\",\"Bus ID\",\"Bus "
              "name\",\"voltage(pu)\",\"angle(deg)\",\"Pgen(MW)\",\"Qgen(MW)\",\"Pload(MW)\",\"Qload(MW)\","
              "\"Plink(MW)\",\"Qlink(MW)\",\"PResid(MW)\",\"QResid(MW)\"\n";
    index_t areaIndex = 0;
    const auto* area = gds->getArea(areaIndex);
    while (area != nullptr) {
        index_t busIndex = 0;
        const auto* bus = area->getBus(busIndex);
        while (bus != nullptr) {
            output << formatCsvBusRecord(area->getUserID(), bus->getUserID(), *bus, basePower)
                   << '\n';
            ++busIndex;
            bus = area->getBus(busIndex);
        }
        ++areaIndex;
        area = gds->getArea(areaIndex);
    }

    index_t busIndex = 0;
    auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        bus->updateLocalCache();
        output << formatCsvBusRecord(1, bus->locIndex + 1, *bus, basePower) << '\n';
        ++busIndex;
        bus = gds->getBus(busIndex);
    }
    gds->log(gds, print_level::normal, "saving csv powerflow to " + fileName);
}

void savePowerFlowTXT(gridDynSimulation* gds, const std::string& fileName)
{
    auto output = makeOutputFile(fileName);
    const double basePower = gds->get("basepower");
    output << gds->getName() << " basepower=" << std::fixed << basePower << '\n';
    output << "Simulation " << gds->getInt("totalbuscount") << " buses "
           << gds->getInt("totallinkcount") << " lines\n";

    output << "===============BUS INFORMATION=====================\n";
    output << "Area#\tBus#\tBus "
              "name\t\t\t\tvoltage(pu)\tangle(deg)\tPgen(MW)\tQgen(MW)\tPload(MW)\tQload(MW)\tPlink(MW)\tQlink(MW)\n";
    index_t areaIndex = 0;
    const auto* area = gds->getArea(areaIndex);
    while (area != nullptr) {
        index_t busIndex = 0;
        const auto* bus = area->getBus(busIndex);
        while (bus != nullptr) {
            output << formatTxtBusRecord(area->getUserID(), *bus, basePower) << '\n';
            ++busIndex;
            bus = area->getBus(busIndex);
        }
        ++areaIndex;
        area = gds->getArea(areaIndex);
    }

    index_t busIndex = 0;
    const auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        output << formatTxtBusRecord(1, *bus, basePower) << '\n';
        ++busIndex;
        bus = gds->getBus(busIndex);
    }
    output << "===============LINE INFORMATION=====================\n";
    output << "Area#\tLine #\tLine Name\t\t\t\t\tfrom\tto\t\tP1_2\t\tQ1_2\t\tP2_1\t\tQ2_1\t\tLoss\n";
    areaIndex = 0;
    area = gds->getArea(areaIndex);
    while (area != nullptr) {
        index_t linkIndex = 0;
        const auto* link = area->getLink(linkIndex);
        while (link != nullptr) {
            output << formatTxtLinkRecord(area->getUserID(), *link, basePower) << '\n';
            ++linkIndex;
            link = area->getLink(linkIndex);
        }
        ++areaIndex;
        area = gds->getArea(areaIndex);
    }

    index_t linkIndex = 0;
    const auto* link = gds->getLink(linkIndex);
    while (link != nullptr) {
        output << formatTxtLinkRecord(1, *link, basePower) << '\n';
        ++linkIndex;
        link = gds->getLink(linkIndex);
    }

    output << "===============AREA INFORMATION=====================\n";
    output << "Area#\tArea Name\t\t\t\tGen Real\t Gen Reactive\t Load Real\t Load Reactive\t Loss\t Export\n";
    areaIndex = 0;
    area = gds->getArea(areaIndex);
    while (area != nullptr) {
        output << formatTxtAreaRecord(area->getUserID(),
                                      area->getName(),
                                      area->getGenerationReal() * basePower,
                                      area->getGenerationReactive() * basePower,
                                      area->getLoadReal() * basePower,
                                      area->getLoadReactive() * basePower,
                                      area->getLoss() * basePower)
               << '\n';
        ++areaIndex;
        area = gds->getArea(areaIndex);
    }

    output << formatTxtAreaRecord(1,
                                  gds->getName(),
                                  gds->getGenerationReal() * basePower,
                                  gds->getGenerationReactive() * basePower,
                                  gds->getLoadReal() * basePower,
                                  gds->getLoadReactive() * basePower,
                                  gds->getLoss() * basePower)
           << '\n';
    gds->log(gds, print_level::normal, "saving txt powerflow to " + fileName);
}

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
static void cdfBusPrint(std::ostream& output, int areaNumber, const gridBus& bus)
{
    const auto type = static_cast<int>(bus.getType());
    output << std::setw(4) << bus.getUserID() << ' ' << std::left << std::setw(12)
           << bus.getName() << std::right << ' ' << std::setw(2) << areaNumber << std::setw(3)
           << bus.getInt("zone") << ' ' << std::setw(2) << type << ' ' << std::fixed
           << std::setw(6) << std::setprecision(4) << bus.getVoltage() << std::setw(7)
           << std::setprecision(2) << degreesFromRadians(bus.getAngle());

    const double realPower = bus.get("p", units::MW);
    const double reactivePower = bus.get("q", units::MW);
    const double generatedRealPower = -bus.get("general", units::MW);
    const double generatedReactivePower = -bus.get("genreactive", units::MW);

    output << std::setw(9) << std::setprecision(2) << realPower << std::setw(9)
           << std::setprecision(2) << reactivePower << std::setw(9) << std::setprecision(2)
           << generatedRealPower << std::setw(8) << std::setprecision(2)
           << generatedReactivePower;

    double vmax = (std::min)(bus.get("vmax"), defaultLimitValue);
    double vmin = (std::max)(bus.get("vmin"), 0.0);
    if (type != 0) {
        vmax = bus.get("qmax");
        vmin = bus.get("qmin");
    }
    if (vmax >= defaultLimitValue) {
        vmax = 0.0;
    }
    if (vmin < -kHalfBigNum) {
        vmin = 0.0;
    }

    if ((type == 1) || (type == 3)) {
        output << ' ' << std::setw(7) << std::setprecision(1) << bus.get("basevoltage") << ' '
               << std::setw(6) << std::setprecision(3) << bus.get("vtarget") << std::setw(8)
               << std::setprecision(2) << vmax << std::setw(8) << std::setprecision(2) << vmin;
    } else {
        output << ' ' << std::setw(7) << std::setprecision(1) << bus.get("basevoltage") << ' '
               << std::setw(6) << std::setprecision(3) << bus.get("vtarget") << std::setw(8)
               << std::setprecision(4) << vmax << std::setw(8) << std::setprecision(4) << vmin;
    }

    const double shuntRealPower = bus.get("yp", units::MW);
    const double shuntReactivePower = -bus.get("yq", units::MW);
    output << std::setw(8) << std::setprecision(4) << shuntRealPower << std::setw(8)
           << std::setprecision(4) << shuntReactivePower << ' ' << std::setw(4) << 0 << '\n';
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

static void cdfLinkPrint(std::ostream& output, int areaNumber, acLine* link)
{
    int type = 0;
    int controlBus = 0;
    double maxVal = 0.0;
    double minVal = 0.0;
    double minAdj = 0.0;
    double maxAdj = 0.0;
    double stepSize = 0.0;
    if (dynamic_cast<links::adjustableTransformer*>(link) != nullptr) {
        auto* adjustableLink = static_cast<links::adjustableTransformer*>(link);
        type = adjustableLink->getInt("control_mode");
        switch (type) {
            case 0:
                type = 1;
                maxVal = 0.0;
                minVal = 0.0;
                break;
            case 1:
                type = 2;
                maxVal = adjustableLink->get("vmax");
                minVal = adjustableLink->get("vmin");
                minAdj = adjustableLink->get("mintap");
                maxAdj = adjustableLink->get("maxtap");
                stepSize = adjustableLink->get("stepsize");
                break;
            case 2:
                type = 3;
                maxVal = adjustableLink->get("qmax", units::MW);
                minVal = adjustableLink->get("qmin", units::MW);
                minAdj = adjustableLink->get("mintap");
                maxAdj = adjustableLink->get("maxtap");
                stepSize = adjustableLink->get("stepsize");
                break;
            case 3:
                type = 4;
                maxVal = adjustableLink->get("pmax", units::MW);
                minVal = adjustableLink->get("pmin", units::MW);
                minAdj = adjustableLink->get("mintapangle");
                maxAdj = adjustableLink->get("maxtapangle");
                stepSize = adjustableLink->get("stepsize");
                break;
            default:
                break;
        }
        controlBus = adjustableLink->getInt("controlbusid");
    } else if (link->getTap() != 1.0) {
        type = 1;
    } else if (link->getTapAngle() != 0.0) {
        type = 4;
    }

    output << std::setw(4) << link->getBus(1)->getUserID() << ' ' << std::setw(4)
           << link->getBus(2)->getUserID() << ' ' << std::setw(2) << link->getInt("zone") << ' '
           << std::setw(2) << areaNumber << " 1 " << std::setw(1) << type << std::fixed
           << std::setw(10) << std::setprecision(6) << link->get("r") << std::setw(10)
           << std::setprecision(6) << link->get("x") << std::setw(11) << std::setprecision(5)
           << link->get("b", units::MW);

    double ratingA = link->get("ratinga", units::MW);
    double ratingB = link->get("ratingb", units::MW);
    double emergencyRating = link->get("erating", units::MW);
    if ((ratingA < 0) || (ratingA > defaultLimitValue)) {
        ratingA = 0.0;
    }
    if ((ratingB < 0) || (ratingB > defaultLimitValue)) {
        ratingB = 0.0;
    }
    if ((emergencyRating < 0) || (emergencyRating > defaultLimitValue)) {
        emergencyRating = 0.0;
    }
    output << std::setw(5) << static_cast<int>(ratingA) << ' ' << std::setw(5)
           << static_cast<int>(ratingB) << ' ' << std::setw(5)
           << static_cast<int>(emergencyRating) << ' ' << std::setw(4) << controlBus << ' '
           << std::setw(1) << 0 << "  ";
    switch (type) {
        case 0:
            output << "0.0       0.0 0.0    0.0     0.0    0.0   0.0\n";
            break;
        case 1:
            output << std::setw(6) << std::setprecision(4) << link->getTap()
                   << "    0.0 0.0    0.0     0.0    0.0   0.0\n";
            break;
        case 2:
        case 3:
            output << std::setw(6) << std::setprecision(4) << link->getTap() << ' '
                   << std::setw(6) << std::setprecision(1) << 0.0 << ' ' << std::setw(6)
                   << std::setprecision(4) << minAdj << ' ' << std::setw(6)
                   << std::setprecision(4) << maxAdj << std::setw(7) << std::setprecision(5)
                   << stepSize << ' ' << std::setw(6) << std::setprecision(4) << minVal << ' '
                   << std::setw(6) << std::setprecision(4) << maxVal << '\n';
            break;
        case 4:
            output << std::setw(6) << std::setprecision(4) << link->getTap() << std::setw(7)
                   << std::setprecision(2) << degreesFromRadians(link->getTapAngle())
                   << std::setw(7) << std::setprecision(4) << minAdj << std::setw(7)
                   << std::setprecision(2) << maxAdj << ' ' << std::setw(6)
                   << std::setprecision(2) << stepSize << std::setw(7) << std::setprecision(1)
                   << minVal << std::setw(7) << std::setprecision(1) << maxVal << '\n';
            break;
        default:
            output << "0.0       0.0 0.0    0.0     0.0    0.0   0.0\n";
            break;
    }
}

void savePowerFlowCdf(gridDynSimulation* gds, const std::string& fileName)
{
    auto output = makeOutputFile(fileName);
    const double basePower = gds->get("basepower");
    // Title Data
    output << " 0 /0 /0  " << std::setw(20) << std::right
           << ("GridDyn " GRIDDYN_VERSION_STRING_SHORT) << ' ' << std::setw(5)
           << static_cast<int>(basePower) << " 2016  " << std::setw(27) << gds->getName()
           << '\n';

    // Bus Data
    output << "BUS DATA FOLLOWS\n";
    index_t busIndex = 0;
    auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        cdfBusPrint(output, 1, *bus);
        ++busIndex;
        bus = gds->getBus(busIndex);
    }

    auto* area = gds->getArea(0);

    index_t areaIndex = 0;
    while (area != nullptr) {
        busIndex = 0;
        bus = area->getBus(busIndex);
        while (bus != nullptr) {
            cdfBusPrint(output, area->getUserID(), *bus);
            ++busIndex;
            bus = area->getBus(busIndex);
        }
        ++areaIndex;
        area = gds->getArea(areaIndex);
    }
    output << "-999\n";

    // Line Data
    output << "BRANCH DATA FOLLOWS\n";
    index_t linkIndex = 0;
    auto* link = gds->getLink(linkIndex);
    while (link != nullptr) {
        cdfLinkPrint(output, 1, static_cast<acLine*>(link));
        ++linkIndex;
        link = gds->getLink(linkIndex);
    }
    areaIndex = 0;
    area = gds->getArea(areaIndex);
    while (area != nullptr) {
        linkIndex = 0;
        link = area->getLink(linkIndex);
        while (link != nullptr) {
            cdfLinkPrint(output, area->getUserID(), static_cast<acLine*>(link));
            ++linkIndex;
            link = area->getLink(linkIndex);
        }
        ++areaIndex;
        area = gds->getArea(areaIndex);
    }

    output << "-999\n";
    output << "LOSS ZONES FOLLOWS \n";
    output << "1 " << gds->getName() << '\n';
    output << "-99\n";
    output << "INTERCHANGE DATA FOLLOWS ";
    output << "-9\n";
    output << "TIE LINES FOLLOWS                0 ITEMS\n";
    output << "-999\n";
    output << "END OF DATA";
}

void savePowerFlowXML(gridDynSimulation* gds, const std::string& fileName)
{
    pugi::xml_document doc;
    auto decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";

    auto comment = doc.append_child(pugi::node_comment);
    comment.set_value("Power Flow Result output");

    auto sol = doc.append_child("PowerFlow");

    auto addTextChild = [](pugi::xml_node parent, const char* name, const auto& value) {
        auto child = parent.append_child(name);
        child.text().set(value);
        return child;
    };

    auto buses = sol.append_child("buses");
    addTextChild(buses, "count", gds->get("buscount"));

    index_t busIndex = 0;
    auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        auto busElement = buses.append_child("bus");
        addTextChild(busElement, "name", bus->getName().c_str());
        addTextChild(busElement, "index", busIndex);
        addTextChild(busElement, "voltage", bus->getVoltage());
        addTextChild(busElement, "angle", bus->getAngle());
        ++busIndex;
        bus = gds->getBus(busIndex);
    }

    auto links = sol.append_child("links");
    addTextChild(links, "count", gds->get("linkcount"));

    index_t linkIndex = 0;
    auto* link = gds->getLink(linkIndex);
    while (link != nullptr) {
        auto linkElement = links.append_child("link");
        addTextChild(linkElement, "name", link->getName().c_str());
        addTextChild(linkElement, "index", linkIndex);
        addTextChild(linkElement, "Bus1", link->getBus(1)->getUserID());
        addTextChild(linkElement, "Bus2", link->getBus(2)->getUserID());
        addTextChild(linkElement, "RealImpedance", link->get("r"));
        addTextChild(linkElement, "ImagImpedance", link->get("x"));
        addTextChild(linkElement, "RealIn", link->getRealPower());
        addTextChild(linkElement, "RealOut", link->getRealPower(2));
        ++linkIndex;
        link = gds->getLink(linkIndex);
    }
    if (!doc.save_file(fileName.c_str())) {
        throw(fileOperationError("unable to open file " + fileName));
    }
}

void saveBusData(gridDynSimulation* gds, const std::string& fileName)
{
    std::vector<double> voltages;
    std::vector<double> angles;
    std::vector<double> generatedRealPower;
    std::vector<double> generatedReactivePower;
    std::vector<double> loadRealPower;
    std::vector<double> loadReactivePower;
    std::vector<std::string> busNames;
    gds->getVoltage(voltages);
    gds->getAngle(angles);
    gds->getBusGenerationReal(generatedRealPower);
    gds->getBusGenerationReactive(generatedReactivePower);
    gds->getBusLoadReactive(loadReactivePower);
    gds->getBusLoadReal(loadRealPower);
    gds->getBusName(busNames);

    std::ofstream out(fileName);
    out << "Name, Voltage, angle, Pg, Qg, Pl, Ql\n";
    for (size_t entryIndex = 0; entryIndex < voltages.size(); ++entryIndex) {
        out << busNames[entryIndex] << ", " << voltages[entryIndex] << ", " << angles[entryIndex];
        out << ", " << generatedRealPower[entryIndex] << ", "
            << generatedReactivePower[entryIndex] << ", ";
        out << loadRealPower[entryIndex] << ", " << loadReactivePower[entryIndex] << "\n";
    }
}

void saveLineData(gridDynSimulation* gds, const std::string& fileName)
{
    std::vector<double> loss;
    std::vector<double> forwardRealPower;
    std::vector<double> forwardReactivePower;
    std::vector<double> reverseRealPower;
    std::vector<double> reverseReactivePower;
    std::vector<std::string> linkNames;
    gds->getLinkLoss(loss);
    gds->getLinkRealPower(forwardRealPower, 0, 1);
    gds->getLinkRealPower(reverseRealPower, 0, 2);
    gds->getLinkReactivePower(forwardReactivePower, 0, 1);
    gds->getLinkReactivePower(reverseReactivePower, 0, 2);
    gds->getLinkName(linkNames);

    std::ofstream out(fileName);
    out << "Name, P1, Q1, P2, Q2, loss\n";
    for (size_t entryIndex = 0; entryIndex < forwardRealPower.size(); ++entryIndex) {
        out << linkNames[entryIndex] << ", " << forwardRealPower[entryIndex] << ", "
            << forwardReactivePower[entryIndex];
        out << ", " << reverseRealPower[entryIndex] << ", "
            << reverseReactivePower[entryIndex] << ", ";
        out << loss[entryIndex] << "\n";
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
            std::cerr << "no file specified\n";
            return;
        }
        filePath = std::filesystem::path(stateFile);
    }

    const std::string ext = convertToLowerCase(filePath.extension().string());

    if (ext == ".xml") {
        saveStateXML(gds, fileName, sMode);
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
    const auto& currentMode = gds->getCurrentMode(sMode);
    auto solverInterface = gds->getSolverInterface(currentMode);
    if (!solverInterface) {
        return;
    }
    auto stateData = solverInterface->state_data();
    auto dataSize = solverInterface->size();

    auto index = gds->getInt("residcount");
    if (fileName.empty()) {
        auto stateFile = gds->getString("statefile");
        writeVector(gds->getSimulationTime(),
                    index,
                    STATE_INFORMATION,
                    currentMode.offsetIndex,
                    dataSize,
                    stateData,
                    stateFile,
                    append);
        if (hasDifferential(currentMode)) {
            writeVector(gds->getSimulationTime(),
                        index,
                        DERIVATIVE_INFORMATION,
                        currentMode.offsetIndex,
                        dataSize,
                        solverInterface->deriv_data(),
                        stateFile);
        }
    } else {
        writeVector(gds->getSimulationTime(),
                    index,
                    STATE_INFORMATION,
                    currentMode.offsetIndex,
                    dataSize,
                    stateData,
                    fileName,
                    append);
        if (hasDifferential(currentMode)) {
            writeVector(gds->getSimulationTime(),
                        index,
                        DERIVATIVE_INFORMATION,
                        currentMode.offsetIndex,
                        dataSize,
                        solverInterface->deriv_data(),
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
    for (size_t elementIndex = 0; elementIndex < numElements; ++elementIndex) {
        const auto elementData = a1.next();
        bFile.write(reinterpret_cast<const char*>(&elementData), sizeof(matrixElement<double>));
    }
}

void loadState(gridDynSimulation* gds, const std::string& fileName, const solverMode& sMode)
{
    const std::filesystem::path filePath(fileName);
    if (fileName.empty()) {
        auto stateFile = gds->getString("statefile");
        if (stateFile.empty()) {
            std::cerr << "no file specified\n";
            gds->log(gds, print_level::error, "no state file specified");
            throw(invalidFileName());
        }
        loadStateBinary(gds, stateFile, sMode);
        return;
    }

    if (!std::filesystem::exists(filePath)) {
        gds->log(gds, print_level::error, "file does not exist");
        throw(invalidFileName());
    }

    const std::string ext = convertToLowerCase(filePath.extension().string());

    if (ext == ".xml") {
        loadStateXML(gds, fileName, sMode);
    } else {
        loadStateBinary(gds, fileName, sMode);
    }
}

void loadStateBinary(gridDynSimulation* gds, const std::string& fileName, const solverMode& sMode)
{
    const solverMode& currentMode = gds->getCurrentMode(sMode);
    auto solverInterface = gds->getSolverInterface(currentMode);
    if (!solverInterface) {
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
    if (solverInterface->size() != dsize) {
        if (!solverInterface->isInitialized()) {
            solverInterface->allocate(dsize);
        } else {
            gds->log(gds, print_level::error, "statefile does not match solverMode in size");
            return;
        }
    }
    // TODO(phlpt): Check this index at some point; the right handling is still unclear,
    // might be used for automatic solverMode location  instead of what is done currently.
    unsigned int outputIndex;
    bFile.read(reinterpret_cast<char*>(&outputIndex), sizeof(int));
    bFile.read(reinterpret_cast<char*>(solverInterface->state_data()), sizeof(double) * dsize);
    if (isDynamic(sMode)) {
        bFile.read(reinterpret_cast<char*>(solverInterface->deriv_data()), sizeof(double) * dsize);
    }
}

void loadStateXML(gridDynSimulation* /*gds*/,
                  const std::string& /*fileName*/,
                  const solverMode& /*sMode*/)
{
}

void loadPowerFlow(gridDynSimulation* gds, const std::string& fileName)
{
    const std::filesystem::path filePath(fileName);
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
    pugi::xml_document doc;
    auto res = doc.load_file(fileName.c_str());
    if (!res) {
        throw(fileOperationError("unable to open file " + fileName));
    }
    auto flow = doc.child("PowerFlow");
    auto buses = flow.child("buses");
    auto links = flow.child("links");
    // check to make sure the buscount is correct
    auto countNode = buses.child("count");
    int count = countNode.text().as_int();
    auto busCount = gds->getInt("buscount");
    if (count != busCount) {
        return;
    }
    // check to make sure the link count is correct
    countNode = links.child("count");
    count = countNode.text().as_int();
    auto linkCount = gds->getInt("linkcount");
    if (count != linkCount) {
        return;
    }
    // loop over the buses
    for (auto busNode : buses.children("bus")) {
        auto busIndex = static_cast<index_t>(busNode.child("index").text().as_llong());
        auto val1 = busNode.child("voltage").text().as_double();
        auto val2 = busNode.child("angle").text().as_double();
        auto* bus = gds->getBus(busIndex);
        bus->setVoltageAngle(val1, val2);
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
    const auto& currentMode = gds->getCurrentMode(sMode);
    auto solverInterface = gds->getSolverInterface(currentMode);
    matrixDataSparse<double> matrixData;
    stateData stateDescription(
        gds->getSimulationTime(), solverInterface->state_data(), solverInterface->deriv_data());

    stateDescription.cj = 10000;

    gds->jacobianElements(noInputs, stateDescription, matrixData, noInputLocs, currentMode);

    stringVec stateNames;
    gds->getStateName(stateNames, currentMode);

    count_t dsize = solverInterface->size();

    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(unsigned int));
    for (auto& stN : stateNames) {
        auto stnSize = static_cast<unsigned int>(stN.length());
        bFile.write(reinterpret_cast<char*>(&stnSize), sizeof(int));
        bFile.write(stN.c_str(), stnSize);
    }

    // write the state vector
    bFile.write(reinterpret_cast<char*>(solverInterface->state_data()), dsize * sizeof(double));
    // writing the Jacobian Matrix
    dsize = matrixData.size();
    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(count_t));

    for (index_t elementIndex = 0; elementIndex < dsize; ++elementIndex) {
        const auto elementData = matrixData.element(elementIndex);
        bFile.write(reinterpret_cast<const char*>(&(elementData.row)), sizeof(index_t));
        bFile.write(reinterpret_cast<const char*>(&(elementData.col)), sizeof(index_t));
        bFile.write(reinterpret_cast<const char*>(&(elementData.data)), sizeof(double));
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
    const auto& currentMode = gds->getCurrentMode(sMode);
    auto SolverInterface = gds->getSolverInterface(currentMode);

    matrixDataSparse<double> matrixData;

    stateData stateDescription(gds->getSimulationTime(),
                               SolverInterface->state_data(),
                               SolverInterface->deriv_data());

    stateDescription.cj = 10000;
    gds->jacobianElements(noInputs, stateDescription, matrixData, noInputLocs, currentMode);

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
    dsize = matrixData.size();
    bFile.write(reinterpret_cast<char*>(&dsize), sizeof(count_t));

    for (index_t elementIndex = 0; elementIndex < dsize; ++elementIndex) {
        const auto elementData = matrixData.element(elementIndex);
        bFile.write(reinterpret_cast<const char*>(&(elementData.row)), sizeof(index_t));
        bFile.write(reinterpret_cast<const char*>(&(elementData.col)), sizeof(index_t));
        bFile.write(reinterpret_cast<const char*>(&(elementData.data)), sizeof(double));
    }
}

void saveContingencyOutput(const std::vector<std::shared_ptr<Contingency>>& contList,
                           const std::string& fileName,
                           int count)
{
    if (contList.empty()) {
        return;
    }
    std::ofstream bFile(fileName.c_str(), std::ios::out);
    int index = 0;
    while (!contList[index]) {
        ++index;
    }
    while (!contList[index]->isFinished()) {
        contList[index]->wait();
    }
    const bool simplified = contList[index]->simplifiedOutput;

    bFile << contList[index]->generateHeader() << "\n";

    std::vector<std::shared_ptr<Contingency>> timedoutList;
    int ccnt{0};
    for (const auto& cont : contList) {
        if (!cont) {
            continue;
        }
        while (!cont->isFinished()) {
            auto res = cont->wait_for(std::chrono::milliseconds(5000));
            if (res == std::future_status::timeout) {
                timedoutList.push_back(cont);
                break;
            }
        }
        if (!cont->isFinished()) {
            continue;
        }
        if (simplified) {
            bFile << cont->generateViolationsOutputLine() << "\n";
        } else {
            bFile << cont->generateFullOutputLine() << "\n";
        }

        ++ccnt;
        if (count > 0 && ccnt > count) {
            break;
        }
    }
    if (ccnt < count && !timedoutList.empty()) {
        for (auto& cont : timedoutList) {
            while (!cont->isFinished()) {
                auto res = cont->wait_for(std::chrono::milliseconds(30000));
                if (res == std::future_status::timeout) {
                    std::cout << cont->generateContingencyString()
                              << " ERROR contingency timed out\n";
                    continue;
                }
            }
            if (simplified) {
                bFile << cont->generateViolationsOutputLine() << "\n";
            } else {
                bFile << cont->generateFullOutputLine() << "\n";
            }

            ++ccnt;
            if (count > 0 && ccnt > count) {
                break;
            }
        }
    }
}

}  // namespace griddyn
