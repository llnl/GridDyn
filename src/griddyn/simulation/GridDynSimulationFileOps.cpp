/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridDynSimulationFileOps.h"

#include "../GridBus.h"
#include "../GridDynSimulation.h"
#include "../links/AcLine.h"
#include "../links/AdjustableTransformer.h"
#include "../solvers/SolverInterface.h"
#include "Contingency.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "units/units.hpp"
#include "utilities/Hdf5DataFile.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>
#include <span>
#include <sstream>
#include <string>
#include <utility>
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
            throw(FileOperationError("unable to open file " + fileName));
        }
        return output;
    }

    std::string
        formatCsvBusRecord(int areaNumber, int busNumber, const GridBus& bus, double basePower)
    {
        std::ostringstream output;
        output << areaNumber << ", " << busNumber << ", " << bus.getUserID() << ", "
               << std::quoted(bus.getName()) << ", " << std::fixed << std::setw(7)
               << std::setprecision(6) << bus.getVoltage() << ", " << std::showpos << std::setw(8)
               << std::setprecision(4) << degreesFromRadians(bus.getAngle()) << std::noshowpos
               << ", " << std::setw(7) << std::setprecision(5)
               << bus.getGenerationReal() * basePower << ", " << std::setw(7)
               << std::setprecision(5) << bus.getGenerationReactive() * basePower << ", "
               << std::setw(7) << std::setprecision(5) << bus.getLoadReal() * basePower << ", "
               << std::setw(7) << std::setprecision(5) << bus.getLoadReactive() * basePower << ", "
               << std::setw(7) << std::setprecision(5) << bus.getLinkReal() * basePower << ", "
               << std::setw(7) << std::setprecision(5) << bus.getLinkReactive() * basePower << ", "
               << std::setw(7) << std::setprecision(5)
               << (bus.getGenerationReal() + bus.getLoadReal() + bus.getLinkReal()) * basePower
               << ", " << std::setw(7) << std::setprecision(5)
               << (bus.getGenerationReactive() + bus.getLoadReactive() + bus.getLinkReactive()) *
                basePower;
        return output.str();
    }

    std::string formatTxtBusRecord(int areaNumber, const GridBus& bus, double basePower)
    {
        std::ostringstream output;
        output << areaNumber << "\t\t " << bus.getUserID() << "\t\t" << std::quoted(bus.getName())
               << "\t " << std::fixed << std::setw(7) << std::setprecision(6) << bus.getVoltage()
               << "\t " << std::showpos << std::setw(8) << std::setprecision(4)
               << degreesFromRadians(bus.getAngle()) << std::noshowpos << "\t " << std::setw(7)
               << std::setprecision(5) << bus.getGenerationReal() * basePower << "\t "
               << std::setw(7) << std::setprecision(5) << bus.getGenerationReactive() * basePower
               << "\t " << std::setw(7) << std::setprecision(5) << bus.getLoadReal() * basePower
               << "\t " << std::setw(7) << std::setprecision(5) << bus.getLoadReactive() * basePower
               << "\t " << std::setw(7) << std::setprecision(5) << bus.getLinkReal() * basePower
               << "\t " << std::setw(7) << std::setprecision(5)
               << bus.getLinkReactive() * basePower;
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

    std::string formatTxtGridAreaRecord(int areaNumber,
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

void savePowerFlow(GridDynSimulation* gds, const std::string& fileName)
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

void savePowerFlowCSV(GridDynSimulation* gds, const std::string& fileName)
{
    auto output = makeOutputFile(fileName);
    const double basePower = gds->get("basepower");
    output << std::fixed << "basepower=" << basePower << '\n';
    output
        << "\"GridArea #\",\"Bus #\",\"Bus ID\",\"Bus "
           "name\",\"voltage(pu)\",\"angle(deg)\",\"Pgen(MW)\",\"Qgen(MVAr)\",\"Pload(MW)\",\"Qload(MVAr)\","
           "\"Plink(MW)\",\"Qlink(MVAr)\",\"PResid(MW)\",\"QResid(MVAr)\"\n";
    index_t areaIndex = 0;
    const auto* area = gds->getGridArea(areaIndex);
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
        area = gds->getGridArea(areaIndex);
    }

    index_t busIndex = 0;
    auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        bus->updateLocalCache();
        output << formatCsvBusRecord(1, bus->locIndex + 1, *bus, basePower) << '\n';
        ++busIndex;
        bus = gds->getBus(busIndex);
    }
    gds->log(gds, PrintLevel::NORMAL, "saving csv powerflow to " + fileName);
}

void savePowerFlowTXT(GridDynSimulation* gds, const std::string& fileName)
{
    auto output = makeOutputFile(fileName);
    const double basePower = gds->get("basepower");
    output << gds->getName() << " basepower=" << std::fixed << basePower << '\n';
    output << "Simulation " << gds->getInt("totalbuscount") << " buses "
           << gds->getInt("totallinkcount") << " lines\n";

    output << "===============BUS INFORMATION=====================\n";
    output
        << "GridArea#\tBus#\tBus "
           "name\t\t\t\tvoltage(pu)\tangle(deg)\tPgen(MW)\tQgen(MW)\tPload(MW)\tQload(MW)\tPlink(MW)\tQlink(MW)\n";
    index_t areaIndex = 0;
    const auto* area = gds->getGridArea(areaIndex);
    while (area != nullptr) {
        index_t busIndex = 0;
        const auto* bus = area->getBus(busIndex);
        while (bus != nullptr) {
            output << formatTxtBusRecord(area->getUserID(), *bus, basePower) << '\n';
            ++busIndex;
            bus = area->getBus(busIndex);
        }
        ++areaIndex;
        area = gds->getGridArea(areaIndex);
    }

    index_t busIndex = 0;
    const auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        output << formatTxtBusRecord(1, *bus, basePower) << '\n';
        ++busIndex;
        bus = gds->getBus(busIndex);
    }
    output << "===============LINE INFORMATION=====================\n";
    output
        << "GridArea#\tLine #\tLine Name\t\t\t\t\tfrom\tto\t\tP1_2\t\tQ1_2\t\tP2_1\t\tQ2_1\t\tLoss\n";
    areaIndex = 0;
    area = gds->getGridArea(areaIndex);
    while (area != nullptr) {
        index_t linkIndex = 0;
        const auto* link = area->getLink(linkIndex);
        while (link != nullptr) {
            output << formatTxtLinkRecord(area->getUserID(), *link, basePower) << '\n';
            ++linkIndex;
            link = area->getLink(linkIndex);
        }
        ++areaIndex;
        area = gds->getGridArea(areaIndex);
    }

    index_t linkIndex = 0;
    const auto* link = gds->getLink(linkIndex);
    while (link != nullptr) {
        output << formatTxtLinkRecord(1, *link, basePower) << '\n';
        ++linkIndex;
        link = gds->getLink(linkIndex);
    }

    output << "===============AREA INFORMATION=====================\n";
    output
        << "GridArea#\tGridArea Name\t\t\t\tGen Real\t Gen Reactive\t GridLoad Real\t GridLoad Reactive\t Loss\t Export\n";
    areaIndex = 0;
    area = gds->getGridArea(areaIndex);
    while (area != nullptr) {
        output << formatTxtGridAreaRecord(area->getUserID(),
                                          area->getName(),
                                          area->getGenerationReal() * basePower,
                                          area->getGenerationReactive() * basePower,
                                          area->getLoadReal() * basePower,
                                          area->getLoadReactive() * basePower,
                                          area->getLoss() * basePower)
               << '\n';
        ++areaIndex;
        area = gds->getGridArea(areaIndex);
    }

    output << formatTxtGridAreaRecord(1,
                                      gds->getName(),
                                      gds->getGenerationReal() * basePower,
                                      gds->getGenerationReactive() * basePower,
                                      gds->getLoadReal() * basePower,
                                      gds->getLoadReactive() * basePower,
                                      gds->getLoss() * basePower)
           << '\n';
    gds->log(gds, PrintLevel::NORMAL, "saving txt powerflow to " + fileName);
}

// static const double deflim2 = -99999.99;

/*
Columns  1- 4   Bus number (I) *
Columns  6-17   Name (A) (left justify) *
Columns 19-20   GridLoad flow area number (I) Don't use zero! *
Columns 21-23   Loss zone number (I)
Columns 25-26   Type (I) *
0 - Unregulated (load, PQ)
1 - Hold MVAR generation within voltage limits, (PQ)
2 - Hold voltage within VAR limits (gen, PV)
3 - Hold voltage and angle (swing, V-Theta) (must always
have one)
Columns 28-33   Final voltage, pu (F) *
Columns 34-40   Final angle, degrees (F) *
Columns 41-49   GridLoad MW (F) *
Columns 50-58   GridLoad MVAR (F) *
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
static void cdfBusPrint(std::ostream& output, int areaNumber, const GridBus& bus)
{
    const auto type = static_cast<int>(bus.getType());
    output << std::setw(4) << bus.getUserID() << ' ' << std::left << std::setw(12) << bus.getName()
           << std::right << ' ' << std::setw(2) << areaNumber << std::setw(3) << bus.getInt("zone")
           << ' ' << std::setw(2) << type << ' ' << std::fixed << std::setw(6)
           << std::setprecision(4) << bus.getVoltage() << std::setw(7) << std::setprecision(2)
           << degreesFromRadians(bus.getAngle());

    const double realPower = bus.get("p", units::MW);
    const double reactivePower = bus.get("q", units::MW);
    const double generatedRealPower = -bus.get("general", units::MW);
    const double generatedReactivePower = -bus.get("genreactive", units::MW);

    output << std::setw(9) << std::setprecision(2) << realPower << std::setw(9)
           << std::setprecision(2) << reactivePower << std::setw(9) << std::setprecision(2)
           << generatedRealPower << std::setw(8) << std::setprecision(2) << generatedReactivePower;

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
Columns 11-12   GridLoad flow area (I)
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

static void cdfLinkPrint(std::ostream& output, int areaNumber, AcLine* link)
{
    if (link == nullptr) {
        return;
    }

    int type = 0;
    int controlBus = 0;
    double maxVal = 0.0;
    double minVal = 0.0;
    double minAdj = 0.0;
    double maxAdj = 0.0;
    double stepSize = 0.0;
    if (dynamic_cast<links::AdjustableTransformer*>(link) != nullptr) {
        auto* adjustableLink = static_cast<links::AdjustableTransformer*>(link);
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
           << static_cast<int>(ratingB) << ' ' << std::setw(5) << static_cast<int>(emergencyRating)
           << ' ' << std::setw(4) << controlBus << ' ' << std::setw(1) << 0 << "  ";
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
            output << std::setw(6) << std::setprecision(4) << link->getTap() << ' ' << std::setw(6)
                   << std::setprecision(1) << 0.0 << ' ' << std::setw(6) << std::setprecision(4)
                   << minAdj << ' ' << std::setw(6) << std::setprecision(4) << maxAdj
                   << std::setw(7) << std::setprecision(5) << stepSize << ' ' << std::setw(6)
                   << std::setprecision(4) << minVal << ' ' << std::setw(6) << std::setprecision(4)
                   << maxVal << '\n';
            break;
        case 4:
            output << std::setw(6) << std::setprecision(4) << link->getTap() << std::setw(7)
                   << std::setprecision(2) << degreesFromRadians(link->getTapAngle())
                   << std::setw(7) << std::setprecision(4) << minAdj << std::setw(7)
                   << std::setprecision(2) << maxAdj << ' ' << std::setw(6) << std::setprecision(2)
                   << stepSize << std::setw(7) << std::setprecision(1) << minVal << std::setw(7)
                   << std::setprecision(1) << maxVal << '\n';
            break;
        default:
            output << "0.0       0.0 0.0    0.0     0.0    0.0   0.0\n";
            break;
    }
}

void savePowerFlowCdf(GridDynSimulation* gds, const std::string& fileName)
{
    auto output = makeOutputFile(fileName);
    const double basePower = gds->get("basepower");
    // Title Data
    output << " 0 /0 /0  " << std::setw(20) << std::right
           << ("GridDyn " GRIDDYN_VERSION_STRING_SHORT) << ' ' << std::setw(5)
           << static_cast<int>(basePower) << " 2016  " << std::setw(27) << gds->getName() << '\n';

    // Bus Data
    output << "BUS DATA FOLLOWS\n";
    index_t busIndex = 0;
    auto* bus = gds->getBus(busIndex);
    while (bus != nullptr) {
        cdfBusPrint(output, 1, *bus);
        ++busIndex;
        bus = gds->getBus(busIndex);
    }

    auto* area = gds->getGridArea(0);

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
        area = gds->getGridArea(areaIndex);
    }
    output << "-999\n";

    // Line Data
    output << "BRANCH DATA FOLLOWS\n";
    index_t linkIndex = 0;
    auto* link = gds->getLink(linkIndex);
    while (link != nullptr) {
        cdfLinkPrint(output, 1, static_cast<AcLine*>(link));
        ++linkIndex;
        link = gds->getLink(linkIndex);
    }
    areaIndex = 0;
    area = gds->getGridArea(areaIndex);
    while (area != nullptr) {
        linkIndex = 0;
        link = area->getLink(linkIndex);
        while (link != nullptr) {
            cdfLinkPrint(output, area->getUserID(), static_cast<AcLine*>(link));
            ++linkIndex;
            link = area->getLink(linkIndex);
        }
        ++areaIndex;
        area = gds->getGridArea(areaIndex);
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

void savePowerFlowXML(GridDynSimulation* gds, const std::string& fileName)
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
        throw(FileOperationError("unable to open file " + fileName));
    }
}

void saveBusData(GridDynSimulation* gds, const std::string& fileName)
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
        out << ", " << generatedRealPower[entryIndex] << ", " << generatedReactivePower[entryIndex]
            << ", ";
        out << loadRealPower[entryIndex] << ", " << loadReactivePower[entryIndex] << "\n";
    }
}

void saveLineData(GridDynSimulation* gds, const std::string& fileName)
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
        out << ", " << reverseRealPower[entryIndex] << ", " << reverseReactivePower[entryIndex]
            << ", ";
        out << loss[entryIndex] << "\n";
    }
}

void savePowerFlowBinary(GridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void saveState(GridDynSimulation* gds,
               const std::string& fileName,
               const SolverMode& mode,
               bool append)
{
    saveStateBinary(gds, fileName, mode, append);
}

namespace {
    using utilities::Hdf5DataFile;
    using utilities::Hdf5RecordInfo;
    using utilities::Hdf5RecordType;
    using utilities::Hdf5RecordView;
    constexpr std::uint32_t dynamicFlag = 1U, differentialFlag = 2U, algebraicFlag = 4U;
    constexpr std::uint32_t localFlag = 8U, extendedFlag = 16U, parameterFlag = 32U;
    constexpr std::uint32_t snapshotSource = 0U, diagnosticSource = 1U;

    std::uint32_t stateModeFlags(const SolverMode& mode)
    {
        return (mode.dynamic ? dynamicFlag : 0U) | (mode.differential ? differentialFlag : 0U) |
            (mode.algebraic ? algebraicFlag : 0U) | (mode.local ? localFlag : 0U) |
            (mode.extended_state ? extendedFlag : 0U) | (mode.parameters ? parameterFlag : 0U);
    }
    Hdf5RecordInfo recordInfo(CoreTime time,
                              std::uint32_t source,
                              std::uint32_t code,
                              std::uint64_t index,
                              const SolverMode& mode)
    {
        Hdf5RecordInfo info;
        info.time_ns = time.to_ns().count();
        info.source = source;
        info.code = code;
        info.index = index;
        info.flags = stateModeFlags(mode);
        info.approx = static_cast<std::uint32_t>(mode.approx.to_ulong());
        info.key = mode.offsetIndex == kNullLocation ? utilities::hdf5NoIndex :
                                                       static_cast<std::uint64_t>(mode.offsetIndex);
        info.paired_key = mode.pairedOffsetIndex == kNullLocation ?
            utilities::hdf5NoIndex :
            static_cast<std::uint64_t>(mode.pairedOffsetIndex);
        return info;
    }
    SolverMode recordedMode(const Hdf5RecordInfo& info)
    {
        if ((info.flags & ~63U) != 0 || info.key > static_cast<std::uint64_t>(kIndexMax) ||
            (info.paired_key != utilities::hdf5NoIndex &&
             info.paired_key > static_cast<std::uint64_t>(kIndexMax))) {
            throw FileOperationError("invalid HDF5 solver mode");
        }
        SolverMode mode(static_cast<index_t>(info.key));
        mode.dynamic = (info.flags & dynamicFlag) != 0;
        mode.differential = (info.flags & differentialFlag) != 0;
        mode.algebraic = (info.flags & algebraicFlag) != 0;
        mode.local = (info.flags & localFlag) != 0;
        mode.extended_state = (info.flags & extendedFlag) != 0;
        mode.parameters = (info.flags & parameterFlag) != 0;
        mode.approx = info.approx;
        mode.pairedOffsetIndex = info.paired_key == utilities::hdf5NoIndex ?
            kNullLocation :
            static_cast<index_t>(info.paired_key);
        return mode;
    }
    Hdf5RecordView vectorRecord(CoreTime time,
                                std::uint32_t source,
                                std::uint32_t code,
                                std::uint64_t index,
                                const SolverMode& mode,
                                std::uint64_t count,
                                const double* data)
    {
        if ((count > 0 && data == nullptr) ||
            count > (std::numeric_limits<std::size_t>::max)() / sizeof(double)) {
            throw FileOperationError("HDF5 vector data is unavailable or too large");
        }
        Hdf5RecordView view;
        view.info = recordInfo(time, source, code, index, mode);
        view.values = std::span<const double>(data, static_cast<std::size_t>(count));
        return view;
    }
    Hdf5RecordView matrixRecord(CoreTime time,
                                std::uint32_t code,
                                std::uint64_t index,
                                const SolverMode& mode,
                                MatrixData<double>& matrix,
                                std::uint64_t rows,
                                std::uint64_t columns,
                                double cj)
    {
        Hdf5RecordView view;
        view.info = recordInfo(time, diagnosticSource, code, index, mode);
        view.info.type = Hdf5RecordType::MATRIX;
        view.info.count = matrix.size();
        view.info.cj = cj;
        if (rows == 0 && matrix.rowLimit() != kIndexMax) {
            rows = matrix.rowLimit();
        }
        if (columns == 0 && matrix.colLimit() != kIndexMax) {
            columns = matrix.colLimit();
        }
        if (rows == 0 || columns == 0) {
            matrix.start();
            std::uint64_t inferredRows = 0, inferredColumns = 0;
            for (std::uint64_t i = 0; i < view.info.count; ++i) {
                const auto entry = matrix.next();
                if (std::cmp_less(entry.row, 0) || std::cmp_less(entry.col, 0)) {
                    throw FileOperationError("negative sparse matrix index");
                }
                inferredRows = (std::max)(inferredRows, static_cast<std::uint64_t>(entry.row) + 1);
                inferredColumns =
                    (std::max)(inferredColumns, static_cast<std::uint64_t>(entry.col) + 1);
            }
            if (rows == 0) {
                rows = inferredRows;
            }
            if (columns == 0) {
                columns = inferredColumns;
            }
        }
        view.info.rows = rows;
        view.info.columns = columns;
        matrix.start();
        view.matrix = [&matrix](std::span<double> values,
                                std::span<std::uint64_t> row,
                                std::span<std::uint64_t> column) {
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto entry = matrix.next();
                if (std::cmp_less(entry.row, 0) || std::cmp_less(entry.col, 0)) {
                    throw FileOperationError("negative sparse matrix index");
                }
                values[i] = entry.data;
                row[i] = entry.row;
                column[i] = entry.col;
            }
        };
        return view;
    }
    template<class Function>
    auto stateFileOperation(Function&& function)
    {
        try {
            return function();
        }
        catch (const std::runtime_error& error) {
            throw FileOperationError(error.what());
        }
    }
}  // namespace

void saveStateBinary(GridDynSimulation* gds,
                     const std::string& fileName,
                     const SolverMode& sMode,
                     bool append)
{
    stateFileOperation([&]() {
        const auto selected = gds->getSolverInterface(gds->getCurrentMode(sMode));
        if (!selected) {
            throw FileOperationError("solver is unavailable");
        }
        const auto mode = selected->getSolverMode();
        const auto path = fileName.empty() ? gds->getString("statefile") : fileName;
        if (path.empty()) {
            throw FileOperationError("no HDF5 state file specified");
        }
        std::vector<Hdf5RecordView> records;
        const auto addSolver = [&](const SolverMode& selected, std::uint64_t block) {
            auto solver = gds->getSolverInterface(selected);
            if (!solver || !solver->isInitialized()) {
                throw FileOperationError("solver must be initialized before saving state");
            }
            records.push_back(vectorRecord(gds->getSimulationTime(),
                                           snapshotSource,
                                           STATE_INFORMATION,
                                           block,
                                           selected,
                                           solver->size(),
                                           solver->stateData()));
            if (hasDifferential(selected)) {
                records.push_back(vectorRecord(gds->getSimulationTime(),
                                               snapshotSource,
                                               DERIVATIVE_INFORMATION,
                                               block,
                                               selected,
                                               solver->size(),
                                               solver->derivData()));
            }
        };
        addSolver(mode, 0);
        if (mode.pairedOffsetIndex != kNullLocation) {
            auto paired = gds->getSolverInterface(SolverMode(mode.pairedOffsetIndex));
            if (!paired) {
                throw FileOperationError("paired solver is unavailable");
            }
            addSolver(paired->getSolverMode(), 1);
        }
        Hdf5DataFile output(path,
                            append ? Hdf5DataFile::OpenMode::APPEND :
                                     Hdf5DataFile::OpenMode::TRUNCATE);
        output.append(records);
    });
}

void writeVector(CoreTime time,
                 std::uint32_t code,
                 std::uint64_t index,
                 const SolverMode& mode,
                 std::uint64_t count,
                 const double* data,
                 const std::string& fileName,
                 bool append)
{
    stateFileOperation([&]() {
        Hdf5DataFile output(fileName,
                            append ? Hdf5DataFile::OpenMode::APPEND :
                                     Hdf5DataFile::OpenMode::TRUNCATE);
        const auto view = vectorRecord(time, diagnosticSource, code, index, mode, count, data);
        output.append(std::span(&view, 1));
    });
}
void writeArray(CoreTime time,
                std::uint32_t code,
                std::uint64_t index,
                const SolverMode& mode,
                MatrixData<double>& matrix,
                const std::string& fileName,
                bool append,
                std::uint64_t rows,
                std::uint64_t columns,
                double cj)
{
    stateFileOperation([&]() {
        Hdf5DataFile output(fileName,
                            append ? Hdf5DataFile::OpenMode::APPEND :
                                     Hdf5DataFile::OpenMode::TRUNCATE);
        const auto view = matrixRecord(time, code, index, mode, matrix, rows, columns, cj);
        output.append(std::span(&view, 1));
    });
}
void writeDiagnosticState(Hdf5DataFile& output,
                          CoreTime time,
                          std::uint64_t index,
                          const SolverMode& mode,
                          std::uint64_t count,
                          const double* state,
                          const double* derivative,
                          const double* residual)
{
    std::vector<Hdf5RecordView> records;
    records.push_back(
        vectorRecord(time, diagnosticSource, STATE_INFORMATION, index, mode, count, state));
    if (derivative != nullptr) {
        records.push_back(vectorRecord(
            time, diagnosticSource, DERIVATIVE_INFORMATION, index, mode, count, derivative));
    }
    if (residual != nullptr) {
        records.push_back(vectorRecord(
            time, diagnosticSource, RESIDUAL_INFORMATION, index, mode, count, residual));
    }
    output.append(records);
}
void writeDiagnosticVector(Hdf5DataFile& output,
                           CoreTime time,
                           std::uint32_t code,
                           std::uint64_t index,
                           const SolverMode& mode,
                           std::uint64_t count,
                           const double* values)
{
    const auto record = vectorRecord(time, diagnosticSource, code, index, mode, count, values);
    output.append(std::span(&record, 1));
}
void writeDiagnosticJacobian(Hdf5DataFile& output,
                             CoreTime time,
                             std::uint64_t index,
                             const SolverMode& mode,
                             MatrixData<double>& matrix,
                             std::uint64_t rows,
                             std::uint64_t columns,
                             double cj)
{
    const auto record = matrixRecord(time, JACOBIAN_DATA, index, mode, matrix, rows, columns, cj);
    output.append(std::span(&record, 1));
}

void loadState(GridDynSimulation* gds, const std::string& fileName, const SolverMode& mode)
{
    loadStateBinary(gds, fileName, mode);
}

BinaryStateData readStateBinary(const std::string& fileName)
{
    return stateFileOperation([&]() {
        Hdf5DataFile input(fileName, Hdf5DataFile::OpenMode::READ_ONLY);
        std::vector<Hdf5RecordInfo> frame;
        std::uint64_t selected = utilities::hdf5NoIndex;
        auto end = input.recordCount();
        bool finished = false;
        while (end > 0 && !finished) {
            const auto start = end > 128 ? end - 128 : 0;
            const auto records = input.readRecords(start, end - start);
            for (auto record = records.rbegin(); record != records.rend(); ++record) {
                if (selected == utilities::hdf5NoIndex && record->source == snapshotSource) {
                    selected = record->frame;
                }
                if (selected != utilities::hdf5NoIndex) {
                    if (record->frame != selected) {
                        finished = true;
                        break;
                    }
                    frame.push_back(*record);
                }
            }
            end = start;
        }
        if (frame.empty()) {
            throw FileOperationError("HDF5 file contains no state snapshots");
        }
        std::reverse(frame.begin(), frame.end());
        BinaryStateData saved;
        bool primary = true;
        for (const auto& record : frame) {
            if (record.source != snapshotSource || record.type != Hdf5RecordType::VECTOR ||
                record.code != STATE_INFORMATION) {
                continue;
            }
            BinaryStateBlock block;
            block.mode = recordedMode(record);
            block.state = input.readValues(record);
            if (hasDifferential(block.mode)) {
                auto derivative = std::find_if(frame.begin(), frame.end(), [&](const auto& other) {
                    return other.source == snapshotSource && other.type == Hdf5RecordType::VECTOR &&
                        other.code == DERIVATIVE_INFORMATION && other.key == record.key;
                });
                if (derivative == frame.end() || derivative->count != record.count ||
                    derivative->time_ns != record.time_ns) {
                    throw FileOperationError("incomplete HDF5 differential state frame");
                }
                block.derivatives = input.readValues(*derivative);
            }
            if (primary) {
                saved.time = CoreTime(std::chrono::nanoseconds(record.time_ns));
                saved.mode = block.mode;
                saved.state = std::move(block.state);
                saved.derivatives = std::move(block.derivatives);
                primary = false;
            } else {
                if (record.time_ns != saved.time.to_ns().count()) {
                    throw FileOperationError("inconsistent state frame time");
                }
                saved.paired.push_back(std::move(block));
            }
        }
        if (primary) {
            throw FileOperationError("HDF5 frame contains no state vector");
        }
        if (saved.mode.pairedOffsetIndex != kNullLocation &&
            std::none_of(saved.paired.begin(), saved.paired.end(), [&](const auto& pair) {
                return pair.mode.offsetIndex == saved.mode.pairedOffsetIndex;
            })) {
            throw FileOperationError("incomplete paired state frame");
        }
        return saved;
    });
}

namespace {
    void applyBinaryState(GridDynSimulation* gds,
                          const SolverMode& mode,
                          const BinaryStateData& saved)
    {
        struct Target {
            std::shared_ptr<SolverInterface> solver;
            const std::vector<double>* state;
            const std::vector<double>* derivative;
        };
        std::vector<Target> targets;
        const auto validate = [&](const SolverMode& targetMode,
                                  const SolverMode& storedMode,
                                  const std::vector<double>& state,
                                  const std::vector<double>& derivative) {
            auto solver = gds->getSolverInterface(targetMode);
            if (!solver || !solver->isInitialized()) {
                throw FileOperationError("solver must be initialized before loading HDF5 state");
            }
            if (stateModeFlags(storedMode) != stateModeFlags(targetMode) ||
                storedMode.approx != targetMode.approx || state.size() != solver->size() ||
                (hasDifferential(targetMode) && derivative.size() != state.size())) {
                throw FileOperationError("HDF5 state does not match the solver mode or size");
            }
            if (!state.empty() &&
                (!solver->stateData() || (hasDifferential(targetMode) && !solver->derivData()))) {
                throw FileOperationError("solver state or derivative data is unavailable");
            }
            targets.push_back({solver, &state, &derivative});
        };
        validate(mode, saved.mode, saved.state, saved.derivatives);
        if (!saved.paired.empty()) {
            if (saved.paired.size() != 1 || mode.pairedOffsetIndex == kNullLocation) {
                throw FileOperationError(
                    "HDF5 paired state does not match the solver configuration");
            }
            auto solver = gds->getSolverInterface(SolverMode(mode.pairedOffsetIndex));
            if (!solver) {
                throw FileOperationError("paired solver is unavailable");
            }
            const auto& pair = saved.paired.front();
            validate(solver->getSolverMode(), pair.mode, pair.state, pair.derivatives);
        }
        // Check every block before modifying any solver array.
        for (const auto& target : targets) {
            std::copy(target.state->begin(), target.state->end(), target.solver->stateData());
            if (hasDifferential(target.solver->getSolverMode())) {
                std::copy(target.derivative->begin(),
                          target.derivative->end(),
                          target.solver->derivData());
            }
        }
    }

}  // namespace

void loadStateBinary(GridDynSimulation* gds, const std::string& fileName, const SolverMode& sMode)
{
    const auto path = fileName.empty() ? gds->getString("statefile") : fileName;
    if (path.empty()) {
        throw FileOperationError("no binary state file specified");
    }
    const auto saved = readStateBinary(path);
    const auto solver = gds->getSolverInterface(gds->getCurrentMode(sMode));
    if (!solver) {
        throw FileOperationError("solver is unavailable");
    }
    applyBinaryState(gds, solver->getSolverMode(), saved);
}

void loadPowerFlow(GridDynSimulation* gds, const std::string& fileName)
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

void loadPowerFlowCdf(GridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void loadPowerFlowCSV(GridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void loadPowerFlowBinary(GridDynSimulation* /*gds*/, const std::string& /*fileName*/) {}

void loadPowerFlowXML(GridDynSimulation* gds, const std::string& fileName)
{
    pugi::xml_document doc;
    auto res = doc.load_file(fileName.c_str());
    if (!res) {
        throw(FileOperationError("unable to open file " + fileName));
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

namespace {
    using CheckpointJson = nlohmann::json;

    std::shared_ptr<SolverInterface> checkpointPowerFlowSolver(GridDynSimulation* gds,
                                                               const SolverMode& mode)
    {
        auto solver = static_cast<const GridDynSimulation*>(gds)->getSolverInterface(mode);
        if (!solver || !gds->getOffsets(mode).offsetsLoaded) {
            throw FileOperationError("power flow must be initialized before using a checkpoint");
        }
        return gds->getSolverInterface(mode);
    }

    stringVec checkpointStateNames(GridDynSimulation* gds, const SolverMode& mode, count_t size)
    {
        stringVec names;
        if (size > 0) {
            gds->getStateName(names, mode);
        }
        names.resize(size);
        return names;
    }
}  // namespace

void savePowerFlowCheckpoint(GridDynSimulation* gds, const std::string& fileName)
{
    const auto mode = gds->getSolverMode("powerflow");
    const auto solver = checkpointPowerFlowSolver(gds, mode);
    const auto count = solver->size();
    std::vector<double> state(count);
    if (count > 0) {
        gds->guessState(gds->getSimulationTime(), state.data(), nullptr, mode);
    }
    const auto names = checkpointStateNames(gds, mode, count);
    CheckpointJson document{{"format", "griddyn-powerflow-checkpoint"},
                            {"version", 1},
                            {"scope", "solution"},
                            {"state", CheckpointJson::array()},
                            {"buses", CheckpointJson::array()}};
    for (count_t index = 0; index < count; ++index) {
        if (!std::isfinite(state[index])) {
            throw FileOperationError("cannot save a non-finite power-flow state");
        }
        document["state"].push_back({{"name", names[index]}, {"value", state[index]}});
    }
    std::vector<GridBus*> buses;
    gds->getBusVector(buses);
    for (const auto* bus : buses) {
        if (!std::isfinite(bus->getVoltage()) || !std::isfinite(bus->getAngle())) {
            throw FileOperationError("cannot save a non-finite bus voltage or angle");
        }
        document["buses"].push_back({{"name", bus->getName()},
                                     {"user_id", bus->getUserID()},
                                     {"voltage_pu", bus->getVoltage()},
                                     {"angle_rad", bus->getAngle()}});
    }
    auto output = makeOutputFile(fileName);
    output << document.dump(2) << '\n';
    output.close();
    if (!output) {
        throw FileOperationError("unable to write file " + fileName);
    }
}

void loadPowerFlowCheckpoint(GridDynSimulation* gds, const std::string& fileName)
{
    std::ifstream input(fileName);
    if (!input.is_open()) {
        throw FileOperationError("unable to open file " + fileName);
    }
    CheckpointJson document;
    try {
        input >> document;
        if (document.at("format") != "griddyn-powerflow-checkpoint" ||
            document.at("version") != 1 || document.at("scope") != "solution") {
            throw FileOperationError("unsupported power-flow checkpoint format or version");
        }
        const auto mode = gds->getSolverMode("powerflow");
        const auto solver = checkpointPowerFlowSolver(gds, mode);
        const auto count = solver->size();
        const auto names = checkpointStateNames(gds, mode, count);
        const auto& savedState = document.at("state");
        const auto& savedBuses = document.at("buses");
        std::vector<GridBus*> buses;
        gds->getBusVector(buses);
        if (!savedState.is_array() || savedState.size() != count || !savedBuses.is_array() ||
            savedBuses.size() != buses.size()) {
            throw FileOperationError("power-flow checkpoint does not match the model layout");
        }
        std::vector<double> values;
        values.reserve(count);
        for (count_t index = 0; index < count; ++index) {
            const auto& entry = savedState.at(index);
            if (entry.at("name").get<std::string>() != names[index]) {
                throw FileOperationError("power-flow checkpoint state layout does not match");
            }
            const auto value = entry.at("value").get<double>();
            if (!std::isfinite(value)) {
                throw FileOperationError("invalid power-flow checkpoint state value");
            }
            values.push_back(value);
        }
        std::vector<std::pair<double, double>> busValues;
        busValues.reserve(buses.size());
        for (std::size_t index = 0; index < buses.size(); ++index) {
            const auto& entry = savedBuses.at(index);
            // Automatically assigned user IDs can differ between two loads of the same case.
            if (entry.at("name").get<std::string>() != buses[index]->getName()) {
                throw FileOperationError("power-flow checkpoint buses do not match");
            }
            const auto voltage = entry.at("voltage_pu").get<double>();
            const auto angle = entry.at("angle_rad").get<double>();
            if (!std::isfinite(voltage) || !std::isfinite(angle)) {
                throw FileOperationError("invalid power-flow checkpoint bus value");
            }
            busValues.emplace_back(voltage, angle);
        }
        if (count > 0) {
            std::copy(values.begin(), values.end(), solver->stateData());
            gds->setState(gds->getSimulationTime(), values.data(), nullptr, mode);
        }
        for (std::size_t index = 0; index < buses.size(); ++index) {
            buses[index]->setVoltageAngle(busValues[index].first, busValues[index].second);
        }
        // Explicit bus values also cover fixed-voltage and fixed-angle buses.
        // Keep the solver vector in sync if those values were edited.
        if (count > 0) {
            gds->guessState(gds->getSimulationTime(), solver->stateData(), nullptr, mode);
            gds->setState(gds->getSimulationTime(), solver->stateData(), nullptr, mode);
        }
        gds->updateLocalCache();
    }
    catch (const CheckpointJson::exception& exc) {
        throw FileOperationError("invalid power-flow checkpoint " + fileName + ": " + exc.what());
    }
}

void captureJacState(GridDynSimulation* gds, const std::string& fileName, const SolverMode& sMode)
{
    const auto& currentMode = gds->getCurrentMode(sMode);
    auto solverInterface = gds->getSolverInterface(currentMode);
    if (!solverInterface || !solverInterface->isInitialized()) {
        throw FileOperationError("solver must be initialized before capturing a Jacobian");
    }
    MatrixDataSparse<double> matrixData;
    StateData stateDescription(gds->getSimulationTime(),
                               solverInterface->stateData(),
                               solverInterface->derivData());

    stateDescription.cj = 10000;

    gds->jacobianElements(noInputs, stateDescription, matrixData, noInputLocs, currentMode);

    stringVec stateNames;
    gds->getStateName(stateNames, currentMode);

    stateNames.resize(solverInterface->size());
    stateFileOperation([&]() {
        Hdf5DataFile output(fileName, Hdf5DataFile::OpenMode::TRUNCATE);
        auto state = vectorRecord(gds->getSimulationTime(),
                                  diagnosticSource,
                                  STATE_INFORMATION,
                                  0,
                                  currentMode,
                                  solverInterface->size(),
                                  solverInterface->stateData());
        auto matrix = matrixRecord(gds->getSimulationTime(),
                                   JACOBIAN_DATA,
                                   0,
                                   currentMode,
                                   matrixData,
                                   solverInterface->size(),
                                   solverInterface->size(),
                                   stateDescription.cj);
        Hdf5RecordView names;
        names.info = recordInfo(gds->getSimulationTime(), diagnosticSource, 0, 0, currentMode);
        names.info.type = Hdf5RecordType::NAMES;
        names.names = stateNames;
        const std::array records{state, matrix, names};
        output.append(records);
    });
}

void saveJacobian(GridDynSimulation* gds, const std::string& fileName, const SolverMode& sMode)
{
    const auto& currentMode = gds->getCurrentMode(sMode);
    auto solverInterface = gds->getSolverInterface(currentMode);
    if (!solverInterface || !solverInterface->isInitialized()) {
        throw FileOperationError("solver must be initialized before saving a Jacobian");
    }

    MatrixDataSparse<double> matrixData;

    StateData stateDescription(gds->getSimulationTime(),
                               solverInterface->stateData(),
                               solverInterface->derivData());

    stateDescription.cj = 10000;
    gds->jacobianElements(noInputs, stateDescription, matrixData, noInputLocs, currentMode);

    stringVec stateNames;
    gds->getStateName(stateNames, currentMode);

    stateNames.resize(solverInterface->size());
    stateFileOperation([&]() {
        Hdf5DataFile output(fileName, Hdf5DataFile::OpenMode::TRUNCATE);
        auto matrix = matrixRecord(gds->getSimulationTime(),
                                   JACOBIAN_DATA,
                                   0,
                                   currentMode,
                                   matrixData,
                                   solverInterface->size(),
                                   solverInterface->size(),
                                   stateDescription.cj);
        Hdf5RecordView names;
        names.info = recordInfo(gds->getSimulationTime(), diagnosticSource, 0, 0, currentMode);
        names.info.type = Hdf5RecordType::NAMES;
        names.names = stateNames;
        const std::array records{matrix, names};
        output.append(records);
    });
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
