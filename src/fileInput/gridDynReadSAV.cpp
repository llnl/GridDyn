/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/simulation/GridSimulation.h"
#include "sqlite3.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using units::MVAR;
using units::MW;

namespace griddyn {
namespace {

constexpr int pslfSchemaVersion = 4;

[[noreturn]] void savError(const std::string& fileName, const std::string& detail)
{
    throw InvalidParameterValue("PSLF SQLite .sav '" + fileName + "': " + detail);
}

class SqliteDatabase {
  public:
    SqliteDatabase(const std::string& fileName, const std::string& displayName): m_fileName(fileName)
    {
        const auto result = sqlite3_open_v2(fileName.c_str(), &m_database, SQLITE_OPEN_READONLY, nullptr);
        if ((result != SQLITE_OK) || (m_database == nullptr)) {
            const auto message = (m_database != nullptr) ? std::string(sqlite3_errmsg(m_database)) :
                                                            std::string("unable to open database");
            if (m_database != nullptr) {
                sqlite3_close(m_database);
                m_database = nullptr;
            }
            savError(displayName, "file is not a readable SQLite database (" + message + ")");
        }
    }

    ~SqliteDatabase()
    {
        if (m_database != nullptr) {
            sqlite3_close(m_database);
        }
    }

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    sqlite3* get() const { return m_database; }
    const std::string& fileName() const { return m_fileName; }

  private:
    sqlite3* m_database = nullptr;
    std::string m_fileName;
};

class SqliteStatement {
  public:
    SqliteStatement(sqlite3* database, const std::string& sql, const std::string& fileName):
        m_database(database)
    {
        const auto result = sqlite3_prepare_v2(database, sql.c_str(), -1, &m_statement, nullptr);
        if (result != SQLITE_OK) {
            savError(fileName, "SQLite query could not be prepared: " + std::string(sqlite3_errmsg(database)));
        }
    }

    ~SqliteStatement()
    {
        if (m_statement != nullptr) {
            sqlite3_finalize(m_statement);
        }
    }

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    sqlite3_stmt* get() const { return m_statement; }
    sqlite3* database() const { return m_database; }

  private:
    sqlite3* m_database = nullptr;
    sqlite3_stmt* m_statement = nullptr;
};

void checkStep(const SqliteStatement& statement, const std::string& fileName)
{
    const auto result = sqlite3_step(statement.get());
    if (result != SQLITE_DONE) {
        savError(fileName, "SQLite query failed: " + std::string(sqlite3_errmsg(statement.database())));
    }
}

std::string columnText(sqlite3_stmt* statement, int column)
{
    const auto* text = sqlite3_column_text(statement, column);
    return (text == nullptr) ? std::string{} : std::string(reinterpret_cast<const char*>(text));
}

double columnDouble(sqlite3_stmt* statement, int column, const std::string& fileName, std::string_view field)
{
    const auto type = sqlite3_column_type(statement, column);
    if ((type != SQLITE_INTEGER) && (type != SQLITE_FLOAT)) {
        savError(fileName, "column '" + std::string(field) + "' is not numeric");
    }
    const auto value = sqlite3_column_double(statement, column);
    if (!std::isfinite(value)) {
        savError(fileName, "column '" + std::string(field) + "' contains a non-finite value");
    }
    return value;
}

int columnInteger(sqlite3_stmt* statement, int column, const std::string& fileName, std::string_view field)
{
    const auto type = sqlite3_column_type(statement, column);
    if (type == SQLITE_INTEGER) {
        const auto value = sqlite3_column_int64(statement, column);
        if ((value < std::numeric_limits<int>::min()) || (value > std::numeric_limits<int>::max())) {
            savError(fileName, "column '" + std::string(field) + "' is outside the supported integer range");
        }
        return static_cast<int>(value);
    }
    if (type == SQLITE_FLOAT) {
        const auto value = sqlite3_column_double(statement, column);
        if (!std::isfinite(value) || (std::trunc(value) != value) ||
            (value < static_cast<double>(std::numeric_limits<int>::min())) ||
            (value > static_cast<double>(std::numeric_limits<int>::max()))) {
            savError(fileName, "column '" + std::string(field) + "' is not an integer");
        }
        return static_cast<int>(value);
    }
    savError(fileName, "column '" + std::string(field) + "' is not an integer");
}

bool tableExists(sqlite3* database, const std::string& fileName, std::string_view tableName)
{
    SqliteStatement statement(
        database,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1",
        fileName);
    sqlite3_bind_text(statement.get(), 1, tableName.data(), static_cast<int>(tableName.size()), SQLITE_TRANSIENT);
    const auto result = sqlite3_step(statement.get());
    if ((result != SQLITE_ROW) && (result != SQLITE_DONE)) {
        savError(fileName, "could not inspect the SQLite table catalog");
    }
    return result == SQLITE_ROW;
}

void requireColumns(sqlite3* database,
                    const std::string& fileName,
                    std::string_view tableName,
                    std::initializer_list<std::string_view> requiredColumns)
{
    if (!tableExists(database, fileName, tableName)) {
        savError(fileName, "required PSLF table '" + std::string(tableName) + "' is missing");
    }

    std::unordered_set<std::string> columns;
    const std::string sql = "PRAGMA table_info(" + std::string(tableName) + ")";
    SqliteStatement statement(database, sql, fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not inspect columns for table '" + std::string(tableName) + "'");
        }
        columns.emplace(columnText(statement.get(), 1));
    }

    for (const auto requiredColumn : requiredColumns) {
        if (!columns.contains(std::string(requiredColumn))) {
            savError(fileName,
                     "PSLF table '" + std::string(tableName) + "' is missing required column '" +
                         std::string(requiredColumn) + "'");
        }
    }
}

struct CaseData {
    double basePower = 0.0;
    int busCount = 0;
    int lineCount = 0;
    int generatorCount = 0;
    int transformerCount = 0;
    int loadCount = 0;
    int shuntCount = 0;
    int areaCount = 0;
    int zoneCount = 0;
};

struct AreaData {
    int index = -1;
    int number = 0;
    std::string name;
};

struct BusData {
    int index = -1;
    int externalNumber = 0;
    std::string name;
    double baseVoltage = 0.0;
    int type = 1;
    int area = 0;
    int zone = 0;
    double scheduledVoltage = 1.0;
    double maxVoltage = 0.0;
    double minVoltage = 0.0;
    double voltage = 1.0;
    double angle = 0.0;
};

struct GeneratorData {
    int index = -1;
    int bus = -1;
    std::string id;
    int status = 1;
    double machineBase = 0.0;
    double realPower = 0.0;
    double reactivePower = 0.0;
    double maxReactivePower = 0.0;
    double minReactivePower = 0.0;
    double maxRealPower = 0.0;
    double minRealPower = 0.0;
    double voltageTarget = 0.0;
};

struct LoadData {
    int index = -1;
    int bus = -1;
    std::string id;
    int status = 1;
    double realPower = 0.0;
    double reactivePower = 0.0;
    double realCurrent = 0.0;
    double reactiveCurrent = 0.0;
    double conductance = 0.0;
    double susceptance = 0.0;
};

struct LineData {
    int index = -1;
    int from = -1;
    int to = -1;
    std::string circuit;
    int status = 1;
    double resistance = 0.0;
    double reactance = 0.0;
    double susceptance = 0.0;
    double rating = 0.0;
};

struct TransformerData {
    int index = -1;
    int from = -1;
    int to = -1;
    std::string circuit;
    int status = 1;
    int type = 1;
    int regulatingBus = -1;
    double transformerBase = 0.0;
    int tertiaryBus = 0;
    double resistance = 0.0;
    double reactance = 0.0;
    double tertiaryResistance = 0.0;
    double tertiaryReactance = 0.0;
    double tap = 1.0;
    double rating = 0.0;
    double tapMax = 0.0;
    double tapMin = 0.0;
    double voltageMax = 0.0;
    double voltageMin = 0.0;
    double tapStep = 0.0;
};

struct ShuntData {
    int index = -1;
    int bus = -1;
    std::string id;
    int status = 1;
    double conductance = 0.0;
    double susceptance = 0.0;
};

template<class Record>
void requireContiguousIndexes(const std::vector<Record>& records,
                              const std::string& fileName,
                              std::string_view tableName)
{
    std::vector<int> indexes;
    indexes.reserve(records.size());
    for (const auto& record : records) {
        indexes.push_back(record.index);
    }
    std::sort(indexes.begin(), indexes.end());
    for (size_t index = 0; index < indexes.size(); ++index) {
        if (indexes[index] != static_cast<int>(index)) {
            savError(fileName,
                     "PSLF table '" + std::string(tableName) +
                         "' does not contain the expected zero-based contiguous _idx values");
        }
    }
}

void requireCount(size_t actual,
                  int expected,
                  const std::string& fileName,
                  std::string_view tableName,
                  std::string_view countName)
{
    if ((expected < 0) || (actual != static_cast<size_t>(expected))) {
        savError(fileName,
                 "PSLF table '" + std::string(tableName) + "' has " + std::to_string(actual) +
                     " rows, but casepar." + std::string(countName) + " declares " +
                     std::to_string(expected));
    }
}

void requireBusReference(int busIndex,
                         size_t busCount,
                         const std::string& fileName,
                         std::string_view tableName)
{
    if ((busIndex < 0) || (static_cast<size_t>(busIndex) >= busCount)) {
        savError(fileName,
                 "PSLF table '" + std::string(tableName) + "' references bus index " +
                     std::to_string(busIndex) + " outside busd");
    }
}

CaseData readCaseData(sqlite3* database, const std::string& fileName)
{
    SqliteStatement statement(
        database,
        "SELECT sbase, nbus, nbrsec, ngen, ntran, nload, nshunt, narea, nzone FROM casepar ORDER BY _idx LIMIT 1",
        fileName);
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        savError(fileName, "casepar does not contain a case parameter row");
    }

    CaseData data;
    data.basePower = columnDouble(statement.get(), 0, fileName, "casepar.sbase");
    data.busCount = columnInteger(statement.get(), 1, fileName, "casepar.nbus");
    data.lineCount = columnInteger(statement.get(), 2, fileName, "casepar.nbrsec");
    data.generatorCount = columnInteger(statement.get(), 3, fileName, "casepar.ngen");
    data.transformerCount = columnInteger(statement.get(), 4, fileName, "casepar.ntran");
    data.loadCount = columnInteger(statement.get(), 5, fileName, "casepar.nload");
    data.shuntCount = columnInteger(statement.get(), 6, fileName, "casepar.nshunt");
    data.areaCount = columnInteger(statement.get(), 7, fileName, "casepar.narea");
    data.zoneCount = columnInteger(statement.get(), 8, fileName, "casepar.nzone");
    if (data.basePower <= 0.0) {
        savError(fileName, "casepar.sbase must be positive");
    }
    return data;
}

void validateFormat(sqlite3* database, const std::string& fileName, CaseData& caseData)
{
    requireColumns(database,
                   fileName,
                   "pslf_database_metadata",
                   {"schema_version", "last_save_client"});
    requireColumns(database, fileName, "casepar", {"_idx", "sbase", "nbus", "nbrsec", "ngen", "ntran", "nload", "nshunt", "narea", "nzone"});
    requireColumns(database,
                   fileName,
                   "busd",
                   {"_idx", "extnum", "busnam", "basekv", "type", "area", "zone", "vsched", "vmax", "vmin"});
    requireColumns(database, fileName, "volt", {"_idx", "vr", "vi", "vm", "va"});
    requireColumns(database, fileName, "gens", {"_idx", "ibgen", "id", "st", "mbase", "pgen", "qgen", "qmax", "qmin", "pmax", "pmin", "vcsched"});
    requireColumns(database, fileName, "load", {"_idx", "lbus", "id", "st", "p", "q", "ip", "iq", "g", "b"});
    requireColumns(database, fileName, "secdd", {"_idx", "ifrom", "ito", "ck", "st", "zsecr", "zsecx", "bsec", "rate0"});
    requireColumns(database,
                   fileName,
                   "tran",
                   {"_idx", "ifrom", "ito", "ck", "st", "type", "kreg", "tbase", "zpsr", "zpsx", "zptr", "zptx", "ztsr", "ztsx", "rate0", "tmax", "tmin", "vtmax", "vtmin", "stepp", "tapp", "midbus_t"});
    requireColumns(database, fileName, "shunt", {"_idx", "ifrom", "id", "st", "g", "b"});
    requireColumns(database, fileName, "area", {"_idx", "arnum", "arname"});
    requireColumns(database, fileName, "zone", {"_idx", "zonum", "zonam"});

    {
        SqliteStatement statement(database,
                                  "SELECT schema_version, last_save_client FROM pslf_database_metadata LIMIT 1",
                                  fileName);
        if (sqlite3_step(statement.get()) != SQLITE_ROW) {
            savError(fileName, "pslf_database_metadata does not contain a metadata row");
        }
        const auto schemaVersion = columnInteger(statement.get(), 0, fileName, "pslf_database_metadata.schema_version");
        if (schemaVersion != pslfSchemaVersion) {
            savError(fileName,
                     "unsupported PSLF SQLite schema version " + std::to_string(schemaVersion) +
                         "; GridDyn supports version " + std::to_string(pslfSchemaVersion));
        }
        auto client = columnText(statement.get(), 1);
        std::transform(client.begin(), client.end(), client.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        if (client.find("PSLF") == std::string::npos) {
            savError(fileName, "metadata does not identify the file as a PSLF save");
        }
    }
    caseData = readCaseData(database, fileName);
}

std::vector<AreaData> readAreas(sqlite3* database, const std::string& fileName)
{
    std::vector<AreaData> areas;
    SqliteStatement statement(database,
                              "SELECT _idx, arnum, arname FROM area ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF area records");
        }
        AreaData area;
        area.index = columnInteger(statement.get(), 0, fileName, "area._idx");
        area.number = columnInteger(statement.get(), 1, fileName, "area.arnum");
        area.name = columnText(statement.get(), 2);
        areas.push_back(std::move(area));
    }
    requireContiguousIndexes(areas, fileName, "area");
    std::unordered_set<int> areaNumbers;
    for (const auto& area : areas) {
        if ((area.number <= 0) || !areaNumbers.emplace(area.number).second) {
            savError(fileName, "area does not contain unique positive arnum values");
        }
    }
    return areas;
}

size_t readZoneCount(sqlite3* database, const std::string& fileName)
{
    SqliteStatement statement(database,
                              "SELECT _idx, zonum FROM zone ORDER BY _idx",
                              fileName);
    std::unordered_set<int> zoneNumbers;
    size_t row = 0;
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF zone records");
        }
        const auto index = columnInteger(statement.get(), 0, fileName, "zone._idx");
        const auto number = columnInteger(statement.get(), 1, fileName, "zone.zonum");
        if ((index != static_cast<int>(row)) || (number <= 0) || !zoneNumbers.emplace(number).second) {
            savError(fileName, "zone does not contain unique zero-based records");
        }
        ++row;
    }
    return row;
}

std::vector<BusData> readBuses(sqlite3* database, const std::string& fileName)
{
    std::vector<BusData> buses;
    SqliteStatement statement(database,
                              "SELECT _idx, extnum, busnam, basekv, type, area, zone, vsched, vmax, vmin FROM busd ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF bus records");
        }
        BusData bus;
        bus.index = columnInteger(statement.get(), 0, fileName, "busd._idx");
        bus.externalNumber = columnInteger(statement.get(), 1, fileName, "busd.extnum");
        bus.name = columnText(statement.get(), 2);
        bus.baseVoltage = columnDouble(statement.get(), 3, fileName, "busd.basekv");
        bus.type = columnInteger(statement.get(), 4, fileName, "busd.type");
        bus.area = columnInteger(statement.get(), 5, fileName, "busd.area");
        bus.zone = columnInteger(statement.get(), 6, fileName, "busd.zone");
        bus.scheduledVoltage = columnDouble(statement.get(), 7, fileName, "busd.vsched");
        bus.maxVoltage = columnDouble(statement.get(), 8, fileName, "busd.vmax");
        bus.minVoltage = columnDouble(statement.get(), 9, fileName, "busd.vmin");
        if ((bus.type < 0) || (bus.type > 4)) {
            savError(fileName, "busd contains unsupported bus type " + std::to_string(bus.type));
        }
        if ((bus.externalNumber <= 0) || (bus.baseVoltage <= 0.0)) {
            savError(fileName, "busd contains an invalid external number or base voltage");
        }
        buses.push_back(std::move(bus));
    }

    std::unordered_set<int> externalNumbers;
    for (const auto& bus : buses) {
        if (!externalNumbers.emplace(bus.externalNumber).second) {
            savError(fileName, "busd contains duplicate external bus number " + std::to_string(bus.externalNumber));
        }
    }
    requireContiguousIndexes(buses, fileName, "busd");
    return buses;
}

void readVoltages(sqlite3* database, const std::string& fileName, std::vector<BusData>& buses)
{
    SqliteStatement statement(database,
                              "SELECT _idx, vr, vi, vm, va FROM volt ORDER BY _idx",
                              fileName);
    size_t row = 0;
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF voltage records");
        }
        const auto index = columnInteger(statement.get(), 0, fileName, "volt._idx");
        if ((index < 0) || (static_cast<size_t>(index) >= buses.size()) || (index != static_cast<int>(row))) {
            savError(fileName, "volt does not align with the zero-based busd indexes");
        }
        const auto realVoltage = columnDouble(statement.get(), 1, fileName, "volt.vr");
        const auto imaginaryVoltage = columnDouble(statement.get(), 2, fileName, "volt.vi");
        const auto magnitude = columnDouble(statement.get(), 3, fileName, "volt.vm");
        const auto savedAngle = columnDouble(statement.get(), 4, fileName, "volt.va");
        const auto rectangularMagnitude = std::hypot(realVoltage, imaginaryVoltage);
        if (rectangularMagnitude > std::numeric_limits<double>::epsilon()) {
            buses[index].voltage = rectangularMagnitude;
            buses[index].angle = std::atan2(imaginaryVoltage, realVoltage);
        } else {
            buses[index].voltage = magnitude;
            buses[index].angle = savedAngle;
        }
        if ((buses[index].voltage < 0.0) || !std::isfinite(buses[index].angle)) {
            savError(fileName, "volt contains an invalid voltage magnitude or angle");
        }
        ++row;
    }
}

std::vector<GeneratorData> readGenerators(sqlite3* database, const std::string& fileName)
{
    std::vector<GeneratorData> generators;
    SqliteStatement statement(database,
                              "SELECT _idx, ibgen, id, st, mbase, pgen, qgen, qmax, qmin, pmax, pmin, vcsched FROM gens ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF generator records");
        }
        GeneratorData generator;
        generator.index = columnInteger(statement.get(), 0, fileName, "gens._idx");
        generator.bus = columnInteger(statement.get(), 1, fileName, "gens.ibgen");
        generator.id = columnText(statement.get(), 2);
        generator.status = columnInteger(statement.get(), 3, fileName, "gens.st");
        generator.machineBase = columnDouble(statement.get(), 4, fileName, "gens.mbase");
        generator.realPower = columnDouble(statement.get(), 5, fileName, "gens.pgen");
        generator.reactivePower = columnDouble(statement.get(), 6, fileName, "gens.qgen");
        generator.maxReactivePower = columnDouble(statement.get(), 7, fileName, "gens.qmax");
        generator.minReactivePower = columnDouble(statement.get(), 8, fileName, "gens.qmin");
        generator.maxRealPower = columnDouble(statement.get(), 9, fileName, "gens.pmax");
        generator.minRealPower = columnDouble(statement.get(), 10, fileName, "gens.pmin");
        generator.voltageTarget = columnDouble(statement.get(), 11, fileName, "gens.vcsched");
        generators.push_back(std::move(generator));
    }
    requireContiguousIndexes(generators, fileName, "gens");
    return generators;
}

std::vector<LoadData> readLoads(sqlite3* database, const std::string& fileName)
{
    std::vector<LoadData> loads;
    SqliteStatement statement(database,
                              "SELECT _idx, lbus, id, st, p, q, ip, iq, g, b FROM load ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF load records");
        }
        LoadData load;
        load.index = columnInteger(statement.get(), 0, fileName, "load._idx");
        load.bus = columnInteger(statement.get(), 1, fileName, "load.lbus");
        load.id = columnText(statement.get(), 2);
        load.status = columnInteger(statement.get(), 3, fileName, "load.st");
        load.realPower = columnDouble(statement.get(), 4, fileName, "load.p");
        load.reactivePower = columnDouble(statement.get(), 5, fileName, "load.q");
        load.realCurrent = columnDouble(statement.get(), 6, fileName, "load.ip");
        load.reactiveCurrent = columnDouble(statement.get(), 7, fileName, "load.iq");
        load.conductance = columnDouble(statement.get(), 8, fileName, "load.g");
        load.susceptance = columnDouble(statement.get(), 9, fileName, "load.b");
        loads.push_back(std::move(load));
    }
    requireContiguousIndexes(loads, fileName, "load");
    return loads;
}

std::vector<LineData> readLines(sqlite3* database, const std::string& fileName)
{
    std::vector<LineData> lines;
    SqliteStatement statement(database,
                              "SELECT _idx, ifrom, ito, ck, st, zsecr, zsecx, bsec, rate0 FROM secdd ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF line records");
        }
        LineData line;
        line.index = columnInteger(statement.get(), 0, fileName, "secdd._idx");
        line.from = columnInteger(statement.get(), 1, fileName, "secdd.ifrom");
        line.to = columnInteger(statement.get(), 2, fileName, "secdd.ito");
        line.circuit = columnText(statement.get(), 3);
        line.status = columnInteger(statement.get(), 4, fileName, "secdd.st");
        line.resistance = columnDouble(statement.get(), 5, fileName, "secdd.zsecr");
        line.reactance = columnDouble(statement.get(), 6, fileName, "secdd.zsecx");
        line.susceptance = columnDouble(statement.get(), 7, fileName, "secdd.bsec");
        line.rating = columnDouble(statement.get(), 8, fileName, "secdd.rate0");
        lines.push_back(std::move(line));
    }
    requireContiguousIndexes(lines, fileName, "secdd");
    return lines;
}

std::vector<TransformerData> readTransformers(sqlite3* database, const std::string& fileName)
{
    std::vector<TransformerData> transformers;
    SqliteStatement statement(database,
                              "SELECT _idx, ifrom, ito, ck, st, type, kreg, tbase, zpsr, zpsx, zptr, zptx, ztsr, ztsx, rate0, tmax, tmin, vtmax, vtmin, stepp, tapp, midbus_t FROM tran ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF transformer records");
        }
        TransformerData transformer;
        transformer.index = columnInteger(statement.get(), 0, fileName, "tran._idx");
        transformer.from = columnInteger(statement.get(), 1, fileName, "tran.ifrom");
        transformer.to = columnInteger(statement.get(), 2, fileName, "tran.ito");
        transformer.circuit = columnText(statement.get(), 3);
        transformer.status = columnInteger(statement.get(), 4, fileName, "tran.st");
        transformer.type = columnInteger(statement.get(), 5, fileName, "tran.type");
        transformer.regulatingBus = columnInteger(statement.get(), 6, fileName, "tran.kreg");
        transformer.transformerBase = columnDouble(statement.get(), 7, fileName, "tran.tbase");
        transformer.resistance = columnDouble(statement.get(), 8, fileName, "tran.zpsr");
        transformer.reactance = columnDouble(statement.get(), 9, fileName, "tran.zpsx");
        transformer.tertiaryResistance = columnDouble(statement.get(), 10, fileName, "tran.zptr");
        transformer.tertiaryReactance = columnDouble(statement.get(), 11, fileName, "tran.zptx");
        const auto secondaryTertiaryResistance = columnDouble(statement.get(), 12, fileName, "tran.ztsr");
        const auto secondaryTertiaryReactance = columnDouble(statement.get(), 13, fileName, "tran.ztsx");
        transformer.rating = columnDouble(statement.get(), 14, fileName, "tran.rate0");
        transformer.tapMax = columnDouble(statement.get(), 15, fileName, "tran.tmax");
        transformer.tapMin = columnDouble(statement.get(), 16, fileName, "tran.tmin");
        transformer.voltageMax = columnDouble(statement.get(), 17, fileName, "tran.vtmax");
        transformer.voltageMin = columnDouble(statement.get(), 18, fileName, "tran.vtmin");
        transformer.tapStep = columnDouble(statement.get(), 19, fileName, "tran.stepp");
        transformer.tap = columnDouble(statement.get(), 20, fileName, "tran.tapp");
        transformer.tertiaryBus = (sqlite3_column_type(statement.get(), 21) == SQLITE_NULL) ?
            0 : columnInteger(statement.get(), 21, fileName, "tran.midbus_t");
        if ((transformer.type != 1) && (transformer.type != 2)) {
            savError(fileName,
                     "unsupported PSLF two-winding transformer type " + std::to_string(transformer.type) +
                         " in record " + std::to_string(transformer.index));
        }
        if (transformer.transformerBase <= 0.0) {
            savError(fileName,
                     "tran.tbase must be positive in transformer record " +
                         std::to_string(transformer.index));
        }
        if (transformer.type == 2) {
            if (transformer.tapMin >= transformer.tapMax) {
                savError(fileName,
                         "tran.tmin must be less than tran.tmax in regulating transformer record " +
                             std::to_string(transformer.index));
            }
            if (transformer.voltageMin >= transformer.voltageMax) {
                savError(fileName,
                         "tran.vtmin must be less than tran.vtmax in regulating transformer record " +
                             std::to_string(transformer.index));
            }
        }
        // PSLF uses 999 as an unused/sentinel impedance value in otherwise ordinary
        // two-winding transformer rows.  Do not mistake that representation for a
        // three-winding transformer.
        const auto isUnusedImpedance = [](double value) {
            return (std::abs(value) <= 1.0e-12) || (std::abs(value - 999.0) <= 1.0e-9);
        };
        if ((transformer.tertiaryBus > 0) ||
            !isUnusedImpedance(transformer.tertiaryResistance) ||
            !isUnusedImpedance(transformer.tertiaryReactance) ||
            !isUnusedImpedance(secondaryTertiaryResistance) ||
            !isUnusedImpedance(secondaryTertiaryReactance)) {
            savError(fileName,
                     "three-winding transformer record " + std::to_string(transformer.index) +
                         " is not supported yet; two-winding transformers are supported");
        }
        if (transformer.tap == 0.0) {
            transformer.tap = 1.0;
        }
        transformers.push_back(std::move(transformer));
    }
    requireContiguousIndexes(transformers, fileName, "tran");
    return transformers;
}

std::vector<ShuntData> readShunts(sqlite3* database, const std::string& fileName)
{
    std::vector<ShuntData> shunts;
    SqliteStatement statement(database,
                              "SELECT _idx, ifrom, id, st, g, b FROM shunt ORDER BY _idx",
                              fileName);
    while (true) {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            savError(fileName, "could not read PSLF fixed shunt records");
        }
        ShuntData shunt;
        shunt.index = columnInteger(statement.get(), 0, fileName, "shunt._idx");
        shunt.bus = columnInteger(statement.get(), 1, fileName, "shunt.ifrom");
        shunt.id = columnText(statement.get(), 2);
        shunt.status = columnInteger(statement.get(), 3, fileName, "shunt.st");
        shunt.conductance = columnDouble(statement.get(), 4, fileName, "shunt.g");
        shunt.susceptance = columnDouble(statement.get(), 5, fileName, "shunt.b");
        shunts.push_back(std::move(shunt));
    }
    requireContiguousIndexes(shunts, fileName, "shunt");
    return shunts;
}

void validateReferences(const std::string& fileName,
                        size_t busCount,
                        const std::vector<GeneratorData>& generators,
                        const std::vector<LoadData>& loads,
                        const std::vector<LineData>& lines,
                        const std::vector<TransformerData>& transformers,
                        const std::vector<ShuntData>& shunts)
{
    for (const auto& generator : generators) {
        requireBusReference(generator.bus, busCount, fileName, "gens");
    }
    for (const auto& load : loads) {
        requireBusReference(load.bus, busCount, fileName, "load");
    }
    for (const auto& line : lines) {
        requireBusReference(line.from, busCount, fileName, "secdd");
        requireBusReference(line.to, busCount, fileName, "secdd");
    }
    for (const auto& transformer : transformers) {
        requireBusReference(transformer.from, busCount, fileName, "tran");
        requireBusReference(transformer.to, busCount, fileName, "tran");
        if (transformer.type == 2) {
            requireBusReference(transformer.regulatingBus, busCount, fileName, "tran.kreg");
        }
    }
    for (const auto& shunt : shunts) {
        requireBusReference(shunt.bus, busCount, fileName, "shunt");
    }
}

std::string prefixedName(const BasicReaderInfo& readerOptions, std::string name)
{
    if (!readerOptions.prefix.empty()) {
        name = readerOptions.prefix + '_' + name;
    }
    return name;
}

CoreObject* getSavLinkParent(CoreObject* parentObject, Link* link)
{
    if ((link == nullptr) || (link->terminalCount() != 2)) {
        return parentObject;
    }
    auto* bus1 = link->getBus(1);
    auto* bus2 = link->getBus(2);
    auto* area1 = (bus1 != nullptr) ? dynamic_cast<GridArea*>(bus1->getParent()) : nullptr;
    auto* area2 = (bus2 != nullptr) ? dynamic_cast<GridArea*>(bus2->getParent()) : nullptr;
    if ((area1 == nullptr) || (area1 != area2)) {
        return parentObject;
    }
    for (auto* area = area1; area != nullptr; area = dynamic_cast<GridArea*>(area->getParent())) {
        if (area == parentObject) {
            return area1;
        }
    }
    return parentObject;
}

void addArea(CoreObject* parentObject,
             const BasicReaderInfo& readerOptions,
             const AreaData& areaData,
             std::unordered_map<int, GridArea*>& areas)
{
    auto name = areaData.name.empty() ? "AREA_" + std::to_string(areaData.number) : areaData.name;
    auto* area = new GridArea(prefixedName(readerOptions, std::move(name)));
    area->setUserID(static_cast<index_t>(areaData.number));
    try {
        parentObject->add(area);
    }
    catch (const ObjectAddFailure&) {
        addToParentWithRename(area, parentObject);
    }
    areas.emplace(areaData.number, area);
}

void addBus(CoreObject* parentObject,
            const BasicReaderInfo& readerOptions,
            const BusData& busData,
            const std::unordered_map<int, GridArea*>& areas,
            std::vector<GridBus*>& buses)
{
    auto name = busData.name.empty() ? "BUS_" + std::to_string(busData.externalNumber) : busData.name;
    auto* bus = new AcBus(prefixedName(readerOptions, std::move(name)));
    bus->setUserID(static_cast<index_t>(busData.externalNumber));
    bus->set("basepower", parentObject->get("basepower"));
    bus->set("basevoltage", busData.baseVoltage, units::kV);
    // PSLF busd type codes are different from the CDF/RAW codes used by
    // several of the other readers: 0 is swing, 1 is PQ, and 2 is PV.
    if (busData.type == 0) {
        bus->set("type", "SLK");
        bus->set("atarget", busData.angle);
    } else if (busData.type == 2) {
        bus->set("type", "PV");
    } else {
        bus->set("type", "PQ");
        if (busData.type == 4) {
            bus->disable();
        }
    }
    if (busData.scheduledVoltage > 0.0) {
        bus->set("vtarget", busData.scheduledVoltage);
    }
    if (busData.maxVoltage > 0.0) {
        bus->set("vmax", busData.maxVoltage);
    }
    if (busData.minVoltage > 0.0) {
        bus->set("vmin", busData.minVoltage);
    }
    bus->setVoltageAngle(busData.voltage, busData.angle);
    if (busData.zone > 0) {
        bus->set("zone", static_cast<double>(busData.zone));
    }

    auto busParent = parentObject;
    if (const auto area = areas.find(busData.area); area != areas.end()) {
        busParent = area->second;
    }
    addToParentWithRename(bus, busParent);
    buses[static_cast<size_t>(busData.index)] = bus;
}

void addGenerator(const BasicReaderInfo& readerOptions,
                  const GeneratorData& generatorData,
                  std::vector<GridBus*>& buses,
                  const std::string& fileName)
{
    auto* bus = buses[static_cast<size_t>(generatorData.bus)];
    auto id = generatorData.id.empty() ? std::to_string(generatorData.index) : generatorData.id;
    // DYD/DYR records attach machine, exciter, governor, and stabilizer models to
    // DynamicGenerator objects.  Keep the lightweight Generator for callers that
    // explicitly requested a power-flow-only import, but make normal SAV imports
    // dynamic-capable so an associated DYD can be loaded afterward.
    Generator* generator = nullptr;
    const auto generatorName = prefixedName(readerOptions, bus->getName() + "_gen_" + id);
    if (readerOptions.checkFlag(ASSUME_POWERFLOW_ONLY)) {
        generator = new Generator(generatorName);
    } else {
        generator = new DynamicGenerator(generatorName);
    }
    generator->set("basepower", bus->get("basepower", units::MW), units::MW);
    generator->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
    // PSLF stores these fields in MW/Mvar.  Convert at the GridDyn object
    // boundary so the internal representation remains puMW.
    generator->set("p", generatorData.realPower, MW);
    generator->set("q", generatorData.reactivePower, MVAR);
    generator->set("qmax", generatorData.maxReactivePower, MVAR);
    generator->set("qmin", generatorData.minReactivePower, MVAR);
    generator->set("pmax", generatorData.maxRealPower, MW);
    generator->set("pmin", generatorData.minRealPower, MW);
    if (generatorData.machineBase > 0.0) {
        generator->set("mbase", generatorData.machineBase, units::MVAR);
    }
    if (generatorData.voltageTarget > 0.0) {
        generator->set("vtarget", generatorData.voltageTarget);
        if (!readerOptions.checkFlag(USE_BUS_VOLTAGE_TARGETS)) {
            bus->set("vtarget", generatorData.voltageTarget);
        }
    }
    if (generatorData.status == 0) {
        generator->disable();
    }
    try {
        bus->add(generator);
    }
    catch (const ObjectAddFailure&) {
        savError(fileName, "could not attach generator " + id + " to bus " + bus->getName());
    }
}

void addLoad(const BasicReaderInfo& readerOptions,
             const LoadData& loadData,
             std::vector<GridBus*>& buses,
             const std::string& fileName)
{
    auto* bus = buses[static_cast<size_t>(loadData.bus)];
    auto id = loadData.id.empty() ? std::to_string(loadData.index) : loadData.id;
    auto* load = new ZipLoad(prefixedName(readerOptions, bus->getName() + "_load_" + id));
    load->set("basepower", bus->get("basepower", units::MW), units::MW);
    load->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
    // PSLF stores constant-power and constant-current load components in
    // MW/Mvar.  Its conductance/susceptance fields are already per-unit.
    load->set("p", loadData.realPower, MW);
    load->set("q", loadData.reactivePower, MVAR);
    load->set("ip", loadData.realCurrent, MW);
    load->set("iq", loadData.reactiveCurrent, MVAR);
    load->set("yp", loadData.conductance, units::puMW);
    load->set("yq", -loadData.susceptance, units::puMW);
    if (loadData.status == 0) {
        load->disable();
    }
    try {
        bus->add(load);
    }
    catch (const ObjectAddFailure&) {
        savError(fileName, "could not attach load " + id + " to bus " + bus->getName());
    }
}

void addLine(CoreObject* parentObject,
             const BasicReaderInfo& readerOptions,
             const LineData& lineData,
             const std::vector<GridBus*>& buses,
             const std::string& fileName)
{
    auto* line = new AcLine(prefixedName(readerOptions,
                                         "line_" + buses[static_cast<size_t>(lineData.from)]->getName() +
                                             "_to_" + buses[static_cast<size_t>(lineData.to)]->getName() +
                                             "_" + (lineData.circuit.empty() ? std::to_string(lineData.index) : lineData.circuit)));
    line->set("basepower",
              buses[static_cast<size_t>(lineData.from)]->get("basepower", units::MW),
              units::MW);
    line->updateBus(buses[static_cast<size_t>(lineData.from)], 1);
    line->updateBus(buses[static_cast<size_t>(lineData.to)], 2);
    line->set("r", lineData.resistance);
    line->set("x", lineData.reactance);
    line->set("b", lineData.susceptance);
    if (lineData.rating > 0.0) {
        line->set("ratinga", lineData.rating, units::MVAR);
    }
    if (lineData.status == 0) {
        line->disable();
    }
    try {
        addToParentWithRename(line, getSavLinkParent(parentObject, line));
    }
    catch (const ObjectAddFailure&) {
        savError(fileName, "could not attach a PSLF transmission line");
    }
}

void addTransformer(CoreObject* parentObject,
                    const BasicReaderInfo& readerOptions,
                    const TransformerData& transformerData,
                    const std::vector<GridBus*>& buses,
                    const std::string& fileName)
{
    const auto name = prefixedName(readerOptions,
                                   "tx_" + buses[static_cast<size_t>(transformerData.from)]->getName() +
                                       "_to_" + buses[static_cast<size_t>(transformerData.to)]->getName() +
                                       "_" + (transformerData.circuit.empty() ?
                                                   std::to_string(transformerData.index) :
                                                   transformerData.circuit));
    Link* transformer = (transformerData.type == 2) ?
        static_cast<Link*>(new links::AdjustableTransformer(name)) :
        static_cast<Link*>(new AcLine(name));
    transformer->set("basepower",
                      buses[static_cast<size_t>(transformerData.from)]->get("basepower", units::MW),
                      units::MW);
    transformer->updateBus(buses[static_cast<size_t>(transformerData.from)], 1);
    transformer->updateBus(buses[static_cast<size_t>(transformerData.to)], 2);
    const auto systemBase = transformer->get("basepower", units::MW);
    // PSLF stores transformer impedances on tran.tbase, while GridDyn links
    // use the system base.  This is the same base conversion applied by the
    // EPC transformer reader.
    const auto impedanceScale = systemBase / transformerData.transformerBase;
    transformer->set("r", transformerData.resistance * impedanceScale);
    transformer->set("x", transformerData.reactance * impedanceScale);
    transformer->set("tap", transformerData.tap);
    if (transformerData.type == 2) {
        auto* adjustableTransformer = static_cast<links::AdjustableTransformer*>(transformer);
        adjustableTransformer->set("mode", "voltage");
        adjustableTransformer->setControlBus(buses[static_cast<size_t>(transformerData.regulatingBus)]);
        adjustableTransformer->set("mintap", transformerData.tapMin);
        adjustableTransformer->set("maxtap", transformerData.tapMax);
        adjustableTransformer->set("vmin", transformerData.voltageMin);
        adjustableTransformer->set("vmax", transformerData.voltageMax);
        adjustableTransformer->set("stepsize", std::abs(transformerData.tapStep));
    }
    if (transformerData.rating > 0.0) {
        transformer->set("ratinga", transformerData.rating, units::MVAR);
    }
    if (transformerData.status == 0) {
        transformer->disable();
    }
    try {
        addToParentWithRename(transformer, getSavLinkParent(parentObject, transformer));
    }
    catch (const ObjectAddFailure&) {
        savError(fileName, "could not attach a PSLF transformer");
    }
}

void addShunt(const BasicReaderInfo& readerOptions,
              const ShuntData& shuntData,
              const std::vector<GridBus*>& buses,
              const std::string& fileName)
{
    auto* bus = buses[static_cast<size_t>(shuntData.bus)];
    auto id = shuntData.id.empty() ? std::to_string(shuntData.index) : shuntData.id;
    auto* shunt = new ZipLoad(prefixedName(readerOptions, bus->getName() + "_shunt_" + id));
    shunt->set("basepower", bus->get("basepower", units::MW), units::MW);
    shunt->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
    shunt->set("yp", shuntData.conductance, units::puMW);
    shunt->set("yq", -shuntData.susceptance, units::puMW);
    if (shuntData.status == 0) {
        shunt->disable();
    }
    try {
        bus->add(shunt);
    }
    catch (const ObjectAddFailure&) {
        savError(fileName, "could not attach fixed shunt " + id + " to bus " + bus->getName());
    }
}

}  // namespace

void loadSav(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    if (parentObject == nullptr) {
        throw InvalidParameterValue("PSLF SQLite .sav reader requires a simulation parent object");
    }

    SqliteDatabase database(fileName, fileName);
    CaseData caseData;
    validateFormat(database.get(), database.fileName(), caseData);

    const auto areas = readAreas(database.get(), database.fileName());
    const auto zoneCount = readZoneCount(database.get(), database.fileName());
    auto buses = readBuses(database.get(), database.fileName());
    readVoltages(database.get(), database.fileName(), buses);
    const auto generators = readGenerators(database.get(), database.fileName());
    const auto loads = readLoads(database.get(), database.fileName());
    const auto lines = readLines(database.get(), database.fileName());
    const auto transformers = readTransformers(database.get(), database.fileName());
    const auto shunts = readShunts(database.get(), database.fileName());

    requireCount(areas.size(), caseData.areaCount, database.fileName(), "area", "narea");
    requireCount(buses.size(), caseData.busCount, database.fileName(), "busd", "nbus");
    requireCount(buses.size(), caseData.busCount, database.fileName(), "volt", "nbus");
    requireCount(generators.size(), caseData.generatorCount, database.fileName(), "gens", "ngen");
    requireCount(loads.size(), caseData.loadCount, database.fileName(), "load", "nload");
    requireCount(lines.size(), caseData.lineCount, database.fileName(), "secdd", "nbrsec");
    requireCount(transformers.size(), caseData.transformerCount, database.fileName(), "tran", "ntran");
    requireCount(shunts.size(), caseData.shuntCount, database.fileName(), "shunt", "nshunt");
    requireCount(zoneCount, caseData.zoneCount, database.fileName(), "zone", "nzone");
    validateReferences(database.fileName(), buses.size(), generators, loads, lines, transformers, shunts);

    // No GridDyn object is created until all database structure and all imported references have
    // passed validation. This is important because legacy binary .sav files are also common.
    GridSimulation::resetObjectCounters();
    parentObject->set("basepower", caseData.basePower, units::MW);

    std::unordered_map<int, GridArea*> areaObjects;
    for (const auto& area : areas) {
        if (area.number > 0) {
            addArea(parentObject, readerOptions, area, areaObjects);
        }
    }

    std::vector<GridBus*> busObjects(buses.size(), nullptr);
    for (const auto& bus : buses) {
        addBus(parentObject, readerOptions, bus, areaObjects, busObjects);
    }
    for (const auto& generator : generators) {
        addGenerator(readerOptions, generator, busObjects, database.fileName());
    }
    for (const auto& load : loads) {
        addLoad(readerOptions, load, busObjects, database.fileName());
    }
    for (const auto& line : lines) {
        addLine(parentObject, readerOptions, line, busObjects, database.fileName());
    }
    for (const auto& transformer : transformers) {
        addTransformer(parentObject, readerOptions, transformer, busObjects, database.fileName());
    }
    for (const auto& shunt : shunts) {
        addShunt(readerOptions, shunt, busObjects, database.fileName());
    }
}

}  // namespace griddyn
