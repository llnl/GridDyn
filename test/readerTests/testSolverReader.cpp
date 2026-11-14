/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "fileInput/readerHelper.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ZipLoad.h"
#include "units/units.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace griddyn {
namespace {
    constexpr std::string_view xmlTestDirectory{GRIDDYN_TEST_DIRECTORY "/xml_tests/"};
    constexpr std::string_view inputTestDirectory{GRIDDYN_TEST_DIRECTORY "/input_tests/"};

    class SolverReaderTests: public GridDynSimulationTestFixture, public ::testing::Test {};

    TEST_F(SolverReaderTests, NamedSolverCanBeSelectedAsDefault)
    {
        const std::string fileName =
            std::string{xmlTestDirectory} + "test_named_default_solver.xml";
        gds = readSimXMLFile(fileName);

        ASSERT_NE(gds, nullptr);
        const auto namedSolver = gds->getSolverInterface("basic");
        ASSERT_NE(namedSolver, nullptr);
        EXPECT_EQ(gds->getSolverInterface("powerflow"), namedSolver);
        EXPECT_EQ(readerConfig::warnCount, 0);
    }

    TEST_F(SolverReaderTests, RawLocalGeneratorVoltageTargetsAreOptIn)
    {
        const std::string path =
            std::string{GRIDDYN_TEST_DIRECTORY} + "/comparison_tests/ieee14.raw";
        for (const auto flags : {"",
                                 "use_generator_voltage_targets",
                                 "use_generator_voltage_targets,use_bus_voltage_targets"}) {
            SCOPED_TRACE(flags);
            ReaderInfo info;
            addFlags(info, flags);
            auto simulation = std::make_unique<GridDynSimulation>();
            loadFile(simulation, path, &info);
            auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
            ASSERT_NE(bus, nullptr);
            EXPECT_DOUBLE_EQ(bus->getVoltage(), 1.01970);
            const bool generatorTarget = info.checkFlag(USE_GENERATOR_VOLTAGE_TARGETS) &&
                !info.checkFlag(USE_BUS_VOLTAGE_TARGETS);
            EXPECT_DOUBLE_EQ(bus->get("vtarget"), generatorTarget ? 1.03 : 1.01970);
            ASSERT_NE(bus->getGen(0), nullptr);
            EXPECT_DOUBLE_EQ(bus->getGen(0)->get("qmax", units::MVAR), 15.0);
            EXPECT_DOUBLE_EQ(bus->getGen(0)->get("qmin", units::MVAR), -40.0);
        }
    }

    TEST_F(SolverReaderTests, RawLocalTargetOptionPreservesRemoteVoltageControl)
    {
        ReaderInfo info;
        addFlags(info, "use_generator_voltage_targets");
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation,
                 std::string{inputTestDirectory} + "raw_remote_voltage_control.raw",
                 &info);
        auto* terminal = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 3));
        auto* remote = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
        ASSERT_NE(terminal, nullptr);
        ASSERT_NE(remote, nullptr);
        EXPECT_DOUBLE_EQ(terminal->get("vtarget"), 0.98);
        EXPECT_DOUBLE_EQ(remote->get("vtarget"), 1.04);
        EXPECT_DOUBLE_EQ(terminal->getGen(0)->get("vtarget"), 1.04);
    }

    TEST_F(SolverReaderTests, RawLocalTargetsIgnoreDisabledGeneratorsAndRejectConflicts)
    {
        const auto sourcePath = std::string{inputTestDirectory} + "raw_local_voltage_targets.raw";
        ReaderInfo info;
        addFlags(info, "use_generator_voltage_targets");
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation, sourcePath, &info);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 3));
        ASSERT_NE(bus, nullptr);
        ASSERT_NE(bus->getGen(2), nullptr);
        EXPECT_FALSE(bus->getGen(2)->isEnabled());
        EXPECT_DOUBLE_EQ(bus->get("vtarget"), 1.04);
        EXPECT_DOUBLE_EQ(bus->getVoltage(), 0.98);

        std::ifstream source(sourcePath);
        ASSERT_TRUE(source.is_open());
        std::ostringstream content;
        content << source.rdbuf();
        auto conflicting = content.str();
        const auto position = conflicting.find("3,'2 ',10.000,0.000,200.000,-200.000,1.04000");
        ASSERT_NE(position, std::string::npos);
        conflicting.replace(conflicting.find("1.04000", position), 7, "1.06000");
        const auto temporary =
            std::filesystem::temp_directory_path() / "griddyn_conflicting_local_raw_targets.raw";
        {
            std::ofstream output(temporary);
            ASSERT_TRUE(output.is_open());
            output << conflicting;
        }
        auto rejected = std::make_unique<GridDynSimulation>();
        EXPECT_THROW(loadFile(rejected, temporary.string(), &info), std::runtime_error);
        // The default bus-target interpretation does not depend on these VS values.
        auto defaultTargets = std::make_unique<GridDynSimulation>();
        EXPECT_NO_THROW(loadFile(defaultTargets, temporary.string()));
        std::error_code error;
        std::filesystem::remove(temporary, error);
    }

    TEST_F(SolverReaderTests, ReactiveLimitOptionPreservesRawBounds)
    {
        for (const bool ignoreLimits : {false, true}) {
            SCOPED_TRACE(ignoreLimits);
            auto simulation = std::make_unique<GridDynSimulation>();
            loadFile(simulation,
                     std::string{inputTestDirectory} + "raw_remote_voltage_control.raw");
            auto* terminal = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 3));
            auto* remote = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
            ASSERT_NE(terminal, nullptr);
            ASSERT_NE(remote, nullptr);
            auto* generator = terminal->getGen(0);
            ASSERT_NE(generator, nullptr);
            generator->set("qmin", -1.0, units::MVAR);
            generator->set("qmax", 1.0, units::MVAR);
            simulation->setFlag("ignore_reactive_limits", ignoreLimits);
            ASSERT_EQ(simulation->powerflow(), 0);
            EXPECT_DOUBLE_EQ(generator->get("qmin", units::MVAR), -1.0);
            EXPECT_DOUBLE_EQ(generator->get("qmax", units::MVAR), 1.0);
            if (ignoreLimits) {
                EXPECT_NEAR(remote->getVoltage(), 1.04, 1e-6);
                EXPECT_GT(-generator->getReactivePower(), 0.01);
            } else {
                EXPECT_LE(-generator->getReactivePower(), 0.01 + 1e-6);
                EXPECT_GT(std::abs(remote->getVoltage() - 1.04), 1e-3);
            }
        }
    }

    TEST(MatlabArrayReaderTests, AcceptsNewlineRowsWithoutChangingSemicolonRows)
    {
        mArray semicolonRows;
        readMatlabArray("matrix = [1 2; 3 4];", 0, semicolonRows);
        ASSERT_EQ(semicolonRows.size(), 2U);
        EXPECT_EQ(semicolonRows[0], (std::vector<double>{1.0, 2.0}));
        EXPECT_EQ(semicolonRows[1], (std::vector<double>{3.0, 4.0}));

        mArray newlineRows;
        readMatlabArray("matrix = [1 2\r\n3 4\n];", 0, newlineRows);
        ASSERT_EQ(newlineRows.size(), 2U);
        EXPECT_EQ(newlineRows[0], (std::vector<double>{1.0, 2.0}));
        EXPECT_EQ(newlineRows[1], (std::vector<double>{3.0, 4.0}));

        mArray continuationRows;
        readMatlabArray("matrix = [1 2 ...\n3 4\n5 6];", 0, continuationRows);
        ASSERT_EQ(continuationRows.size(), 2U);
        EXPECT_EQ(continuationRows[0], (std::vector<double>{1.0, 2.0, 3.0, 4.0}));
        EXPECT_EQ(continuationRows[1], (std::vector<double>{5.0, 6.0}));
    }

    TEST_F(SolverReaderTests, RawNegativePhaseControlRetainsFixedTap)
    {
        const std::string fileName =
            std::string{inputTestDirectory} + "raw_negative_phase_control.raw";
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), fileName);

        auto* transformer =
            dynamic_cast<links::AdjustableTransformer*>(gds->find("padjtx_FROM BUS_to_TO BUS"));
        ASSERT_NE(transformer, nullptr);
        EXPECT_NEAR(transformer->getTap(), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->getTapAngle(),
                    units::convert(-26.0, units::deg, units::rad),
                    1.0e-12);

        ASSERT_EQ(gds->pFlowInitialize(), 0);
        EXPECT_NEAR(transformer->getTap(), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->getTapAngle(),
                    units::convert(-26.0, units::deg, units::rad),
                    1.0e-12);
    }

    TEST_F(SolverReaderTests, RawNegativeTransformerControlDirectionIsPreserved)
    {
        const std::string fileName =
            std::string{inputTestDirectory} + "raw_negative_transformer_control.raw";
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), fileName);

        auto* transformer = dynamic_cast<links::AdjustableTransformer*>(gds->getLink(0));
        ASSERT_NE(transformer, nullptr);
        EXPECT_DOUBLE_EQ(transformer->get("controlbusid"), 3.0);
        EXPECT_DOUBLE_EQ(transformer->get("direction"), -1.0);

        ASSERT_EQ(gds->pFlowInitialize(), 0);
        EXPECT_DOUBLE_EQ(transformer->get("controlbusid"), 3.0);
        EXPECT_DOUBLE_EQ(transformer->get("direction"), -1.0);
    }

    TEST_F(SolverReaderTests, RawThreeWindingTransformerCreatesStarEquivalent)
    {
        const std::string fileName =
            std::string{inputTestDirectory} + "raw_three_winding_transformer.raw";
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), fileName);

        ASSERT_EQ(gds->getInt("totalbuscount"), 4);
        ASSERT_EQ(gds->getInt("totallinkcount"), 3);
        const std::array<std::array<double, 4>, 3> expected{{
            {0.020, 0.200, 0.98, 2.0},
            {0.010, 0.100, 1.02, -1.0},
            {0.030, 0.300, 1.00, 0.0},
        }};
        for (std::size_t ii = 0; ii < expected.size(); ++ii) {
            const auto* leg = dynamic_cast<const AcLine*>(gds->getLink(static_cast<index_t>(ii)));
            ASSERT_NE(leg, nullptr);
            EXPECT_NEAR(leg->get("r"), expected[ii][0], 1.0e-12);
            EXPECT_NEAR(leg->get("x"), expected[ii][1], 1.0e-12);
            EXPECT_NEAR(leg->get("tap"), expected[ii][2], 1.0e-12);
            EXPECT_NEAR(leg->get("tapangle"),
                        units::convert(expected[ii][3], units::deg, units::rad),
                        1.0e-12);
            ASSERT_NE(leg->getBus(2), nullptr);
            EXPECT_EQ(leg->getBus(2), gds->getBus(3));
        }

        // MAG1/MAG2 belong to the first external/I-side winding.  They must
        // not be split between that leg and the synthetic star bus.
        const auto* primary = dynamic_cast<const AcLine*>(gds->getLink(0));
        ASSERT_NE(primary, nullptr);
        EXPECT_NEAR(primary->get("g1"), 0.002, 1.0e-12);
        EXPECT_NEAR(primary->get("b1"), -0.003, 1.0e-12);
        EXPECT_NEAR(primary->get("g2"), 0.0, 1.0e-12);
        EXPECT_NEAR(primary->get("b2"), 0.0, 1.0e-12);

        const auto* starBus = gds->getBus(3);
        ASSERT_NE(starBus, nullptr);
        EXPECT_EQ(starBus->getUserID(), 4);
        ASSERT_EQ(starBus->getInt("loadcount"), 1);
        EXPECT_NE(dynamic_cast<const loads::Svd*>(starBus->getLoad()), nullptr);
    }

    TEST_F(SolverReaderTests, AuxCoreNetworkImportsAndSolves)
    {
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), std::string{inputTestDirectory} + "aux_basic_network.aux");

        EXPECT_EQ(gds->getInt("totalbuscount"), 3);
        EXPECT_EQ(gds->getInt("totallinkcount"), 2);
        EXPECT_EQ(gds->getInt("gencount"), 1);
        auto* area = dynamic_cast<GridArea*>(gds->findByUserID("area", 1));
        auto* generatorBus = dynamic_cast<GridBus*>(gds->findByUserID("bus", 2));
        auto* loadBus = dynamic_cast<GridBus*>(gds->findByUserID("bus", 3));
        ASSERT_NE(area, nullptr);
        ASSERT_NE(generatorBus, nullptr);
        ASSERT_NE(loadBus, nullptr);
        EXPECT_EQ(generatorBus->getParent(), area);
        EXPECT_EQ(generatorBus->getType(), GridBus::BusType::PV);
        EXPECT_NEAR(generatorBus->get("vtarget"), 1.02, 1.0e-12);

        auto* generator = generatorBus->getGen();
        ASSERT_NE(generator, nullptr);
        // Generator getters use the bus injection sign convention.
        EXPECT_NEAR(generator->get("p", units::MW), -40.0, 1.0e-12);
        EXPECT_NEAR(generator->get("q", units::MVAR), -5.0, 1.0e-12);
        EXPECT_GT(generator->get("qmax", units::MVAR), 1.0e6);
        EXPECT_LT(generator->get("qmin", units::MVAR), -1.0e6);
        EXPECT_TRUE(generator->checkFlag(LOCAL_VOLTAGE_CONTROL));

        ZipLoad* load = nullptr;
        ZipLoad* shunt = nullptr;
        ZipLoad* busShunt = nullptr;
        for (index_t index = 0;; ++index) {
            auto* candidate = loadBus->getLoad(index);
            if (candidate == nullptr) {
                break;
            }
            auto* zip = dynamic_cast<ZipLoad*>(candidate);
            if (zip == nullptr) {
                continue;
            }
            if (zip->getName().ends_with("_load_A")) {
                load = zip;
            } else if (zip->getName().ends_with("_shunt_1")) {
                shunt = zip;
            } else if (zip->getName().ends_with("_shunt_bus")) {
                busShunt = zip;
            }
        }
        ASSERT_NE(load, nullptr);
        ASSERT_NE(shunt, nullptr);
        ASSERT_NE(busShunt, nullptr);
        EXPECT_NEAR(load->get("p", units::MW), 30.0, 1.0e-12);
        EXPECT_NEAR(load->get("q", units::MVAR), 10.0, 1.0e-12);
        EXPECT_NEAR(load->get("ip", units::MW), 2.0, 1.0e-12);
        EXPECT_NEAR(load->get("iq", units::MVAR), 1.0, 1.0e-12);
        EXPECT_NEAR(load->get("yp", units::MW), 3.0, 1.0e-12);
        EXPECT_NEAR(load->get("yq", units::MVAR), -2.0, 1.0e-12);
        EXPECT_NEAR(shunt->get("yq", units::MVAR), -4.0, 1.0e-12);
        EXPECT_NEAR(busShunt->get("yp", units::MW), 0.5, 1.0e-12);
        EXPECT_NEAR(busShunt->get("yq", units::MVAR), 1.5, 1.0e-12);

        auto* slackBus = dynamic_cast<GridBus*>(gds->findByUserID("bus", 1));
        ASSERT_NE(slackBus, nullptr);
        auto* line = dynamic_cast<AcLine*>(generatorBus->findLink(slackBus));
        ASSERT_NE(line, nullptr);
        EXPECT_NEAR(line->get("r"), 0.01, 1.0e-12);
        EXPECT_NEAR(line->get("x"), 0.10, 1.0e-12);
        EXPECT_NEAR(line->get("b"), 0.04, 1.0e-12);
        EXPECT_NEAR(line->get("g1"), 0.001, 1.0e-12);
        EXPECT_NEAR(line->get("g2"), 0.001, 1.0e-12);

        EXPECT_EQ(gds->powerflow(), 0);
        EXPECT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    }

    TEST_F(SolverReaderTests, AuxExplicitUppercaseFormatUsesReader)
    {
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds, std::string{inputTestDirectory} + "aux_basic_network.aux", nullptr, ".AUX");
        EXPECT_EQ(gds->getInt("totalbuscount"), 3);
        EXPECT_EQ(gds->getInt("totallinkcount"), 2);
    }

    TEST_F(SolverReaderTests, AuxCaseMetadataKeepsNamedReferencesAndBusAssignments)
    {
        const auto metadata =
            readAuxCaseMetadata(std::string{inputTestDirectory} + "aux_case_metadata.aux");
        ASSERT_EQ(metadata.mOwners.size(), 1);
        EXPECT_EQ(metadata.mOwners.at(7).mName, "First Owner");
        ASSERT_EQ(metadata.mZones.size(), 2);
        EXPECT_EQ(metadata.mZones.at(4).mName, "Inland");
        ASSERT_EQ(metadata.mSubstations.size(), 1);
        const auto& substation = metadata.mSubstations.at(20);
        EXPECT_EQ(substation.mName, "North Yard");
        EXPECT_EQ(substation.mLatitude, std::optional<double>{35.1});
        EXPECT_EQ(substation.mLongitude, std::optional<double>{-118.1});
        ASSERT_EQ(metadata.mSuperAreas.size(), 1);
        EXPECT_EQ(metadata.mSuperAreas.front(), "Western System");
        ASSERT_EQ(metadata.mBalancingAuthorities.size(), 1);
        EXPECT_EQ(metadata.mBalancingAuthorities.at(9).mName, "Test BA");
        ASSERT_EQ(metadata.mBuses.size(), 2);
        const auto& firstBus = metadata.mBuses.at(101);
        EXPECT_EQ(firstBus.mAreaNumber, 5);
        EXPECT_EQ(firstBus.mZoneNumber, 4);
        EXPECT_EQ(firstBus.mOwnerNumber, 7);
        EXPECT_EQ(firstBus.mSubstationNumber, 20);
        EXPECT_EQ(firstBus.mBalancingAuthorityNumber, 9);
        EXPECT_EQ(firstBus.mLatitude, std::optional<double>{35.2});
        EXPECT_EQ(firstBus.mLongitude, std::optional<double>{-118.2});
        EXPECT_FALSE(metadata.mBuses.at(102).mLatitude.has_value());
    }

    TEST_F(SolverReaderTests, AuxRemoteGeneratorRegulation)
    {
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), std::string{inputTestDirectory} + "aux_remote_voltage.aux");
        auto* target = dynamic_cast<GridBus*>(gds->findByUserID("bus", 2));
        auto* firstTerminal = dynamic_cast<GridBus*>(gds->findByUserID("bus", 3));
        auto* secondTerminal = dynamic_cast<GridBus*>(gds->findByUserID("bus", 4));
        ASSERT_NE(target, nullptr);
        ASSERT_NE(firstTerminal, nullptr);
        ASSERT_NE(secondTerminal, nullptr);
        auto* first = firstTerminal->getGen();
        auto* second = secondTerminal->getGen();
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(firstTerminal->getType(), GridBus::BusType::PQ);
        EXPECT_EQ(secondTerminal->getType(), GridBus::BusType::PQ);
        EXPECT_NEAR(target->get("vtarget"), 1.04, 1.0e-12);
        EXPECT_TRUE(first->checkFlag(REMOTE_VOLTAGE_CONTROL));
        EXPECT_TRUE(second->checkFlag(REMOTE_VOLTAGE_CONTROL));
        EXPECT_NEAR(first->get("vcontrolfrac"), 0.3, 1.0e-12);
        EXPECT_NEAR(second->get("vcontrolfrac"), 0.7, 1.0e-12);
        EXPECT_EQ(gds->pFlowInitialize(), 0);
        EXPECT_EQ(gds->powerflow(), 0);
        EXPECT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
        EXPECT_NEAR(target->getVoltage(), 1.04, 1.0e-6);
    }

    TEST_F(SolverReaderTests, AuxPhaseShifterUsesMWLimitsAndFromSideDirection)
    {
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), std::string{inputTestDirectory} + "aux_phase_shifter.aux");
        ASSERT_EQ(gds->getInt("totallinkcount"), 2);
        auto* transformer = dynamic_cast<links::AdjustableTransformer*>(gds->getLink(0));
        ASSERT_NE(transformer, nullptr);
        const double voltageRatio = 18.0 / 765.0;
        EXPECT_NEAR(transformer->get("r"), 0.18785 * voltageRatio * voltageRatio, 1.0e-12);
        EXPECT_NEAR(transformer->get("x"), 1.638269 * voltageRatio * voltageRatio, 1.0e-12);
        EXPECT_NEAR(transformer->get("tap"), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->get("tapangle"),
                    units::convert(-7.69582, units::deg, units::rad),
                    1.0e-12);
        EXPECT_NEAR(transformer->get("min"), 25.0, 1.0e-12);
        EXPECT_NEAR(transformer->get("max"), 28.0, 1.0e-12);
        EXPECT_NEAR(transformer->get("controlbusid"), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->get("direction"), -1.0, 1.0e-12);
        ASSERT_EQ(gds->pFlowInitialize(), 0);
        EXPECT_NEAR(transformer->get("tap"), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->get("direction"), -1.0, 1.0e-12);
    }

    TEST_F(SolverReaderTests, AuxThreeWindingReusesAndNormalizesStarLegs)
    {
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), std::string{inputTestDirectory} + "aux_three_winding.aux");
        ASSERT_EQ(gds->getInt("totalbuscount"), 4);
        ASSERT_EQ(gds->getInt("totallinkcount"), 3);
        auto* star = dynamic_cast<GridBus*>(gds->findByUserID("bus", 40));
        ASSERT_NE(star, nullptr);
        const std::array<int, 3> terminalIDs{10, 20, 30};
        const std::array<double, 3> expectedR{0.01, 0.02, 0.03};
        const std::array<double, 3> expectedX{0.10, 0.20, 0.30};
        for (size_t index = 0; index < terminalIDs.size(); ++index) {
            auto* terminal = dynamic_cast<GridBus*>(gds->findByUserID("bus", terminalIDs[index]));
            ASSERT_NE(terminal, nullptr);
            auto* leg = dynamic_cast<AcLine*>(terminal->findLink(star));
            ASSERT_NE(leg, nullptr);
            EXPECT_NEAR(leg->get("r"), expectedR[index], 1.0e-12);
            EXPECT_NEAR(leg->get("x"), expectedX[index], 1.0e-12);
            EXPECT_NEAR(leg->get("tap"), 1.0, 1.0e-12);
            EXPECT_TRUE(leg->isEnabled());
            if (index == 0) {
                EXPECT_NEAR(leg->get("tapangle"),
                            units::convert(5.0, units::deg, units::rad),
                            1.0e-12);
            }
        }
    }

    TEST_F(SolverReaderTests, AuxActivsg10kPowerFlowWhenCaseIsAvailable)
    {
        const char* path = std::getenv("GRIDDYN_ACTIVSG10K_AUX");
        if (path == nullptr || *path == '\0') {
            GTEST_SKIP() << "Set GRIDDYN_ACTIVSG10K_AUX to run the full-case regression";
        }
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), path);
        ASSERT_EQ(gds->getInt("totalbuscount"), 10000);
        ASSERT_EQ(gds->getInt("totallinkcount"), 12706);
        ASSERT_EQ(gds->powerflow(), 0);
        EXPECT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    }

    TEST_F(SolverReaderTests, AuxActivsg10kMetadataWhenCaseIsAvailable)
    {
        const char* path = std::getenv("GRIDDYN_ACTIVSG10K_AUX");
        if (path == nullptr || *path == '\0') {
            GTEST_SKIP() << "Set GRIDDYN_ACTIVSG10K_AUX to run the full-case metadata regression";
        }
        const auto metadata = readAuxCaseMetadata(path);
        EXPECT_EQ(metadata.mBuses.size(), 10000);
        EXPECT_FALSE(metadata.mOwners.empty());
        EXPECT_FALSE(metadata.mZones.empty());
        EXPECT_FALSE(metadata.mSubstations.empty());
        const auto& firstBus = metadata.mBuses.at(10001);
        EXPECT_EQ(firstBus.mSubstationNumber, 10000);
        ASSERT_TRUE(firstBus.mLatitude.has_value());
        ASSERT_TRUE(firstBus.mLongitude.has_value());
    }
}  // namespace
}  // namespace griddyn
