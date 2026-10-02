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
        throw InvalidParameterValue("PSLF SQLite .save '" + fileName + "': " + detail);
    }

    class SqliteDatabase {
      public:
        SqliteDatabase(const std::string& fileName, const std::string& displayName):
            mFileName(fileName)
        {
            const auto result =
                sqlite3_open_v2(fileName.c_str(), &mDatabase, SQLITE_OPEN_READONLY, nullptr);
            if ((result != SQLITE_OK) || (mDatabase == nullptr)) {
                const auto message = (mDatabase != nullptr) ?
                    std::string(sqlite3_errmsg(mDatabase)) :
                    std::string("unable to open database");
                if (mDatabase != nullptr) {
                    sqlite3_close(mDatabase);
                    mDatabase = nullptr;
                }
                savError(displayName, "file is not a readable SQLite database (" + message + ")");
            }
        }

        ~SqliteDatabase()
        {
            if (mDatabase != nullptr) {
                sqlite3_close(mDatabase);
            }
        }

        SqliteDatabase(const SqliteDatabase&) = delete;
        SqliteDatabase& operator=(const SqliteDatabase&) = delete;

        [[nodiscard]] sqlite3* get() const { return mDatabase; }
        [[nodiscard]] const std::string& fileName() const { return mFileName; }

      private:
        sqlite3* mDatabase = nullptr;
        std::string mFileName;
    };

    class SqliteStatement {
      public:
        SqliteStatement(sqlite3* database, const std::string& sql, const std::string& fileName):
            mDatabase(database)
        {
            const auto result = sqlite3_prepare_v2(database, sql.c_str(), -1, &mStatement, nullptr);
            if (result != SQLITE_OK) {
                savError(fileName,
                         "SQLite query could not be prepared: " +
                             std::string(sqlite3_errmsg(database)));
            }
        }

        ~SqliteStatement()
        {
            if (mStatement != nullptr) {
                sqlite3_finalize(mStatement);
            }
        }

        SqliteStatement(const SqliteStatement&) = delete;
        SqliteStatement& operator=(const SqliteStatement&) = delete;

        [[nodiscard]] sqlite3_stmt* get() const { return mStatement; }

      private:
        sqlite3* mDatabase = nullptr;
        sqlite3_stmt* mStatement = nullptr;
    };

    std::string columnText(sqlite3_stmt* statement, int column)
    {
        const auto* text = sqlite3_column_text(statement, column);
        return (text == nullptr) ? std::string{} : std::string(reinterpret_cast<const char*>(text));
    }

    double columnDouble(sqlite3_stmt* statement,
                        int column,
                        const std::string& fileName,
                        std::string_view field)
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

    int columnInteger(sqlite3_stmt* statement,
                      int column,
                      const std::string& fileName,
                      std::string_view field)
    {
        const auto type = sqlite3_column_type(statement, column);
        if (type == SQLITE_INTEGER) {
            const auto value = sqlite3_column_int64(statement, column);
            if ((value < std::numeric_limits<int>::min()) ||
                (value > std::numeric_limits<int>::max())) {
                savError(fileName,
                         "column '" + std::string(field) +
                             "' is outside the supported integer range");
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
        const SqliteStatement statement(
            database,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1",
            fileName);
        sqlite3_bind_text(statement.get(),
                          1,
                          tableName.data(),
                          static_cast<int>(tableName.size()),
                          SQLITE_TRANSIENT);
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
        const SqliteStatement statement(database, sql, fileName);
        while (true) {
            const auto result = sqlite3_step(statement.get());
            if (result == SQLITE_DONE) {
                break;
            }
            if (result != SQLITE_ROW) {
                savError(fileName,
                         "could not inspect columns for table '" + std::string(tableName) + "'");
            }
            columns.emplace(columnText(statement.get(), 1));
        }

        for (const auto requiredColumn : requiredColumns) {
            if (!columns.contains(std::string(requiredColumn))) {
                savError(fileName,
                         "PSLF table '" + std::string(tableName) +
                             "' is missing required column '" + std::string(requiredColumn) + "'");
            }
        }
    }

    struct CaseData {
        double mBasePower = 0.0;
        int mBusCount = 0;
        int mLineCount = 0;
        int mGeneratorCount = 0;
        int mTransformerCount = 0;
        int mLoadCount = 0;
        int mShuntCount = 0;
        int mAreaCount = 0;
        int mZoneCount = 0;
    };

    struct AreaData {
        int mIndex = -1;
        int mNumber = 0;
        std::string mName;
    };

    struct BusData {
        int mIndex = -1;
        int mExternalNumber = 0;
        std::string mName;
        double mBaseVoltage = 0.0;
        int mType = 1;
        int mArea = 0;
        int mZone = 0;
        double mScheduledVoltage = 1.0;
        double mMaxVoltage = 0.0;
        double mMinVoltage = 0.0;
        double mVoltage = 1.0;
        double mAngle = 0.0;
    };

    struct GeneratorData {
        int mIndex = -1;
        int mBus = -1;
        std::string mId;
        int mStatus = 1;
        double mMachineBase = 0.0;
        double mRealPower = 0.0;
        double mReactivePower = 0.0;
        double mMaxReactivePower = 0.0;
        double mMinReactivePower = 0.0;
        double mMaxRealPower = 0.0;
        double mMinRealPower = 0.0;
        double mVoltageTarget = 0.0;
    };

    struct LoadData {
        int mIndex = -1;
        int mBus = -1;
        std::string mId;
        int mStatus = 1;
        double mRealPower = 0.0;
        double mReactivePower = 0.0;
        double mRealCurrent = 0.0;
        double mReactiveCurrent = 0.0;
        double mConductance = 0.0;
        double mSusceptance = 0.0;
    };

    struct LineData {
        int mIndex = -1;
        int mFrom = -1;
        int mTo = -1;
        std::string mCircuit;
        int mStatus = 1;
        double mResistance = 0.0;
        double mReactance = 0.0;
        double mSusceptance = 0.0;
        double mRating = 0.0;
    };

    struct TransformerData {
        int mIndex = -1;
        int mFrom = -1;
        int mTo = -1;
        std::string mCircuit;
        int mStatus = 1;
        int mType = 1;
        int mRegulatingBus = -1;
        double mTransformerBase = 0.0;
        int mTertiaryBus = 0;
        double mResistance = 0.0;
        double mReactance = 0.0;
        double mTertiaryResistance = 0.0;
        double mTertiaryReactance = 0.0;
        double mTap = 1.0;
        double mRating = 0.0;
        double mTapMax = 0.0;
        double mTapMin = 0.0;
        double mVoltageMax = 0.0;
        double mVoltageMin = 0.0;
        double mTapStep = 0.0;
    };

    struct ShuntData {
        int mIndex = -1;
        int mBus = -1;
        std::string mId;
        int mStatus = 1;
        double mConductance = 0.0;
        double mSusceptance = 0.0;
    };

    template<class Record>
    void requireContiguousIndexes(const std::vector<Record>& records,
                                  const std::string& fileName,
                                  std::string_view tableName)
    {
        std::vector<int> indexes;
        indexes.reserve(records.size());
        for (const auto& record : records) {
            indexes.push_back(record.mIndex);
        }
        std::sort(indexes.begin(), indexes.end());
        for (size_t index = 0; index < indexes.size(); ++index) {
            if (std::cmp_not_equal(indexes[index], index)) {
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
        if ((expected < 0) || std::cmp_not_equal(actual, expected)) {
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
        if ((busIndex < 0) || std::cmp_greater_equal(busIndex, busCount)) {
            savError(fileName,
                     "PSLF table '" + std::string(tableName) + "' references bus index " +
                         std::to_string(busIndex) + " outside busd");
        }
    }

    CaseData readCaseData(sqlite3* database, const std::string& fileName)
    {
        const SqliteStatement statement(
            database,
            "SELECT sbase, nbus, nbrsec, ngen, ntran, nload, nshunt, narea, nzone FROM casepar ORDER BY _idx LIMIT 1",
            fileName);
        if (sqlite3_step(statement.get()) != SQLITE_ROW) {
            savError(fileName, "casepar does not contain a case parameter row");
        }

        CaseData data;
        data.mBasePower = columnDouble(statement.get(), 0, fileName, "casepar.sbase");
        data.mBusCount = columnInteger(statement.get(), 1, fileName, "casepar.nbus");
        data.mLineCount = columnInteger(statement.get(), 2, fileName, "casepar.nbrsec");
        data.mGeneratorCount = columnInteger(statement.get(), 3, fileName, "casepar.ngen");
        data.mTransformerCount = columnInteger(statement.get(), 4, fileName, "casepar.ntran");
        data.mLoadCount = columnInteger(statement.get(), 5, fileName, "casepar.nload");
        data.mShuntCount = columnInteger(statement.get(), 6, fileName, "casepar.nshunt");
        data.mAreaCount = columnInteger(statement.get(), 7, fileName, "casepar.narea");
        data.mZoneCount = columnInteger(statement.get(), 8, fileName, "casepar.nzone");
        if (data.mBasePower <= 0.0) {
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
        requireColumns(database,
                       fileName,
                       "casepar",
                       {"_idx",
                        "sbase",
                        "nbus",
                        "nbrsec",
                        "ngen",
                        "ntran",
                        "nload",
                        "nshunt",
                        "narea",
                        "nzone"});
        requireColumns(database,
                       fileName,
                       "busd",
                       {"_idx",
                        "extnum",
                        "busnam",
                        "basekv",
                        "type",
                        "area",
                        "zone",
                        "vsched",
                        "vmax",
                        "vmin"});
        requireColumns(database, fileName, "volt", {"_idx", "vr", "vi", "vm", "va"});
        requireColumns(database,
                       fileName,
                       "gens",
                       {"_idx",
                        "ibgen",
                        "id",
                        "st",
                        "mbase",
                        "pgen",
                        "qgen",
                        "qmax",
                        "qmin",
                        "pmax",
                        "pmin",
                        "vcsched"});
        requireColumns(database,
                       fileName,
                       "load",
                       {"_idx", "lbus", "id", "st", "p", "q", "ip", "iq", "g", "b"});
        requireColumns(database,
                       fileName,
                       "secdd",
                       {"_idx", "ifrom", "ito", "ck", "st", "zsecr", "zsecx", "bsec", "rate0"});
        requireColumns(database, fileName, "tran", {"_idx", "ifrom",   "ito",   "ck",    "st",
                                                    "type", "kreg",    "tbase", "zpsr",  "zpsx",
                                                    "zptr", "zptx",    "ztsr",  "ztsx",  "rate0",
                                                    "tmax", "tmin",    "vtmax", "vtmin", "stepp",
                                                    "tapp", "midbus_t"});
        requireColumns(database, fileName, "shunt", {"_idx", "ifrom", "id", "st", "g", "b"});
        requireColumns(database, fileName, "area", {"_idx", "arnum", "arname"});
        requireColumns(database, fileName, "zone", {"_idx", "zonum", "zonam"});

        {
            const SqliteStatement statement(
                database,
                "SELECT schema_version, last_save_client FROM pslf_database_metadata LIMIT 1",
                fileName);
            if (sqlite3_step(statement.get()) != SQLITE_ROW) {
                savError(fileName, "pslf_database_metadata does not contain a metadata row");
            }
            const auto schemaVersion = columnInteger(statement.get(),
                                                     0,
                                                     fileName,
                                                     "pslf_database_metadata.schema_version");
            if (schemaVersion != pslfSchemaVersion) {
                savError(fileName,
                         "unsupported PSLF SQLite schema version " + std::to_string(schemaVersion) +
                             "; GridDyn supports version " + std::to_string(pslfSchemaVersion));
            }
            auto client = columnText(statement.get(), 1);
            std::transform(client.begin(),
                           client.end(),
                           client.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::toupper(character));
                           });
            if (!client.contains("PSLF")) {
                savError(fileName, "metadata does not identify the file as a PSLF save");
            }
        }
        caseData = readCaseData(database, fileName);
    }

    std::vector<AreaData> readAreas(sqlite3* database, const std::string& fileName)
    {
        std::vector<AreaData> areas;
        const SqliteStatement statement(database,
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
            area.mIndex = columnInteger(statement.get(), 0, fileName, "area._idx");
            area.mNumber = columnInteger(statement.get(), 1, fileName, "area.arnum");
            area.mName = columnText(statement.get(), 2);
            areas.push_back(std::move(area));
        }
        requireContiguousIndexes(areas, fileName, "area");
        std::unordered_set<int> areaNumbers;
        for (const auto& area : areas) {
            if ((area.mNumber <= 0) || !areaNumbers.emplace(area.mNumber).second) {
                savError(fileName, "area does not contain unique positive arnum values");
            }
        }
        return areas;
    }

    size_t readZoneCount(sqlite3* database, const std::string& fileName)
    {
        const SqliteStatement statement(database,
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
            if (std::cmp_not_equal(index, row) || (number <= 0) ||
                !zoneNumbers.emplace(number).second) {
                savError(fileName, "zone does not contain unique zero-based records");
            }
            ++row;
        }
        return row;
    }

    std::vector<BusData> readBuses(sqlite3* database, const std::string& fileName)
    {
        std::vector<BusData> buses;
        const SqliteStatement statement(
            database,
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
            bus.mIndex = columnInteger(statement.get(), 0, fileName, "busd._idx");
            bus.mExternalNumber = columnInteger(statement.get(), 1, fileName, "busd.extnum");
            bus.mName = columnText(statement.get(), 2);
            bus.mBaseVoltage = columnDouble(statement.get(), 3, fileName, "busd.basekv");
            bus.mType = columnInteger(statement.get(), 4, fileName, "busd.type");
            bus.mArea = columnInteger(statement.get(), 5, fileName, "busd.area");
            bus.mZone = columnInteger(statement.get(), 6, fileName, "busd.zone");
            bus.mScheduledVoltage = columnDouble(statement.get(), 7, fileName, "busd.vsched");
            bus.mMaxVoltage = columnDouble(statement.get(), 8, fileName, "busd.vmax");
            bus.mMinVoltage = columnDouble(statement.get(), 9, fileName, "busd.vmin");
            if ((bus.mType < 0) || (bus.mType > 4)) {
                savError(fileName,
                         "busd contains unsupported bus type " + std::to_string(bus.mType));
            }
            if ((bus.mExternalNumber <= 0) || (bus.mBaseVoltage <= 0.0)) {
                savError(fileName, "busd contains an invalid external number or base voltage");
            }
            buses.push_back(std::move(bus));
        }

        std::unordered_set<int> externalNumbers;
        for (const auto& bus : buses) {
            if (!externalNumbers.emplace(bus.mExternalNumber).second) {
                savError(fileName,
                         "busd contains duplicate external bus number " +
                             std::to_string(bus.mExternalNumber));
            }
        }
        requireContiguousIndexes(buses, fileName, "busd");
        return buses;
    }

    void readVoltages(sqlite3* database, const std::string& fileName, std::vector<BusData>& buses)
    {
        const SqliteStatement statement(database,
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
            if ((index < 0) || std::cmp_greater_equal(index, buses.size()) ||
                std::cmp_not_equal(index, row)) {
                savError(fileName, "volt does not align with the zero-based busd indexes");
            }
            const auto realVoltage = columnDouble(statement.get(), 1, fileName, "volt.vr");
            const auto imaginaryVoltage = columnDouble(statement.get(), 2, fileName, "volt.vi");
            const auto magnitude = columnDouble(statement.get(), 3, fileName, "volt.vm");
            const auto savedAngle = columnDouble(statement.get(), 4, fileName, "volt.va");
            const auto rectangularMagnitude = std::hypot(realVoltage, imaginaryVoltage);
            if (rectangularMagnitude > std::numeric_limits<double>::epsilon()) {
                buses[index].mVoltage = rectangularMagnitude;
                buses[index].mAngle = std::atan2(imaginaryVoltage, realVoltage);
            } else {
                buses[index].mVoltage = magnitude;
                buses[index].mAngle = savedAngle;
            }
            if ((buses[index].mVoltage < 0.0) || !std::isfinite(buses[index].mAngle)) {
                savError(fileName, "volt contains an invalid voltage magnitude or angle");
            }
            ++row;
        }
    }

    std::vector<GeneratorData> readGenerators(sqlite3* database, const std::string& fileName)
    {
        std::vector<GeneratorData> generators;
        const SqliteStatement statement(
            database,
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
            generator.mIndex = columnInteger(statement.get(), 0, fileName, "gens._idx");
            generator.mBus = columnInteger(statement.get(), 1, fileName, "gens.ibgen");
            generator.mId = columnText(statement.get(), 2);
            generator.mStatus = columnInteger(statement.get(), 3, fileName, "gens.st");
            generator.mMachineBase = columnDouble(statement.get(), 4, fileName, "gens.mbase");
            generator.mRealPower = columnDouble(statement.get(), 5, fileName, "gens.pgen");
            generator.mReactivePower = columnDouble(statement.get(), 6, fileName, "gens.qgen");
            generator.mMaxReactivePower = columnDouble(statement.get(), 7, fileName, "gens.qmax");
            generator.mMinReactivePower = columnDouble(statement.get(), 8, fileName, "gens.qmin");
            generator.mMaxRealPower = columnDouble(statement.get(), 9, fileName, "gens.pmax");
            generator.mMinRealPower = columnDouble(statement.get(), 10, fileName, "gens.pmin");
            generator.mVoltageTarget = columnDouble(statement.get(), 11, fileName, "gens.vcsched");
            generators.push_back(std::move(generator));
        }
        requireContiguousIndexes(generators, fileName, "gens");
        return generators;
    }

    std::vector<LoadData> readLoads(sqlite3* database, const std::string& fileName)
    {
        std::vector<LoadData> loads;
        const SqliteStatement statement(
            database,
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
            load.mIndex = columnInteger(statement.get(), 0, fileName, "load._idx");
            load.mBus = columnInteger(statement.get(), 1, fileName, "load.lbus");
            load.mId = columnText(statement.get(), 2);
            load.mStatus = columnInteger(statement.get(), 3, fileName, "load.st");
            load.mRealPower = columnDouble(statement.get(), 4, fileName, "load.p");
            load.mReactivePower = columnDouble(statement.get(), 5, fileName, "load.q");
            load.mRealCurrent = columnDouble(statement.get(), 6, fileName, "load.ip");
            load.mReactiveCurrent = columnDouble(statement.get(), 7, fileName, "load.iq");
            load.mConductance = columnDouble(statement.get(), 8, fileName, "load.g");
            load.mSusceptance = columnDouble(statement.get(), 9, fileName, "load.b");
            loads.push_back(std::move(load));
        }
        requireContiguousIndexes(loads, fileName, "load");
        return loads;
    }

    std::vector<LineData> readLines(sqlite3* database, const std::string& fileName)
    {
        std::vector<LineData> lines;
        const SqliteStatement statement(
            database,
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
            line.mIndex = columnInteger(statement.get(), 0, fileName, "secdd._idx");
            line.mFrom = columnInteger(statement.get(), 1, fileName, "secdd.ifrom");
            line.mTo = columnInteger(statement.get(), 2, fileName, "secdd.ito");
            line.mCircuit = columnText(statement.get(), 3);
            line.mStatus = columnInteger(statement.get(), 4, fileName, "secdd.st");
            line.mResistance = columnDouble(statement.get(), 5, fileName, "secdd.zsecr");
            line.mReactance = columnDouble(statement.get(), 6, fileName, "secdd.zsecx");
            line.mSusceptance = columnDouble(statement.get(), 7, fileName, "secdd.bsec");
            line.mRating = columnDouble(statement.get(), 8, fileName, "secdd.rate0");
            lines.push_back(std::move(line));
        }
        requireContiguousIndexes(lines, fileName, "secdd");
        return lines;
    }

    std::vector<TransformerData> readTransformers(sqlite3* database, const std::string& fileName)
    {
        std::vector<TransformerData> transformers;
        const SqliteStatement statement(
            database,
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
            transformer.mIndex = columnInteger(statement.get(), 0, fileName, "tran._idx");
            transformer.mFrom = columnInteger(statement.get(), 1, fileName, "tran.ifrom");
            transformer.mTo = columnInteger(statement.get(), 2, fileName, "tran.ito");
            transformer.mCircuit = columnText(statement.get(), 3);
            transformer.mStatus = columnInteger(statement.get(), 4, fileName, "tran.st");
            transformer.mType = columnInteger(statement.get(), 5, fileName, "tran.type");
            transformer.mRegulatingBus = columnInteger(statement.get(), 6, fileName, "tran.kreg");
            transformer.mTransformerBase = columnDouble(statement.get(), 7, fileName, "tran.tbase");
            transformer.mResistance = columnDouble(statement.get(), 8, fileName, "tran.zpsr");
            transformer.mReactance = columnDouble(statement.get(), 9, fileName, "tran.zpsx");
            transformer.mTertiaryResistance =
                columnDouble(statement.get(), 10, fileName, "tran.zptr");
            transformer.mTertiaryReactance =
                columnDouble(statement.get(), 11, fileName, "tran.zptx");
            const auto secondaryTertiaryResistance =
                columnDouble(statement.get(), 12, fileName, "tran.ztsr");
            const auto secondaryTertiaryReactance =
                columnDouble(statement.get(), 13, fileName, "tran.ztsx");
            transformer.mRating = columnDouble(statement.get(), 14, fileName, "tran.rate0");
            transformer.mTapMax = columnDouble(statement.get(), 15, fileName, "tran.tmax");
            transformer.mTapMin = columnDouble(statement.get(), 16, fileName, "tran.tmin");
            transformer.mVoltageMax = columnDouble(statement.get(), 17, fileName, "tran.vtmax");
            transformer.mVoltageMin = columnDouble(statement.get(), 18, fileName, "tran.vtmin");
            transformer.mTapStep = columnDouble(statement.get(), 19, fileName, "tran.stepp");
            transformer.mTap = columnDouble(statement.get(), 20, fileName, "tran.tapp");
            transformer.mTertiaryBus = (sqlite3_column_type(statement.get(), 21) == SQLITE_NULL) ?
                0 :
                columnInteger(statement.get(), 21, fileName, "tran.midbus_t");
            if ((transformer.mType != 1) && (transformer.mType != 2)) {
                savError(fileName,
                         "unsupported PSLF two-winding transformer type " +
                             std::to_string(transformer.mType) + " in record " +
                             std::to_string(transformer.mIndex));
            }
            if (transformer.mTransformerBase <= 0.0) {
                savError(fileName,
                         "tran.tbase must be positive in transformer record " +
                             std::to_string(transformer.mIndex));
            }
            if (transformer.mType == 2) {
                if (transformer.mTapMin >= transformer.mTapMax) {
                    savError(
                        fileName,
                        "tran.tmin must be less than tran.tmax in regulating transformer record " +
                            std::to_string(transformer.mIndex));
                }
                if (transformer.mVoltageMin >= transformer.mVoltageMax) {
                    savError(
                        fileName,
                        "tran.vtmin must be less than tran.vtmax in regulating transformer record " +
                            std::to_string(transformer.mIndex));
                }
            }
            // PSLF uses 999 as an unused/sentinel impedance value in otherwise ordinary
            // two-winding transformer rows.  Do not mistake that representation for a
            // three-winding transformer.
            const auto isUnusedImpedance = [](double value) {
                return (std::abs(value) <= 1.0e-12) || (std::abs(value - 999.0) <= 1.0e-9);
            };
            if ((transformer.mTertiaryBus > 0) ||
                !isUnusedImpedance(transformer.mTertiaryResistance) ||
                !isUnusedImpedance(transformer.mTertiaryReactance) ||
                !isUnusedImpedance(secondaryTertiaryResistance) ||
                !isUnusedImpedance(secondaryTertiaryReactance)) {
                savError(fileName,
                         "three-winding transformer record " + std::to_string(transformer.mIndex) +
                             " is not supported yet; two-winding transformers are supported");
            }
            if (transformer.mTap == 0.0) {
                transformer.mTap = 1.0;
            }
            transformers.push_back(std::move(transformer));
        }
        requireContiguousIndexes(transformers, fileName, "tran");
        return transformers;
    }

    std::vector<ShuntData> readShunts(sqlite3* database, const std::string& fileName)
    {
        std::vector<ShuntData> shunts;
        const SqliteStatement statement(database,
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
            shunt.mIndex = columnInteger(statement.get(), 0, fileName, "shunt._idx");
            shunt.mBus = columnInteger(statement.get(), 1, fileName, "shunt.ifrom");
            shunt.mId = columnText(statement.get(), 2);
            shunt.mStatus = columnInteger(statement.get(), 3, fileName, "shunt.st");
            shunt.mConductance = columnDouble(statement.get(), 4, fileName, "shunt.g");
            shunt.mSusceptance = columnDouble(statement.get(), 5, fileName, "shunt.b");
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
            requireBusReference(generator.mBus, busCount, fileName, "gens");
        }
        for (const auto& load : loads) {
            requireBusReference(load.mBus, busCount, fileName, "load");
        }
        for (const auto& line : lines) {
            requireBusReference(line.mFrom, busCount, fileName, "secdd");
            requireBusReference(line.mTo, busCount, fileName, "secdd");
        }
        for (const auto& transformer : transformers) {
            requireBusReference(transformer.mFrom, busCount, fileName, "tran");
            requireBusReference(transformer.mTo, busCount, fileName, "tran");
            if (transformer.mType == 2) {
                requireBusReference(transformer.mRegulatingBus, busCount, fileName, "tran.kreg");
            }
        }
        for (const auto& shunt : shunts) {
            requireBusReference(shunt.mBus, busCount, fileName, "shunt");
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
        for (auto* area = area1; area != nullptr;
             area = dynamic_cast<GridArea*>(area->getParent())) {
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
        auto name =
            areaData.mName.empty() ? "AREA_" + std::to_string(areaData.mNumber) : areaData.mName;
        auto* area = new GridArea(prefixedName(readerOptions, std::move(name)));
        area->setUserID(static_cast<index_t>(areaData.mNumber));
        try {
            parentObject->add(area);
        }
        catch (const ObjectAddFailure&) {
            addToParentWithRename(area, parentObject);
        }
        areas.emplace(areaData.mNumber, area);
    }

    void addBus(CoreObject* parentObject,
                const BasicReaderInfo& readerOptions,
                const BusData& busData,
                const std::unordered_map<int, GridArea*>& areas,
                std::vector<GridBus*>& buses)
    {
        auto name = busData.mName.empty() ? "BUS_" + std::to_string(busData.mExternalNumber) :
                                            busData.mName;
        auto* bus = new AcBus(prefixedName(readerOptions, std::move(name)));
        bus->setUserID(static_cast<index_t>(busData.mExternalNumber));
        bus->set("basepower", parentObject->get("basepower"));
        bus->set("basevoltage", busData.mBaseVoltage, units::kV);
        // PSLF busd type codes are different from the CDF/RAW codes used by
        // several of the other readers: 0 is swing, 1 is PQ, and 2 is PV.
        if (busData.mType == 0) {
            bus->set("type", "SLK");
            bus->set("atarget", busData.mAngle);
        } else if (busData.mType == 2) {
            bus->set("type", "PV");
        } else {
            bus->set("type", "PQ");
            if (busData.mType == 4) {
                bus->disable();
            }
        }
        if (busData.mScheduledVoltage > 0.0) {
            bus->set("vtarget", busData.mScheduledVoltage);
        }
        if (busData.mMaxVoltage > 0.0) {
            bus->set("vmax", busData.mMaxVoltage);
        }
        if (busData.mMinVoltage > 0.0) {
            bus->set("vmin", busData.mMinVoltage);
        }
        bus->setVoltageAngle(busData.mVoltage, busData.mAngle);
        if (busData.mZone > 0) {
            bus->set("zone", static_cast<double>(busData.mZone));
        }

        auto* busParent = parentObject;
        if (const auto area = areas.find(busData.mArea); area != areas.end()) {
            busParent = area->second;
        }
        addToParentWithRename(bus, busParent);
        buses[static_cast<size_t>(busData.mIndex)] = bus;
    }

    void addGenerator(const BasicReaderInfo& readerOptions,
                      const GeneratorData& generatorData,
                      std::vector<GridBus*>& buses,
                      const std::string& fileName)
    {
        auto* bus = buses[static_cast<size_t>(generatorData.mBus)];
        auto componentId =
            generatorData.mId.empty() ? std::to_string(generatorData.mIndex) : generatorData.mId;
        // DYD/DYR records attach machine, exciter, governor, and stabilizer models to
        // DynamicGenerator objects.  Keep the lightweight Generator for callers that
        // explicitly requested a power-flow-only import, but make normal SAVE imports
        // dynamic-capable so an associated DYD can be loaded afterward.
        Generator* generator = nullptr;
        const auto generatorName =
            prefixedName(readerOptions, bus->getName() + "_gen_" + componentId);
        if (readerOptions.checkFlag(ASSUME_POWERFLOW_ONLY)) {
            generator = new Generator(generatorName);
        } else {
            generator = new DynamicGenerator(generatorName);
        }
        generator->set("basepower", bus->get("basepower", units::MW), units::MW);
        generator->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
        // PSLF stores these fields in MW/Mvar.  Convert at the GridDyn object
        // boundary so the internal representation remains puMW.
        generator->set("p", generatorData.mRealPower, MW);
        generator->set("q", generatorData.mReactivePower, MVAR);
        generator->set("qmax", generatorData.mMaxReactivePower, MVAR);
        generator->set("qmin", generatorData.mMinReactivePower, MVAR);
        generator->set("pmax", generatorData.mMaxRealPower, MW);
        generator->set("pmin", generatorData.mMinRealPower, MW);
        if (generatorData.mMachineBase > 0.0) {
            generator->set("mbase", generatorData.mMachineBase, units::MVAR);
        }
        if (generatorData.mVoltageTarget > 0.0) {
            generator->set("vtarget", generatorData.mVoltageTarget);
            if (!readerOptions.checkFlag(USE_BUS_VOLTAGE_TARGETS)) {
                bus->set("vtarget", generatorData.mVoltageTarget);
            }
        }
        if (generatorData.mStatus == 0) {
            generator->disable();
        }
        try {
            bus->add(generator);
        }
        catch (const ObjectAddFailure&) {
            savError(fileName,
                     "could not attach generator " + componentId + " to bus " + bus->getName());
        }
    }

    void addLoad(const BasicReaderInfo& readerOptions,
                 const LoadData& loadData,
                 std::vector<GridBus*>& buses,
                 const std::string& fileName)
    {
        auto* bus = buses[static_cast<size_t>(loadData.mBus)];
        auto loadId = loadData.mId.empty() ? std::to_string(loadData.mIndex) : loadData.mId;
        auto* load = new ZipLoad(prefixedName(readerOptions, bus->getName() + "_load_" + loadId));
        load->set("basepower", bus->get("basepower", units::MW), units::MW);
        load->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
        // PSLF stores constant-power and constant-current load components in
        // MW/Mvar.  Its conductance/susceptance fields are already per-unit.
        load->set("p", loadData.mRealPower, MW);
        load->set("q", loadData.mReactivePower, MVAR);
        load->set("ip", loadData.mRealCurrent, MW);
        load->set("iq", loadData.mReactiveCurrent, MVAR);
        load->set("yp", loadData.mConductance, units::puMW);
        load->set("yq", -loadData.mSusceptance, units::puMW);
        if (loadData.mStatus == 0) {
            load->disable();
        }
        try {
            bus->add(load);
        }
        catch (const ObjectAddFailure&) {
            savError(fileName, "could not attach load " + loadId + " to bus " + bus->getName());
        }
    }

    void addLine(CoreObject* parentObject,
                 const BasicReaderInfo& readerOptions,
                 const LineData& lineData,
                 const std::vector<GridBus*>& buses,
                 const std::string& fileName)
    {
        auto* line = new AcLine(
            prefixedName(readerOptions,
                         "line_" + buses[static_cast<size_t>(lineData.mFrom)]->getName() + "_to_" +
                             buses[static_cast<size_t>(lineData.mTo)]->getName() + "_" +
                             (lineData.mCircuit.empty() ? std::to_string(lineData.mIndex) :
                                                          lineData.mCircuit)));
        line->set("basepower",
                  buses[static_cast<size_t>(lineData.mFrom)]->get("basepower", units::MW),
                  units::MW);
        line->updateBus(buses[static_cast<size_t>(lineData.mFrom)], 1);
        line->updateBus(buses[static_cast<size_t>(lineData.mTo)], 2);
        line->set("r", lineData.mResistance);
        line->set("x", lineData.mReactance);
        line->set("b", lineData.mSusceptance);
        if (lineData.mRating > 0.0) {
            line->set("ratinga", lineData.mRating, units::MVAR);
        }
        if (lineData.mStatus == 0) {
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
        const auto name =
            prefixedName(readerOptions,
                         "tx_" + buses[static_cast<size_t>(transformerData.mFrom)]->getName() +
                             "_to_" + buses[static_cast<size_t>(transformerData.mTo)]->getName() +
                             "_" +
                             (transformerData.mCircuit.empty() ?
                                  std::to_string(transformerData.mIndex) :
                                  transformerData.mCircuit));
        Link* transformer = (transformerData.mType == 2) ?
            static_cast<Link*>(new links::AdjustableTransformer(name)) :
            static_cast<Link*>(new AcLine(name));
        transformer->set("basepower",
                         buses[static_cast<size_t>(transformerData.mFrom)]->get("basepower",
                                                                                units::MW),
                         units::MW);
        transformer->updateBus(buses[static_cast<size_t>(transformerData.mFrom)], 1);
        transformer->updateBus(buses[static_cast<size_t>(transformerData.mTo)], 2);
        const auto systemBase = transformer->get("basepower", units::MW);
        // PSLF stores transformer impedances on tran.tbase, while GridDyn links
        // use the system base.  This is the same base conversion applied by the
        // EPC transformer reader.
        const auto impedanceScale = systemBase / transformerData.mTransformerBase;
        transformer->set("r", transformerData.mResistance * impedanceScale);
        transformer->set("x", transformerData.mReactance * impedanceScale);
        transformer->set("tap", transformerData.mTap);
        if (transformerData.mType == 2) {
            auto* adjustableTransformer = static_cast<links::AdjustableTransformer*>(transformer);
            adjustableTransformer->set("mode", "voltage");
            adjustableTransformer->setControlBus(
                buses[static_cast<size_t>(transformerData.mRegulatingBus)]);
            adjustableTransformer->set("mintap", transformerData.mTapMin);
            adjustableTransformer->set("maxtap", transformerData.mTapMax);
            adjustableTransformer->set("vmin", transformerData.mVoltageMin);
            adjustableTransformer->set("vmax", transformerData.mVoltageMax);
            adjustableTransformer->set("stepsize", std::abs(transformerData.mTapStep));
        }
        if (transformerData.mRating > 0.0) {
            transformer->set("ratinga", transformerData.mRating, units::MVAR);
        }
        if (transformerData.mStatus == 0) {
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
        auto* bus = buses[static_cast<size_t>(shuntData.mBus)];
        auto shuntId = shuntData.mId.empty() ? std::to_string(shuntData.mIndex) : shuntData.mId;
        auto* shunt =
            new ZipLoad(prefixedName(readerOptions, bus->getName() + "_shunt_" + shuntId));
        shunt->set("basepower", bus->get("basepower", units::MW), units::MW);
        shunt->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
        shunt->set("yp", shuntData.mConductance, units::puMW);
        shunt->set("yq", -shuntData.mSusceptance, units::puMW);
        if (shuntData.mStatus == 0) {
            shunt->disable();
        }
        try {
            bus->add(shunt);
        }
        catch (const ObjectAddFailure&) {
            savError(fileName,
                     "could not attach fixed shunt " + shuntId + " to bus " + bus->getName());
        }
    }

}  // namespace

void loadSav(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& readerOptions)
{
    if (parentObject == nullptr) {
        throw InvalidParameterValue("PSLF SQLite .save reader requires a simulation parent object");
    }

    const SqliteDatabase database(fileName, fileName);
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

    requireCount(areas.size(), caseData.mAreaCount, database.fileName(), "area", "narea");
    requireCount(buses.size(), caseData.mBusCount, database.fileName(), "busd", "nbus");
    requireCount(buses.size(), caseData.mBusCount, database.fileName(), "volt", "nbus");
    requireCount(generators.size(), caseData.mGeneratorCount, database.fileName(), "gens", "ngen");
    requireCount(loads.size(), caseData.mLoadCount, database.fileName(), "load", "nload");
    requireCount(lines.size(), caseData.mLineCount, database.fileName(), "secdd", "nbrsec");
    requireCount(
        transformers.size(), caseData.mTransformerCount, database.fileName(), "tran", "ntran");
    requireCount(shunts.size(), caseData.mShuntCount, database.fileName(), "shunt", "nshunt");
    requireCount(zoneCount, caseData.mZoneCount, database.fileName(), "zone", "nzone");
    validateReferences(
        database.fileName(), buses.size(), generators, loads, lines, transformers, shunts);

    // No GridDyn object is created until all database structure and all imported references have
    // passed validation. This is important because legacy binary .save files are also common.
    GridSimulation::resetObjectCounters();
    parentObject->set("basepower", caseData.mBasePower, units::MW);

    std::unordered_map<int, GridArea*> areaObjects;
    for (const auto& area : areas) {
        if (area.mNumber > 0) {
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
