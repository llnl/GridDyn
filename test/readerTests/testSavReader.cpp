/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Load.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "sqlite3.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

void executeSql(sqlite3* database, const char* sql)
{
    char* errorMessage = nullptr;
    const auto result = sqlite3_exec(database, sql, nullptr, nullptr, &errorMessage);
    const std::string message = (errorMessage == nullptr) ? std::string{} : errorMessage;
    sqlite3_free(errorMessage);
    ASSERT_EQ(result, SQLITE_OK) << message;
}

std::filesystem::path makeSavFixture()
{
    auto filePath = std::filesystem::temp_directory_path() / "griddyn_pslf_sqlite_reader.sav";
    std::error_code removeError;
    std::filesystem::remove(filePath, removeError);

    sqlite3* database = nullptr;
    if (sqlite3_open(filePath.string().c_str(), &database) != SQLITE_OK) {
        ADD_FAILURE() << "could not create SQLite test fixture";
        if (database != nullptr) {
            sqlite3_close(database);
        }
        return {};
    }
    executeSql(
        database,
        "CREATE TABLE pslf_database_metadata (schema_version integer, last_save_client string);"
        "INSERT INTO pslf_database_metadata VALUES (4, 'PSLF Version 23.0.9');"
        "CREATE TABLE casepar (_idx integer, sbase real, nbus integer, nbrsec integer, ngen integer, ntran integer, nload integer, nshunt integer, narea integer, nzone integer);"
        "INSERT INTO casepar VALUES (0, 100.0, 2, 1, 1, 1, 1, 1, 0, 0);"
        "CREATE TABLE area (_idx integer, arnum integer, arname text);"
        "CREATE TABLE zone (_idx integer, zonum integer, zonam text);"
        "CREATE TABLE busd (_idx integer, extnum integer, busnam text, basekv real, type integer, area integer, zone integer, vsched real, vmax real, vmin real);"
        "INSERT INTO busd VALUES (0, 101, 'BUS_A', 230.0, 0, 0, 0, 1.02, 1.1, 0.9);"
        "INSERT INTO busd VALUES (1, 102, 'BUS_B', 230.0, 1, 0, 0, 1.0, 1.1, 0.9);"
        "CREATE TABLE volt (_idx integer, vr real, vi real, vm real, va real);"
        "INSERT INTO volt VALUES (0, 1.0198, 0.0204, 1.02, 0.02);"
        "INSERT INTO volt VALUES (1, 0.99, -0.0173, 0.99, -0.0175);"
        "CREATE TABLE gens (_idx integer, ibgen integer, id text, st integer, mbase real, pgen real, qgen real, qmax real, qmin real, pmax real, pmin real, vcsched real);"
        "INSERT INTO gens VALUES (0, 0, 'UnitA', 1, 100.0, 0.3, 0.01, 0.5, -0.5, 0.8, 0.0, 1.02);"
        "CREATE TABLE load (_idx integer, lbus integer, id text, st integer, p real, q real, ip real, iq real, g real, b real);"
        "INSERT INTO load VALUES (0, 1, '1', 1, 0.1, 0.03, 0.0, 0.0, 0.0, 0.0);"
        "CREATE TABLE secdd (_idx integer, ifrom integer, ito integer, ck text, st integer, zsecr real, zsecx real, bsec real, rate0 real);"
        "INSERT INTO secdd VALUES (0, 0, 1, '1', 1, 0.01, 0.1, 0.02, 100.0);"
        "CREATE TABLE tran (_idx integer, ifrom integer, ito integer, ck text, st integer, type integer, kreg integer, tbase real, zpsr real, zpsx real, zptr real, zptx real, ztsr real, ztsx real, rate0 real, tmax real, tmin real, vtmax real, vtmin real, stepp real, tapp real, midbus_t integer);"
        "INSERT INTO tran VALUES (0, 0, 1, '1', 1, 2, 1, 50.0, 0.02, 0.08, 0.0, 0.0, 0.0, 0.0, 100.0, 1.1, 0.9, 1.03, 0.99, 0.01, 1.0, NULL);"
        "CREATE TABLE shunt (_idx integer, ifrom integer, id text, st integer, g real, b real);"
        "INSERT INTO shunt VALUES (0, 1, '1', 1, 0.001, 0.002);");
    EXPECT_EQ(sqlite3_close(database), SQLITE_OK);
    return filePath;
}

TEST(SavReaderTests, RejectsLegacyOrMalformedSavBeforeObjectCreation)
{
    const auto filePath = std::filesystem::temp_directory_path() / "griddyn_not_a_pslf_sqlite.sav";
    {
        std::ofstream output(filePath, std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output << "legacy binary save data";
    }

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    EXPECT_THROW(griddyn::loadFile(simulation, filePath.string()), griddyn::InvalidParameterValue);
    EXPECT_EQ(simulation->getInt("totalbuscount"), 0);
    EXPECT_EQ(simulation->getInt("totallinkcount"), 0);
    std::error_code removeError;
    std::filesystem::remove(filePath, removeError);
}

TEST(SavReaderTests, LoadsValidatedPslfSqlitePowerFlowCase)
{
    const auto filePath = makeSavFixture();
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    ASSERT_NO_THROW(griddyn::loadFile(simulation, filePath.string()));

    EXPECT_EQ(simulation->getInt("totalbuscount"), 2);
    EXPECT_EQ(simulation->getInt("totallinkcount"), 2);
    EXPECT_EQ(simulation->getInt("gencount"), 1);
    EXPECT_EQ(simulation->getInt("loadcount"), 2);
    auto* transformer =
        dynamic_cast<griddyn::links::AdjustableTransformer*>(simulation->getLink(1));
    ASSERT_NE(transformer, nullptr);
    EXPECT_NEAR(transformer->get("r"), 0.04, 1.0e-12);
    EXPECT_NEAR(transformer->get("x"), 0.16, 1.0e-12);
    EXPECT_DOUBLE_EQ(transformer->get("control_mode"), 1.0);
    EXPECT_DOUBLE_EQ(transformer->get("controlbusid"), 102.0);
    EXPECT_NEAR(transformer->get("mintap"), 0.9, 1.0e-12);
    EXPECT_NEAR(transformer->get("maxtap"), 1.1, 1.0e-12);
    EXPECT_NEAR(transformer->get("vmin"), 0.99, 1.0e-12);
    EXPECT_NEAR(transformer->get("vmax"), 1.03, 1.0e-12);
    EXPECT_NEAR(transformer->get("stepsize"), 0.01, 1.0e-12);
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), griddyn::GridBus::BusType::SLK);
    EXPECT_NEAR(bus->getVoltage(), 1.02, 1.0e-3);
    EXPECT_NEAR(bus->getAngle(), 0.02, 1.0e-3);
    auto* shuntBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 102));
    ASSERT_NE(shuntBus, nullptr);
    bool foundFixedShunt = false;
    for (int index = 0; shuntBus->getLoad(index) != nullptr; ++index) {
        foundFixedShunt |= shuntBus->getLoad(index)->isFixedShunt();
    }
    EXPECT_TRUE(foundFixedShunt);

    std::error_code removeError;
    std::filesystem::remove(filePath, removeError);
}

TEST(SavReaderTests, MatchesSavGeneratorMachineIdentifierWhenLoadingDyd)
{
    const auto savPath = makeSavFixture();
    const auto dydPath = std::filesystem::temp_directory_path() / "griddyn_sav_machine_id.dyd";
    {
        std::ofstream output(dydPath);
        ASSERT_TRUE(output.is_open());
        // The DYD number is deliberately different from busd.extnum. PSLF
        // supplies BUS_A as the stable cross-file identifier in this case.
        output << "gencls 1 \"BUS_A\" 230.0 \"UnitA\" : #9 mva=100.0 3.0 0.0 /\n";
    }

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    ASSERT_NO_THROW(griddyn::loadFile(simulation, savPath.string()));
    std::vector<griddyn::GridBus*> buses;
    simulation->getBusVector(buses);
    ASSERT_EQ(buses.size(), 2U);
    ASSERT_NE(buses[0], nullptr);
    EXPECT_TRUE(buses[0]->getName().starts_with("BUS_A"));
    EXPECT_NO_THROW(griddyn::loadFile(simulation, dydPath.string()));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    EXPECT_NE(generator->find("genmodel"), nullptr);

    std::error_code removeError;
    std::filesystem::remove(savPath, removeError);
    std::filesystem::remove(dydPath, removeError);
}

}  // namespace
