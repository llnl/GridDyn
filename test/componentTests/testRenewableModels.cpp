/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "fileInput/ReaderInfo.h"
#include "fileInput/fileInput.h"
#include "griddyn/ControlSignalRouting.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/controllers/Scheduler.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/genmodels/GenModelClassical.h"
#include "griddyn/genmodels/GenModelGENROU.h"
#include "griddyn/genmodels/GenModelInverter.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/relays/BusMeasurementSensor.h"
#include "griddyn/relays/COISensor.h"
#include "griddyn/relays/DGProtectionRelay.h"
#include "griddyn/renewables/DistributedConverter.h"
#include "griddyn/renewables/GridFormingConverter.h"
#include "griddyn/renewables/REECA1.h"
#include "griddyn/renewables/REECA1E.h"
#include "griddyn/renewables/REECA1G.h"
#include "griddyn/renewables/REECB1.h"
#include "griddyn/renewables/REGCA1.h"
#include "griddyn/renewables/REGCP1.h"
#include "griddyn/renewables/REPCA1.h"
#include "griddyn/renewables/WT3E1.h"
#include "griddyn/renewables/WT3G1.h"
#include "griddyn/renewables/WT4E1.h"
#include "griddyn/renewables/WT4G1.h"
#include "griddyn/renewables/WTARA1.h"
#include "griddyn/renewables/WTDS.h"
#include "griddyn/renewables/WTDTA1.h"
#include "griddyn/renewables/WTPTA1.h"
#include "griddyn/renewables/WTTQA1.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;

namespace {
std::string renewableDyrRecord(std::string_view model,
                               int busId = 101,
                               int repcaVbus = 0,
                               int repcaFflag = 0,
                               int repcaBranchFrom = 0,
                               int repcaBranchTo = 0)
{
    if (model == "PLL1") {
        return std::to_string(busId) + " 'PLL1' 'pll1a' 1 2 .02 .03 60 /\n";
    }
    if (model == "PLL2") {
        return std::to_string(busId) + " 'PLL2' 'pll2a' 1 2 60 /\n";
    }
    if (model == "FREQDIV") {
        return std::to_string(busId) + " 'FREQDIV' 'frequency' /\n";
    }
    if (model == "REGCP1_PLL") {
        return std::to_string(busId) +
            " 'REGCP1' '1' 1 .1 10 1 .5 1 1.2 .8 .4 -1.5 .1 .7 1 -1 0 'pll1a' /\n";
    }
    std::string payload;
    if (model == "REGCA1" || model == "REGCP1") {
        payload = "1 .1 10 1 .5 1 1.2 .8 .4 -1.5 .1 .7 1 -1 0";
    } else if (model == "REECA1" || model == "REECA1E" || model == "REECA1G") {
        payload = "0 0 1 0 0 1 .8 1.2 .02 -.02 .02 1 999 -999 0 0 0 0 "
                  ".02 999 -999 999 -999 1 .1 1 .1 1 .02 999 -999 999 0 999 .02 "
                  ".2 2 .4 4 .8 8 1 10 .2 2 .4 4 .8 8 1 12";
        if (model == "REECA1E") {
            payload += " 4 .5 'freq1'";
        } else if (model == "REECA1G") {
            payload += " 4 'sync1'";
        }
    } else if (model == "REECB1") {
        payload = "0 0 1 0 1 .8 1.2 .02 -.02 .02 1 999 -999 0 .02 "
                  "999 -999 999 -999 1 .1 1 .1 .02 999 -999 999 0 999 .02";
    } else if (model == "REPCA1") {
        payload = std::to_string(repcaVbus) + " " + std::to_string(repcaBranchFrom) + " " +
            std::to_string(repcaBranchTo) + " '1' 0 1 " + std::to_string(repcaFflag) +
            " .02 1 .1 1 1 .8 0 0 0 999 -999 -.1 .1 999 -999 1 .1 .02 "
            "-.0002833 .0002833 .05 -.05 999 -999 .02 10 10";
    } else if (model == "WTDTA1") {
        payload = "3 0 .5 1 1";
    } else if (model == "WTDS") {
        payload = "3 1 1";
    } else if (model == "BUSROCOF") {
        payload = "'freq1' .02 .1 .1 60";
    } else if (model == "WTARA1") {
        payload = "1 0";
    } else if (model == "WTPTA1") {
        payload = ".1 0 .1 0 0 .3 30 0 5 -5";
    } else if (model == "WTTQA1") {
        payload = "0 0 .1 .05 30 3 0 .4 .58 .8 .72 1.2 .86 1.6 1 999";
    } else if (model == "REGCV1") {
        payload = "60 .01 0 0 10 0 0 .2 .5 .02 .5 .02 .2 .01 .2 .01";
    } else if (model == "REGCV2") {
        payload = "60 0 0 10 0 0 .2 .5 .02 .5 .02 .01 .01";
    } else if (model == "REGF1" || model == "REGF2" || model == "REGF3") {
        payload = "60 0 .2 75 -75 .033 .045 .005 .005 .5 20 3 10 "
                  "1 -1 5 30 1 -1 .1 1.5 .025";
        if (model == "REGF2") {
            payload += " .15 .11 'pll2a'";
        }
    }
    return std::to_string(busId) + " '" + std::string{model} + "' '1' " + payload + " /\n";
}

std::unique_ptr<GridDynSimulation> renewableDyrSimulation()
{
    auto simulation = std::make_unique<GridDynSimulation>();
    auto* bus = new AcBus("bus101");
    bus->setUserID(101);
    simulation->add(bus);
    auto* generator = new Generator("bus101_Gen_1");
    generator->setUserID(77);
    generator->set("p", 0.8);
    generator->set("q", 0.1);
    generator->set("mbase", 50.0, units::MVAR);
    bus->add(generator);
    return simulation;
}

void loadRenewableRecords(GridDynSimulation& simulation,
                          const std::vector<std::string_view>& models,
                          int busId = 101,
                          int repcaVbus = 0,
                          int repcaFflag = 0,
                          int repcaBranchFrom = 0,
                          int repcaBranchTo = 0)
{
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_renewable_" + std::to_string(simulation.getID()) + ".dyr");
    {
        std::ofstream stream(file);
        for (auto model : models) {
            auto record = renewableDyrRecord(
                model, busId, repcaVbus, repcaFflag, repcaBranchFrom, repcaBranchTo);
            if (model == "REECA1" &&
                std::find(models.begin(), models.end(), std::string_view{"WTDS"}) != models.end()) {
                const auto flags = record.find("0 0 1 0 0 1 .8");
                if (flags != std::string::npos) {
                    record.replace(flags, 14, "0 0 1 0 1 1 .8");
                }
            }
            stream << record;
        }
    }
    try {
        loadDyr(&simulation, file.string(), BasicReaderInfo{});
    }
    catch (...) {
        std::filesystem::remove(file);
        throw;
    }
    std::filesystem::remove(file);
}

void loadOtherMachines(GridDynSimulation& simulation)
{
    const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14_gencls.dyr";
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_renewable_other_" + std::to_string(simulation.getID()) + ".dyr");
    {
        std::ifstream input(source);
        std::ofstream output(file);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.starts_with("2 'GENCLS'")) {
                output << line << '\n';
            }
        }
    }
    try {
        loadDyr(&simulation, file.string(), BasicReaderInfo{});
    }
    catch (...) {
        std::filesystem::remove(file);
        throw;
    }
    std::filesystem::remove(file);
}

enum class RenewableDynamicProfile : std::uint8_t {
    gridFollowing,
    speedCoupledWind,
    gridForming,
    type3Wind,
    type4Wind
};

constexpr std::array renewableDynamicProfiles{
    RenewableDynamicProfile::gridFollowing,
    RenewableDynamicProfile::speedCoupledWind,
    RenewableDynamicProfile::gridForming,
    RenewableDynamicProfile::type3Wind,
    RenewableDynamicProfile::type4Wind,
};

std::string_view renewableDynamicProfileName(RenewableDynamicProfile profile)
{
    switch (profile) {
        case RenewableDynamicProfile::gridFollowing:
            return "grid-following solar";
        case RenewableDynamicProfile::speedCoupledWind:
            return "speed-coupled wind";
        case RenewableDynamicProfile::gridForming:
            return "grid-forming";
        case RenewableDynamicProfile::type3Wind:
            return "type 3 wind";
        case RenewableDynamicProfile::type4Wind:
            return "type 4 wind";
    }
    return "unknown renewable profile";
}

std::unique_ptr<GridDynSimulation> renewableNetworkCase(RenewableDynamicProfile profile,
                                                        std::string_view solverMethod)
{
    const auto xml = std::filesystem::path(__FILE__).parent_path().parent_path() / "reference" /
        "renewable_fault" / "two_bus.xml";
    auto simulation = std::make_unique<GridDynSimulation>();
    loadFile(simulation.get(), xml.string());
    simulation->consolePrintLevel = PrintLevel::NO_PRINT;
    simulation->set("dynamicsolvermethod", solverMethod);
    simulation->set("defdyndiff", "basicode");
    simulation->set("timestep", 0.005);

    auto* bus = dynamic_cast<GridBus*>(simulation->find("solar_bus"));
    if (bus == nullptr) {
        throw std::runtime_error("renewable two-bus test case has no solar_bus");
    }
    auto* host = new RenewableGenerator("solar");
    host->set("p", 0.6);
    host->set("mbase", 100.0, units::MVAR);

    switch (profile) {
        case RenewableDynamicProfile::gridFollowing: {
            auto* converter = new REGCA1;
            converter->set("tg", 0.02);
            converter->set("tfltr", 0.02);
            converter->set("iqrmax", 999.0);
            converter->set("iqrmin", -999.0);
            converter->set("lvplsw", 0.0);
            auto* control = new REECA1;
            control->set("vflag", 0.0);
            control->set("pqflag", 0.0);
            control->set("thld2", 0.5);
            control->set("tpord", 0.0);
            control->set("imax", 1.3);
            control->set("qmax", 1.0);
            control->set("qmin", -1.0);
            for (int index = 1; index <= 4; ++index) {
                const auto suffix = std::to_string(index);
                control->set("iq" + suffix, 1.3);
                control->set("ip" + suffix, 1.3);
            }
            auto* plant = new REPCA1;
            plant->set("vcflag", 0.0);
            plant->set("refflag", 1.0);
            plant->set("fflag", 0.0);
            plant->set("plflag", 0.0);
            host->add(converter);
            host->add(control);
            host->add(plant);
            break;
        }
        case RenewableDynamicProfile::speedCoupledWind: {
            auto* converter = new REGCP1;
            converter->set("tg", 0.02);
            converter->set("tfltr", 0.02);
            converter->set("iqrmax", 999.0);
            converter->set("iqrmin", -999.0);
            converter->set("lvplsw", 0.0);
            auto* control = new REECA1;
            control->set("vflag", 0.0);
            control->set("pqflag", 0.0);
            control->set("pflag", 1.0);
            control->set("tpord", 0.02);
            control->set("imax", 1.3);
            control->set("qmax", 1.0);
            control->set("qmin", -1.0);
            auto* shaft = new WTDS;
            host->add(converter);
            host->add(control);
            host->add(shaft);
            break;
        }
        case RenewableDynamicProfile::gridForming:
            host->add(new REGCV1);
            break;
        case RenewableDynamicProfile::type3Wind:
            host->add(new WT3G1);
            host->add(new WT3E1);
            break;
        case RenewableDynamicProfile::type4Wind:
            host->add(new WT4G1);
            host->add(new WT4E1);
            break;
    }
    bus->add(host);
    return simulation;
}

std::unique_ptr<RenewableGenerator> renewableDynamicHost(RenewableDynamicProfile profile)
{
    auto host = std::make_unique<RenewableGenerator>("renewable");
    host->set("mbase", 100.0, units::MVAR);
    switch (profile) {
        case RenewableDynamicProfile::gridFollowing: {
            auto* converter = new REGCA1;
            converter->set("lvplsw", 0.0);
            auto* control = new REECA1;
            control->set("vflag", 0.0);
            control->set("pqflag", 0.0);
            control->set("tpord", 0.0);
            auto* plant = new REPCA1;
            plant->set("vcflag", 0.0);
            plant->set("refflag", 1.0);
            plant->set("fflag", 0.0);
            plant->set("plflag", 0.0);
            host->add(converter);
            host->add(control);
            host->add(plant);
            break;
        }
        case RenewableDynamicProfile::speedCoupledWind: {
            auto* converter = new REGCP1;
            converter->set("lvplsw", 0.0);
            auto* control = new REECA1;
            control->set("vflag", 0.0);
            control->set("pqflag", 0.0);
            control->set("pflag", 1.0);
            control->set("tpord", 0.02);
            host->add(converter);
            host->add(control);
            host->add(new WTDS);
            break;
        }
        case RenewableDynamicProfile::gridForming:
            host->add(new REGCV1);
            break;
        case RenewableDynamicProfile::type3Wind:
            host->add(new WT3G1);
            host->add(new WT3E1);
            break;
        case RenewableDynamicProfile::type4Wind:
            host->add(new WT4G1);
            host->add(new WT4E1);
            break;
    }
    return host;
}

void checkRenewablePartitionedHost(RenewableGenerator& host,
                                   const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   std::string_view profile)
{
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB(inputs, desiredOutput, fields);
    auto algMode = cDynAlgSolverMode;
    auto diffMode = cDynDiffSolverMode;
    algMode.pairedOffsetIndex = diffMode.offsetIndex;
    diffMode.pairedOffsetIndex = algMode.offsetIndex;
    static_cast<void>(host.stateSize(cDaeSolverMode));
    static_cast<void>(host.stateSize(algMode));
    static_cast<void>(host.stateSize(diffMode));
    host.setOffset(0, cDaeSolverMode);
    host.setOffset(0, algMode);
    host.setOffset(0, diffMode);
    std::vector<double> algState(host.stateSize(algMode));
    std::vector<double> diffState(host.stateSize(diffMode));
    std::vector<double> rates(diffState.size());
    host.guessState(0.0, algState.data(), nullptr, algMode);
    host.guessState(0.0, diffState.data(), rates.data(), diffMode);

    const auto makeStateData = [&](const SolverMode& mode,
                                   std::vector<double>& state,
                                   const std::vector<double>& activeRates) {
        StateData stateData(0.0, state.data(), activeRates.data());
        stateData.stateSize = static_cast<count_t>(state.size());
        if (isAlgebraicOnly(mode)) {
            stateData.diffState = diffState.data();
            stateData.pairIndex = diffMode.offsetIndex;
        } else {
            stateData.algState = algState.data();
            stateData.pairIndex = algMode.offsetIndex;
        }
        return stateData;
    };

    auto algData = makeStateData(algMode, algState, rates);
    std::vector<double> algResidual(algState.size());
    host.residual(inputs, algData, algResidual.data(), algMode);
    for (const auto value : algResidual) {
        EXPECT_NEAR(value, 0.0, 1e-8) << profile << " algebraic residual";
    }
    std::vector<double> updatedAlgebraic(algState.size());
    host.algebraicUpdate(inputs, algData, updatedAlgebraic.data(), algMode, 1.0);
    for (std::size_t index = 0; index < algState.size(); ++index) {
        EXPECT_NEAR(updatedAlgebraic[index], algState[index], 1e-8)
            << profile << " algebraic update " << index;
    }

    auto diffData = makeStateData(diffMode, diffState, rates);
    std::vector<double> calculatedRates(diffState.size());
    host.derivative(inputs, diffData, calculatedRates.data(), diffMode);
    std::vector<double> diffResidual(diffState.size());
    host.residual(inputs, diffData, diffResidual.data(), diffMode);
    for (std::size_t index = 0; index < diffState.size(); ++index) {
        EXPECT_NEAR(calculatedRates[index], rates[index], 1e-8)
            << profile << " derivative " << index;
        EXPECT_NEAR(diffResidual[index], 0.0, 1e-8)
            << profile << " differential residual " << index;
    }

    const auto checkJacobian = [&](const SolverMode& mode, bool algebraic) {
        auto& activeState = algebraic ? algState : diffState;
        auto& activeRates = rates;
        auto stateData = makeStateData(mode, activeState, activeRates);
        stateData.cj = 0.0;
        MatrixDataSparse<double> jacobian;
        host.jacobianElements(
            inputs, stateData, jacobian, IOlocs(inputs.size(), kNullLocation), mode);
        jacobian.compact();
        const auto evaluate = [&](std::vector<double>& values) {
            auto trialData = makeStateData(mode, values, activeRates);
            std::vector<double> result(values.size());
            if (algebraic) {
                host.residual(inputs, trialData, result.data(), mode);
            } else {
                host.derivative(inputs, trialData, result.data(), mode);
            }
            return result;
        };
        const auto base = evaluate(activeState);
        constexpr double step = 1e-7;
        for (std::size_t column = 0; column < activeState.size(); ++column) {
            activeState[column] += step;
            const auto shifted = evaluate(activeState);
            activeState[column] -= step;
            for (std::size_t row = 0; row < activeState.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                            (shifted[row] - base[row]) / step,
                            4e-4)
                    << profile << (algebraic ? " algebraic" : " differential") << " row " << row
                    << " column " << column;
            }
        }
    };
    checkJacobian(algMode, true);
    checkJacobian(diffMode, false);
}
}  // namespace

TEST(ControlSignalRouting, ScaledComputedInputPropagatesSparseJacobian)
{
    ControlSignalRouting routing;
    ControlSignalRoute computed;
    computed.inputIndex = 0;
    computed.gain = 3.0;
    computed.offset = -1.0;
    computed.value = [](const ControlSignalContext& context) {
        return (context.hostInputs[0] * context.hostInputs[1]) +
            (2.0 * context.hostInputs[1]);
    };
    computed.derivatives = [](const ControlSignalContext& context,
                              std::vector<ControlSignalDerivative>& terms) {
        terms.push_back({.location = (*context.hostInputLocs)[0],
                         .value = context.hostInputs[1]});
        terms.push_back({.location = (*context.hostInputLocs)[1],
                         .value = context.hostInputs[0] + 2.0});
    };
    routing.add(std::move(computed));

    ControlSignalRoute direct;
    direct.inputIndex = 1;
    direct.value = [](const ControlSignalContext& context) { return context.hostInputs[2]; };
    direct.derivatives = [](const ControlSignalContext& context,
                            std::vector<ControlSignalDerivative>& terms) {
        terms.push_back({.location = (*context.hostInputLocs)[2], .value = 1.0});
    };
    routing.add(std::move(direct));

    ControlSignalRoute neutral;
    neutral.inputIndex = 2;
    neutral.value = [](const ControlSignalContext&) { return 0.0; };
    routing.add(std::move(neutral));

    const IOdata inputs{4.0, 5.0, 6.0};
    const IOlocs inputLocs{10, 11, 12};
    const ControlSignalContext context{.hostInputs = inputs,
                                       .hostInputLocs = &inputLocs,
                                       .stateData = emptyStateData,
                                       .solverMode = cDaeSolverMode};
    EXPECT_EQ(routing.values(context), (IOdata{89.0, 6.0, 0.0}));
    IOdata hostBuffer{1.0, 1.0, 1.0, 42.0};
    routing.writeValues(context, hostBuffer);
    EXPECT_EQ(hostBuffer, (IOdata{89.0, 6.0, 0.0, 42.0}));

    const auto locations = routing.inputLocations(context);
    EXPECT_TRUE(locations.needsTranslation());
    EXPECT_EQ(locations.locations[1], 12);
    EXPECT_EQ(locations.locations[2], kNullLocation);
    MatrixDataSparse<double> jacobian;
    locations.assign(jacobian, 3, locations.locations[0], 2.0);
    locations.assign(jacobian, 3, locations.locations[1], 7.0);
    jacobian.compact();
    EXPECT_DOUBLE_EQ(jacobian.at(3, 10), 30.0);
    EXPECT_DOUBLE_EQ(jacobian.at(3, 11), 36.0);
    EXPECT_DOUBLE_EQ(jacobian.at(3, 12), 7.0);
}

TEST(ControlSignalRouting, HostFrameRoutesValuesAndSparseDerivatives)
{
    ControlSignalRouting routing;
    routing.addInput(0, 0, "direct source", 2.0, 1.0);
    routing.addInput(1, 1, "computed source");

    const IOdata values{3.0, 4.0};
    const IOlocs directLocations{10, kNullLocation};
    const std::vector<std::vector<ControlSignalDerivative>> sparse{
        {}, {{.location = 20, .value = 0.5}, {.location = 21, .value = -1.0}}};
    const ControlSignalContext context{.hostInputs = values,
                                       .hostInputLocs = &directLocations,
                                       .stateData = emptyStateData,
                                       .solverMode = cDaeSolverMode,
                                       .hostInputDerivatives = &sparse};
    EXPECT_EQ(routing.values(context), (IOdata{7.0, 4.0}));

    const auto locations = routing.inputLocations(context);
    std::vector<ControlSignalDerivative> expanded;
    locations.appendExpanded(locations.locations[1], 2.0, expanded);
    ASSERT_EQ(expanded.size(), 2);
    EXPECT_EQ(expanded[0].location, 20);
    EXPECT_DOUBLE_EQ(expanded[0].value, 1.0);
    EXPECT_EQ(expanded[1].location, 21);
    EXPECT_DOUBLE_EQ(expanded[1].value, -2.0);
    MatrixDataSparse<double> jacobian;
    locations.assign(jacobian, 3, locations.locations[0], 2.0);
    locations.assign(jacobian, 3, locations.locations[1], 3.0);
    jacobian.compact();
    EXPECT_DOUBLE_EQ(jacobian.at(3, 10), 4.0);
    EXPECT_DOUBLE_EQ(jacobian.at(3, 20), 1.5);
    EXPECT_DOUBLE_EQ(jacobian.at(3, 21), -3.0);

    ControlSignalInputLocations reused;
    routing.writeInputLocations(context, reused);
    EXPECT_TRUE(reused.needsTranslation());
    ControlSignalRouting directOnly;
    directOnly.addInput(0, 0, "direct source");
    directOnly.writeInputLocations(context, reused);
    EXPECT_FALSE(reused.needsTranslation());
    EXPECT_EQ(reused.locations, (IOlocs{10}));
}

TEST(RenewableModels, FactoryAndRoleReplacement)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> hostObject(factory->createObject("generator", "renewable_dynamic"));
    std::unique_ptr<CoreObject> modelObject(factory->createObject("renewable_model", "regca1"));
    ASSERT_NE(dynamic_cast<RenewableGenerator*>(hostObject.get()), nullptr);
    ASSERT_NE(dynamic_cast<REGCA1*>(modelObject.get()), nullptr);

    RenewableGenerator host;
    EXPECT_THROW(host.dynInitializeA(0.0, 0), InvalidParameterValue);
    auto* first = new REGCA1("first");
    host.add(first);
    EXPECT_EQ(host.find("electrical"), first);
    auto* second = new REGCA1("second");
    host.add(second);
    EXPECT_EQ(host.find("electrical"), second);
    EXPECT_EQ(host.getSubObject("renewable_component", 0), second);
    std::unique_ptr<CoreObject> copy(host.clone());
    auto* copiedHost = dynamic_cast<RenewableGenerator*>(copy.get());
    ASSERT_NE(copiedHost, nullptr);
    auto* copiedConverter = dynamic_cast<REGCA1*>(copiedHost->find("electrical"));
    ASSERT_NE(copiedConverter, nullptr);
    EXPECT_NE(copiedConverter, second);
    host.remove(second);
    EXPECT_EQ(host.find("electrical"), nullptr);
}

TEST(RenewableModels, SignalRoutesFreezeUntilFullDynamicInitialization)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.dynInitializeA(0.0, 0);

    auto replacement = std::make_unique<REGCA1>();
    EXPECT_THROW(host.add(replacement.get()), InvalidParameterValue);
    EXPECT_THROW(host.remove(host.find("electrical")), InvalidParameterValue);

    host.resetSignalRoutesForDynamicInitialization();
    EXPECT_THROW(host.timestep(0.01, {1.0, 0.0}, cLocalSolverMode), InvalidParameterValue);
    auto* replacementModel = replacement.get();
    host.add(replacementModel);
    EXPECT_EQ(replacement.release(), replacementModel);
    EXPECT_NO_THROW(host.dynInitializeA(0.0, 0));
}

TEST(RenewableModels, GridFormingVariantsInitializeAndRespond)
{
    auto factory = CoreObjectFactory::instance();
    for (const auto name : {"regcv1", "regcv2", "regf1", "regf2", "regf3"}) {
        std::unique_ptr<CoreObject> object(factory->createObject("renewable_model", name));
        auto* model = dynamic_cast<GridFormingConverter*>(object.get());
        ASSERT_NE(model, nullptr) << name;
        if (std::string_view{name} == "regf2") {
            EXPECT_THROW(model->dynInitializeA(0.0, 0), InvalidParameterValue);
            model->set("pll", std::string_view{"pll2a"});
            EXPECT_EQ(model->sourceName(RenewableSignal::frequencyDeviation), "pll2a");
        }
        model->dynInitializeA(0.0, 0);
        IOdata inputs =
            std::string_view{name} == "regf2" ? IOdata{1.0, 0.1, 0.0} : IOdata{1.0, 0.1};
        IOdata fields;
        model->dynInitializeB(inputs, {0.6, 0.1}, fields);
        const auto states = model->getStates();
        EXPECT_EQ(states.size(), model->localStateNames().size()) << name;
        std::vector<double> residual(states.size());
        model->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        for (double value : residual) {
            EXPECT_NEAR(value, 0.0, 1e-9) << name;
        }
        EXPECT_EQ(model->getOutputs(inputs, emptyStateData, cLocalSolverMode), (IOdata{0.6, 0.1}));
        inputs[0] = 0.98;
        model->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        EXPECT_TRUE(std::any_of(residual.begin(), residual.end(), [](double value) {
            return std::abs(value) > 1e-6;
        })) << name;
        std::unique_ptr<CoreObject> copy(model->clone());
        EXPECT_NE(dynamic_cast<GridFormingConverter*>(copy.get()), nullptr) << name;
    }
}

TEST(RenewableModels, GridFormingVariantsDaeJacobians)
{
    for (const auto name : {"regcv1", "regcv2", "regf1", "regf2", "regf3"}) {
        std::unique_ptr<CoreObject> object(
            CoreObjectFactory::instance()->createObject("renewable_model", name));
        auto* model = dynamic_cast<GridFormingConverter*>(object.get());
        ASSERT_NE(model, nullptr);
        if (std::string_view{name} == "regf2") {
            model->set("pll", std::string_view{"pll2a"});
        }
        model->dynInitializeA(0.0, 0);
        IOdata inputs =
            std::string_view{name} == "regf2" ? IOdata{1.0, 0.1, 0.0} : IOdata{1.0, 0.1};
        IOdata fields;
        model->dynInitializeB(inputs, {0.6, 0.1}, fields);
        model->setOffset(0, cDaeSolverMode);
        std::vector<double> state(model->stateSize(cDaeSolverMode));
        std::vector<double> rate(state.size());
        model->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
        StateData data(0.0, state.data(), rate.data());
        data.stateSize = static_cast<count_t>(state.size());
        MatrixDataSparse<double> jacobian;
        IOlocs locations{40, 41};
        if (inputs.size() == 3) {
            locations.push_back(42);
        }
        model->jacobianElements(inputs, data, jacobian, locations, cDaeSolverMode);
        const auto calc = [&]() {
            std::vector<double> result(state.size());
            model->residual(inputs, data, result.data(), cDaeSolverMode);
            return result;
        };
        const auto base = calc();
        constexpr double perturbation = 1e-7;
        for (std::size_t column = 0; column < state.size(); ++column) {
            state[column] += perturbation;
            const auto shifted = calc();
            for (std::size_t row = 0; row < state.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                            (shifted[row] - base[row]) / perturbation,
                            1e-4)
                    << name;
            }
            state[column] -= perturbation;
        }
        for (std::size_t column = 0; column < inputs.size(); ++column) {
            inputs[column] += perturbation;
            const auto shifted = calc();
            for (std::size_t row = 0; row < state.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), locations[column]),
                            (shifted[row] - base[row]) / perturbation,
                            1e-4)
                    << name;
            }
            inputs[column] -= perturbation;
        }
    }
}

TEST(RenewableModels, GridFormingReadsPairedSolverStates)
{
    REGCV1 model;
    model.dynInitializeA(0.0, 0);
    IOdata fields;
    model.dynInitializeB({1.0, 0.1}, {0.6, 0.1}, fields);
    auto algMode = cDynAlgSolverMode;
    auto diffMode = cDynDiffSolverMode;
    algMode.pairedOffsetIndex = diffMode.offsetIndex;
    diffMode.pairedOffsetIndex = algMode.offsetIndex;
    std::vector<double> alg(model.stateSize(algMode));
    std::vector<double> diff(model.stateSize(diffMode));
    std::vector<double> rate(diff.size());
    model.setOffset(0, algMode);
    model.setOffset(0, diffMode);
    model.guessState(0.0, alg.data(), rate.data(), algMode);
    model.guessState(0.0, diff.data(), rate.data(), diffMode);
    StateData algData(0.0, alg.data(), rate.data());
    algData.stateSize = static_cast<count_t>(alg.size());
    algData.diffState = diff.data();
    algData.pairIndex = diffMode.offsetIndex;
    StateData diffData(0.0, diff.data(), rate.data());
    diffData.stateSize = static_cast<count_t>(diff.size());
    diffData.algState = alg.data();
    diffData.pairIndex = algMode.offsetIndex;
    std::vector<double> algResidual(alg.size());
    std::vector<double> diffResidual(diff.size());
    model.residual({1.0, 0.1}, algData, algResidual.data(), algMode);
    model.residual({1.0, 0.1}, diffData, diffResidual.data(), diffMode);
    for (double value : algResidual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    for (double value : diffResidual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    diff[6] += 0.01;  // converter d-axis voltage state
    model.residual({1.0, 0.1}, algData, algResidual.data(), algMode);
    EXPECT_GT(std::abs(algResidual[1]), 1e-4);
    diff[6] -= 0.01;
    alg[0] += 0.01;
    model.residual({1.0, 0.1}, diffData, diffResidual.data(), diffMode);
    EXPECT_NEAR(diffResidual[0], -0.001, 1e-12);
}

TEST(RenewableModels, GridFormingDyrAttachesAndBindsPLL)
{
    for (const auto name : {"REGCV1", "REGCV2", "REGF1", "REGF2", "REGF3"}) {
        auto simulation = renewableDyrSimulation();
        if (std::string_view{name} == "REGF2") {
            loadRenewableRecords(*simulation, {"REGF2", "PLL2"});
        } else {
            loadRenewableRecords(*simulation, {name});
        }
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr) << name;
        auto* model = dynamic_cast<GridFormingConverter*>(host->find("electrical"));
        ASSERT_NE(model, nullptr) << name;
        if (std::string_view{name} == "REGF2") {
            auto* pll = dynamic_cast<PLL2Sensor*>(simulation->getRelay(0));
            ASSERT_NE(pll, nullptr);
            pll->dynInitializeA(0.0, 0);
            IOdata sensorFields;
            pll->dynInitializeB({}, {}, sensorFields);
            EXPECT_EQ(model->sourceName(RenewableSignal::frequencyDeviation), "pll2a");
        }
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        const auto output = host->getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode);
        EXPECT_NEAR(output[0], -0.8, 1e-12) << name;
        EXPECT_NEAR(output[1], -0.1, 1e-12) << name;
    }
}

TEST(RenewableModels, GridFormingRejectsIncompatibleControlAndPLL)
{
    RenewableGenerator incompatible;
    incompatible.add(new REGCV1);
    incompatible.add(new REECA1);
    EXPECT_THROW(incompatible.dynInitializeA(0.0, 0), InvalidParameterValue);

    auto missing = renewableDyrSimulation();
    loadRenewableRecords(*missing, {"REGF2"});
    auto* bus = dynamic_cast<GridBus*>(missing->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(host, nullptr);
    EXPECT_THROW(host->dynInitializeA(0.0, 0), InvalidParameterValue);

    auto* wrong = new FreqDivSensor("pll2a");
    wrong->setSource(bus);
    missing->add(wrong);
    EXPECT_THROW(host->dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, GridFormingTwoBusFaultAndAndesReference)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "reference" / "renewable_fault";
    for (const auto loadProfile : {"power", "impedance"}) {
        const bool constantImpedance = std::string_view{loadProfile} == "impedance";
        const auto xml = directory / (constantImpedance ? "two_bus_impedance.xml" : "two_bus.xml");
        for (const auto name : {"regcv1", "regcv2", "regf1", "regf2", "regf3"}) {
            SCOPED_TRACE(std::string{name} + " with " + loadProfile + " load");
            auto simulation = std::make_unique<GridDynSimulation>();
            loadFile(simulation.get(), xml.string());
            auto* bus = dynamic_cast<GridBus*>(simulation->find("solar_bus"));
            ASSERT_NE(bus, nullptr);
            auto* demand = bus->find("demand");
            ASSERT_NE(demand, nullptr);
            EXPECT_NEAR(demand->get("yp"), constantImpedance ? 0.4 : 0.0, 1e-12);
            EXPECT_NEAR(demand->get("yq"), constantImpedance ? 0.1 : 0.0, 1e-12);
            auto* host = new RenewableGenerator("solar");
            host->set("p", 0.6);
            host->set("mbase", 100.0, units::MVAR);
            auto* model = dynamic_cast<GridFormingConverter*>(
                CoreObjectFactory::instance()->createObject("renewable_model", name));
            ASSERT_NE(model, nullptr);
            if (std::string_view{name} == "regf2") {
                auto* pll = new PLL2Sensor("pll2a");
                pll->setSource(bus);
                auto* owner = dynamic_cast<GridArea*>(bus->getParent());
                ASSERT_NE(owner, nullptr);
                owner->add(pll);
                model->set("pll", std::string_view{"pll2a"});
            }
            host->add(model);
            bus->add(host);
            ASSERT_EQ(simulation->dynInitialize(), 0) << name;
            const auto reference = directory /
                ("andes_" + std::string{name} + (constantImpedance ? "_impedance" : "") +
                 "_reference.csv");
            std::ifstream stream(reference);
            ASSERT_TRUE(stream.is_open()) << reference;
            std::string line;
            ASSERT_TRUE(static_cast<bool>(std::getline(stream, line)));
            EXPECT_EQ(line, "time,voltage,converter_p,converter_q,delta");
            int samples = 0;
            while (std::getline(stream, line)) {
                std::istringstream row(line);
                std::array<double, 5> expected{};
                for (auto& value : expected) {
                    std::string field;
                    ASSERT_TRUE(static_cast<bool>(std::getline(row, field, ',')));
                    value = std::stod(field);
                }
                ASSERT_EQ(simulation->run(expected[0]), 0) << name << " at " << expected[0];
                EXPECT_TRUE(std::isfinite(bus->getVoltage())) << name;
                EXPECT_TRUE(std::isfinite(model->getStates()[0])) << name;
                EXPECT_TRUE(std::isfinite(model->getStates()[1])) << name;
                EXPECT_NEAR(bus->getVoltage(), expected[1], 0.02) << name << " at " << expected[0];
                EXPECT_NEAR(model->getStates()[0], expected[2], 0.04)
                    << name << " at " << expected[0];
                EXPECT_NEAR(model->getStates()[1], expected[3], 0.04)
                    << name << " at " << expected[0];
                const index_t deltaIndex = std::string_view{name}.starts_with("regcv") ? 3 : 2;
                EXPECT_NEAR(model->getStates()[deltaIndex], expected[4], 0.1)
                    << name << " at " << expected[0];
                ++samples;
            }
            EXPECT_EQ(samples, 8);
        }
    }
}

TEST(RenewableModels, REGCA1SteadyInitializationAndHostSign)
{
    RenewableGenerator host;
    auto* converter = new REGCA1;
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    ASSERT_EQ(converter->getStates().size(), 5U);
    EXPECT_NEAR(converter->getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(converter->getStates()[1], 0.1, 1e-12);
    EXPECT_NEAR(converter->getStates()[2], 0.8, 1e-12);
    EXPECT_NEAR(converter->getStates()[3], 0.1, 1e-12);
    EXPECT_NEAR(converter->getStates()[4], 1.0, 1e-12);
    const auto output = host.getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode);
    ASSERT_EQ(output.size(), 2U);
    EXPECT_NEAR(output[0], -0.8, 1e-12);
    EXPECT_NEAR(output[1], -0.1, 1e-12);
    double residual[5]{};
    converter->residual({1.0, kNullVal, kNullVal}, emptyStateData, residual, cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> daeState(host.stateSize(cDaeSolverMode));
    std::vector<double> daeRate(daeState.size());
    ASSERT_EQ(daeState.size(), 5U);
    host.guessState(0.0, daeState.data(), daeRate.data(), cDaeSolverMode);
    StateData stateData(0.0, daeState.data(), daeRate.data());
    stateData.stateSize = static_cast<count_t>(daeState.size());
    std::vector<double> daeResidual(daeState.size());
    host.residual({1.0, 0.0}, stateData, daeResidual.data(), cDaeSolverMode);
    for (double value : daeResidual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    const auto daeOutput = host.getOutputs({1.0, 0.0}, stateData, cDaeSolverMode);
    EXPECT_NEAR(daeOutput[0], -0.8, 1e-12);
    EXPECT_NEAR(daeOutput[1], -0.1, 1e-12);
}

TEST(RenewableModels, REGCP1NoPllMatchesREGCA1)
{
    REGCP1 converter;
    EXPECT_THROW(converter.set("pll", 1.0), InvalidParameterValue);
    converter.set("pll", std::string_view{"none"});
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    converter.dynInitializeB({1.0, 0.0, 0.0}, {0.8, 0.1}, fields);
    REGCA1 reference;
    reference.dynInitializeA(0.0, 0);
    IOdata referenceFields;
    reference.dynInitializeB({1.0, 0.0, 0.0}, {0.8, 0.1}, referenceFields);
    EXPECT_TRUE(std::equal(converter.getStates().begin(),
                           converter.getStates().end(),
                           reference.getStates().begin()));
    EXPECT_EQ(converter.getOutputs({1.0, 0.0, 0.0}, emptyStateData, cLocalSolverMode),
              reference.getOutputs({1.0, 0.0, 0.0}, emptyStateData, cLocalSolverMode));
    std::unique_ptr<CoreObject> copy(converter.clone());
    ASSERT_NE(dynamic_cast<REGCP1*>(copy.get()), nullptr);
}

TEST(RenewableModels, REGCP1RotatesPowerAndInitializesCurrentFrame)
{
    REGCP1 converter;
    converter.set("pll", std::string_view{"pll1"});
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    constexpr double delta = 0.2;
    converter.dynInitializeB({1.0, kNullVal, kNullVal, delta, 0.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(converter.getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(converter.getStates()[1], 0.1, 1e-12);
    EXPECT_NEAR(converter.getStates()[2], (0.8 * std::cos(delta)) + (0.1 * std::sin(delta)), 1e-12);
    EXPECT_NEAR(converter.getStates()[3],
                (-0.8 * std::sin(delta)) + (0.1 * std::cos(delta)),
                1e-12);
    EXPECT_EQ(converter.sourceName(RenewableSignal::measuredAngle), "pll1");
    double residual[5]{};
    converter.residual({1.0, kNullVal, kNullVal, delta, 0.0},
                       emptyStateData,
                       residual,
                       cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    std::array<double, 5> update{};
    converter.algebraicUpdate(
        {1.0, kNullVal, kNullVal, 0.0, 0.0}, emptyStateData, update.data(), cLocalSolverMode, 1.0);
    EXPECT_NEAR(update[0], converter.getStates()[2], 1e-12);
    EXPECT_NEAR(update[1], converter.getStates()[3], 1e-12);
    std::unique_ptr<CoreObject> copy(converter.clone());
    auto* cloned = dynamic_cast<REGCP1*>(copy.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->sourceName(RenewableSignal::measuredAngle), "pll1");
}

TEST(RenewableModels, REGCP1AngleAndCurrentJacobiansMatchResidual)
{
    REGCP1 converter;
    converter.set("pll", std::string_view{"pll1"});
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    converter.dynInitializeB({0.9, kNullVal, kNullVal, 0.2, 0.05}, {0.7, 0.2}, fields);
    converter.setOffset(0, cDaeSolverMode);
    std::vector<double> state(converter.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    converter.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    IOdata inputs{0.9, kNullVal, kNullVal, 0.2, 0.05};
    MatrixDataSparse<double> jacobian;
    converter.jacobianElements(
        inputs, data, jacobian, {10, kNullLocation, kNullLocation, 11, 12}, cDaeSolverMode);
    const auto evaluate = [&]() {
        std::vector<double> residual(state.size());
        converter.residual(inputs, data, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = evaluate();
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < 5; ++column) {
        state[column] += step;
        const auto shifted = evaluate();
        for (std::size_t row = 0; row < 2; ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (shifted[row] - base[row]) / step,
                        1e-5);
        }
        state[column] -= step;
    }
    for (const auto [index, location] : {std::pair{0, 10}, std::pair{3, 11}, std::pair{4, 12}}) {
        inputs[index] += step;
        const auto shifted = evaluate();
        for (std::size_t row = 0; row < 2; ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), location),
                        (shifted[row] - base[row]) / step,
                        1e-5);
        }
        inputs[index] -= step;
    }
}

TEST(RenewableModels, WTDSOneMassEquationAndAliasedSpeeds)
{
    WTDS shaft;
    shaft.set("h", 2.0);
    shaft.set("d", 0.5);
    shaft.set("w0", 1.1);
    shaft.dynInitializeA(0.0, 0);
    IOdata fields;
    shaft.dynInitializeB({0.8}, {0.8, 0.1}, fields);
    EXPECT_EQ(fields, (IOdata{1.1, 1.1}));
    EXPECT_EQ(shaft.getOutputLoc(cLocalSolverMode, 0), shaft.getOutputLoc(cLocalSolverMode, 1));
    std::array<double, 1> derivative{};
    shaft.derivative({0.7, 1.0, 1.0}, emptyStateData, derivative.data(), cLocalSolverMode);
    const double expected = (((1.0 - 0.7) / 1.1) - (0.5 * (1.1 - 1.0))) / 4.0;
    EXPECT_NEAR(derivative[0], expected, 1e-12);
    shaft.timestep(0.1, {0.7, 1.0, 1.0}, cLocalSolverMode);
    EXPECT_NEAR(shaft.getOutput(0), 1.1 + (0.1 * expected), 1e-12);
    EXPECT_NEAR(shaft.getOutput(1), shaft.getOutput(0), 1e-12);
}

TEST(RenewableModels, REGCA1AcceptsCaseAccelerationAndValidatesRange)
{
    REGCA1 converter;
    converter.set("Accel", 0.8);
    EXPECT_DOUBLE_EQ(converter.get("Accel"), 0.8);
    converter.dynInitializeA(0.0, 0);
    converter.set("Accel", 1.1);
    EXPECT_THROW(converter.dynInitializeA(0.0, 0), InvalidParameterValue);
    converter.set("Accel", 0.8);
    converter.set("Tg", 0.0);
    EXPECT_THROW(converter.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, REGCA1ZeroReactiveRecoveryLimitsAreDirectional)
{
    REGCA1 converter;
    converter.set("iqrmin", -1.0);
    converter.set("iqrmax", 0.0);
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    converter.dynInitializeB({1.0, 0.0, 0.0}, {0.8, 0.1}, fields);
    std::vector<double> rate(converter.getStates().size());
    converter.derivative({1.0, 0.8, -0.4}, emptyStateData, rate.data(), cLocalSolverMode);
    EXPECT_NEAR(rate[3], 3.0, 1e-12);
    converter.set("iqrmax", 2.0);
    converter.derivative({1.0, 0.8, -0.4}, emptyStateData, rate.data(), cLocalSolverMode);
    EXPECT_NEAR(rate[3], 2.0, 1e-12);
}

TEST(RenewableModels, REECA1ZeroCurvesAndZeroOrderingLag)
{
    REECA1 controller;
    controller.set("tpord", 0.0);
    controller.set("imax", 1.3);
    for (int index = 1; index <= 4; ++index) {
        const auto suffix = std::to_string(index);
        controller.set("vq" + suffix, 0.0);
        controller.set("iq" + suffix, 0.0);
        controller.set("vp" + suffix, 0.0);
        controller.set("ip" + suffix, 0.0);
    }
    controller.dynInitializeA(0.0, 0);
    IOdata fields;
    controller.dynInitializeB({1.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(fields[0], 0.8, 1e-12);
    EXPECT_NEAR(fields[1], -0.1, 1e-12);
    EXPECT_NEAR(controller.getOutputs({1.0}, emptyStateData, cLocalSolverMode)[2], 0.8, 1e-12);
    EXPECT_DOUBLE_EQ(controller.get("vq4"), 0.0);
    double update[6]{};
    EXPECT_THROW(controller.algebraicUpdate({0.7}, emptyStateData, update, cLocalSolverMode, 1.0),
                 InvalidParameterValue);
    controller.set("thld2", 0.5);
    EXPECT_DOUBLE_EQ(controller.get("thld2"), 0.5);
    EXPECT_NO_THROW(controller.dynInitializeA(0.0, 0));
    EXPECT_EQ(controller.rootSize(cDaeSolverMode), 2U);
}

TEST(RenewableModels, ACTIVSg25kDyrProfileLoadsRecoveryHold)
{
    auto simulation = renewableDyrSimulation();
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_activsg25k_profile_" + std::to_string(simulation->getID()) + ".dyr");
    {
        std::ofstream output(file);
        output << "101 'REGCA1' '1' 1 0.02 10 0.9 0.5 1.22 1.2 0.8 0.4 "
                  "-1.3 0.02 0.7 0 0 0.8 /\n";
        output << "101 'REECA1' '1' 0 0 0 0 0 0 0.85 1.15 0.033333 "
                  "0 0 5 1.1 -1.1 0 0 0 0.5 0 0.436 -0.436 1.1 0.9 "
                  "0 0.1 18 5 0 0.033333 99 -99 1 0 1.3 0 "
                  "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 /\n";
    }
    EXPECT_NO_THROW(loadDyr(simulation.get(), file.string(), BasicReaderInfo{}));
    std::filesystem::remove(file);
    auto* generator = dynamic_cast<RenewableGenerator*>(simulation->getBus(0)->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* converter = dynamic_cast<REGCA1*>(generator->find("electrical"));
    ASSERT_NE(converter, nullptr);
    EXPECT_DOUBLE_EQ(converter->get("accel"), 0.8);
    auto* electrical = dynamic_cast<REECA1*>(generator->find("electrical_control"));
    ASSERT_NE(electrical, nullptr);
    EXPECT_DOUBLE_EQ(electrical->get("thld2"), 0.5);
    EXPECT_DOUBLE_EQ(electrical->get("tpord"), 0.0);
    EXPECT_EQ(electrical->rootSize(cDaeSolverMode), 2U);
}

TEST(RenewableModels, REECA1HoldsDipLimitAfterRecovery)
{
    REECA1 controller;
    controller.set("thld2", 0.5);
    controller.set("tpord", 0.0);
    controller.set("pqflag", 0.0);
    controller.set("imax", 0.85);
    controller.set("qmax", 1.0);
    controller.set("qmin", -1.0);
    for (int index = 1; index <= 4; ++index) {
        const auto suffix = std::to_string(index);
        for (const auto* prefix : {"vq", "iq", "vp", "ip"}) {
            controller.set(std::string{prefix} + suffix, 0.0);
        }
    }
    controller.dynInitializeA(0.0, 0);
    IOdata fields;
    controller.dynInitializeB({1.0}, {0.8, 0.1}, fields);
    controller.setRootOffset(0, cLocalSolverMode);
    double roots[2]{};
    controller.rootTest({1.0}, StateData(0.0), roots, cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    controller.rootTest({0.7}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    controller.timestep(0.1, {1.0, 0.0, 0.0, 0.8}, cLocalSolverMode);
    controller.rootTrigger(0.1, {0.7}, {-1, 0}, cLocalSolverMode);
    double update[6]{};
    EXPECT_NO_THROW(
        controller.algebraicUpdate({0.7}, emptyStateData, update, cLocalSolverMode, 1.0));
    for (int step = 11; step <= 19; ++step) {
        controller.timestep(step * 0.01, {0.7, 0.0, 0.0, 0.8}, cLocalSolverMode);
    }
    EXPECT_NEAR(controller.getOutputs({0.7}, emptyStateData, cLocalSolverMode)[2], 0.8, 1e-12);
    EXPECT_LT(controller.getOutputs({0.7}, emptyStateData, cLocalSolverMode)[0], 0.79);

    controller.rootTrigger(0.2, {1.0}, {1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(0.2), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.5, 1e-12);
    controller.timestep(0.21, {1.0, 0.0, 0.0, 0.8}, cLocalSolverMode);
    const double heldCurrent = controller.getOutputs({1.0}, emptyStateData, cLocalSolverMode)[0];
    EXPECT_LT(heldCurrent, 0.79);
    controller.rootTest({1.0}, StateData(0.7), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.0, 1e-9);
    controller.rootTrigger(0.7, {1.0}, {0, -1}, cLocalSolverMode);
    EXPECT_GT(controller.getOutputs({1.0}, emptyStateData, cLocalSolverMode)[0],
              heldCurrent + 0.02);
}

TEST(RenewableModels, REECA1RestartsHoldOnSecondDipAndHandlesHighVoltage)
{
    REECA1 controller;
    controller.set("thld2", 0.5);
    controller.dynInitializeA(0.0, 0);
    IOdata fields;
    controller.dynInitializeB({1.0}, {0.8, 0.1}, fields);
    controller.setRootOffset(0, cLocalSolverMode);
    double roots[2]{};

    controller.rootTest({1.3}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    controller.rootTrigger(0.1, {1.3}, {-1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(0.2), roots, cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    controller.rootTrigger(0.2, {1.0}, {1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(0.6), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.1, 1e-12);

    controller.rootTrigger(0.6, {0.7}, {-1, 0}, cLocalSolverMode);
    controller.rootTest({0.7}, StateData(0.8), roots, cLocalSolverMode);
    EXPECT_GT(roots[1], 0.0);
    controller.rootTrigger(0.8, {1.0}, {1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(1.0), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.3, 1e-12);
    controller.rootTrigger(1.3, {1.0}, {0, -1}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(1.4), roots, cLocalSolverMode);
    EXPECT_GT(roots[1], 0.0);
}

TEST(RenewableModels, RenewableGeneratorForwardsREECA1VoltageRoots)
{
    RenewableGenerator host;
    auto* electrical = new REECA1;
    electrical->set("thld2", 0.5);
    electrical->set("tpord", 0.0);
    auto* converter = new REGCA1;
    host.add(electrical);
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    ASSERT_EQ(host.rootSize(cLocalSolverMode), 2U);
    EXPECT_EQ(electrical->rootSize(cLocalSolverMode), 2U);
    host.setRootOffset(0, cLocalSolverMode);
    double roots[2]{};
    host.rootTest({0.7, 0.0}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    host.rootTrigger(0.1, {0.7, 0.0}, {-1, 0}, cLocalSolverMode);
    electrical->rootTest({0.7}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    host.rootTrigger(0.2, {1.0, 0.0}, {1, 0}, cLocalSolverMode);
    host.rootTest({1.0, 0.0}, StateData(0.2), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.5, 1e-12);
}

TEST(RenewableModels, REGCA1VoltageStepAndMachineBase)
{
    RenewableGenerator host;
    host.set("mbase", 50.0, units::MVAR);
    auto* converter = new REGCA1;
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.4, 0.05}, fields);
    EXPECT_NEAR(converter->getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.4, 1e-12);
    host.timestep(0.1, {0.6, 0.0}, cLocalSolverMode);
    EXPECT_NEAR(converter->getStates()[4], 0.6, 1e-12);
    EXPECT_NEAR(converter->getStates()[0], 0.24, 1e-12);
    EXPECT_NEAR(host.getOutputs({0.6, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.12, 1e-12);
}

TEST(RenewableModels, REGCA1DaeJacobianMatchesResidual)
{
    REGCA1 converter;
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    const IOdata inputs{1.0, kNullVal, kNullVal};
    converter.dynInitializeB(inputs, {0.8, 0.1}, fields);
    converter.setOffset(0, cDaeSolverMode);
    const auto state = converter.getStates();
    std::vector<double> dstate(state.size(), 0.0);
    StateData stateData(0.0, state.data(), dstate.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    stateData.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    converter.jacobianElements(
        inputs, stateData, jacobian, {6, kNullLocation, kNullLocation}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& values,
                                const std::vector<double>& rates) {
        StateData trial(0.0, values.data(), rates.data());
        trial.stateSize = static_cast<count_t>(values.size());
        std::vector<double> residual(values.size());
        converter.residual(inputs, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = residualAt(state, dstate);
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto shifted = state;
        auto rates = dstate;
        shifted[column] += step;
        rates[column] += step;
        const auto residual = residualAt(shifted, rates);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (residual[row] - base[row]) / step,
                        1e-5)
                << "row " << row << " column " << column;
        }
    }
}

TEST(RenewableModels, IndependentElectricalAndPlantControls)
{
    RenewableGenerator host;
    auto* converter = new REGCA1;
    auto* electrical = new REECA1;
    auto* plant = new REPCA1;
    plant->set("dbd2", 0.0);
    host.add(plant);
    host.add(electrical);
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(electrical->getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(electrical->getStates()[1], -0.1, 1e-12);
    EXPECT_NEAR(plant->getStates()[0], 0.0, 1e-12);
    EXPECT_NEAR(plant->getStates()[1], 0.0, 1e-12);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size(), 0.0);
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    std::vector<double> residual(state.size(), 0.0);
    host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-10);
    }

    plant->set("vref", 1.05);
    host.timestep(0.01, {1.0, 0.0}, cLocalSolverMode);
    EXPECT_GT(plant->getStates()[1], 0.0);
    EXPECT_GT(electrical->getStates()[5], 0.1);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.8, 1e-3);
}

TEST(RenewableModels, SchedulerDrivesElectricalActiveReferenceOnMachineBase)
{
    RenewableGenerator host;
    host.set("mbase", 50.0, units::MVAR);
    auto* converter = new REGCA1;
    auto* control = new REECA1;
    auto* scheduler = new Scheduler("dispatch", 0.4);
    scheduler->set("purpose", "pset");
    host.add(converter);
    host.add(control);
    host.add(scheduler);
    ASSERT_EQ(host.find("pset"), scheduler);
    EXPECT_EQ(scheduler->getParent(), &host);
    EXPECT_TRUE(host.supportsActivePowerSchedule());
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.4, 0.0}, fields);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size(), 0.0);
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    std::vector<double> residual(state.size(), 0.0);
    host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-10);
    }
    scheduler->setTarget(1.0, 0.5);
    scheduler->updateA(1.0);
    data.time = 1.0;
    host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
    EXPECT_NEAR(residual[control->getOutputLoc(cDaeSolverMode, 2) - 1], 10.0, 1e-9);
}

TEST(RenewableModels, SchedulerRejectsUncontrolledAndCompetingReferences)
{
    RenewableGenerator unsupported;
    unsupported.add(new REGCA1);
    unsupported.add(new SchedulerReg("dispatch"));
    EXPECT_FALSE(unsupported.supportsActivePowerSchedule());
    EXPECT_THROW(unsupported.dynInitializeA(0.0, 0), InvalidParameterValue);

    RenewableGenerator gridForming;
    gridForming.add(new REGF1);
    gridForming.add(new SchedulerReg("dispatch"));
    EXPECT_FALSE(gridForming.supportsActivePowerSchedule());
    EXPECT_THROW(gridForming.dynInitializeA(0.0, 0), InvalidParameterValue);

    RenewableGenerator wind;
    wind.add(new REGCA1);
    wind.add(new REECA1);
    wind.add(new WTTQA1);
    wind.add(new SchedulerReg("dispatch"));
    EXPECT_FALSE(wind.supportsActivePowerSchedule());
    EXPECT_THROW(wind.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, SchedulerDrivesDistributedConverterReference)
{
    RenewableGenerator host;
    host.set("mbase", 50.0, units::MVAR);
    auto* converter = new PVD1;
    converter->set("tip", 0.1);
    auto* scheduler = new Scheduler("dispatch", 0.4);
    host.add(converter);
    host.add(scheduler);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0, 1.0}, {0.4, 0.0}, fields);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0, 1.0}, emptyStateData, cLocalSolverMode)[0], -0.4, 1e-12);
    scheduler->setTarget(0.1, 0.5);
    host.timestep(0.1, {1.0, 0.0, 1.0}, cLocalSolverMode);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0, 1.0}, emptyStateData, cLocalSolverMode)[0], -0.5, 1e-10);
}

TEST(RenewableModels, REECA1UnsupportedModesFail)
{
    REECA1 electrical;
    EXPECT_THROW(electrical.set("PFFLAG", 0.5), InvalidParameterValue);
    electrical.set("PFFLAG", 1.0);
    EXPECT_THROW(electrical.dynInitializeA(0.0, 0), InvalidParameterValue);
    REPCA1 plant;
    EXPECT_THROW(plant.set("RefFlag", 0.5), InvalidParameterValue);
    plant.set("Fflag", 1.0);
    plant.set("Rc", 0.01);
    EXPECT_THROW(plant.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, REPCA1FrequencyModeDaeJacobianIncludesFrequencyInput)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    auto* plant = new REPCA1;
    plant->set("fflag", 1.0);
    plant->set("femin", -10.0);
    plant->set("femax", 10.0);
    host.add(plant);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    constexpr double frequency = 0.997;
    host.dynInitializeB({1.0, 0.0, frequency}, {0.8, 0.1}, fields);

    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;

    const auto frequencyColumn = static_cast<index_t>(state.size() + 3);
    MatrixDataSparse<double> jacobian;
    host.jacobianElements({1.0, 0.0, frequency},
                          data,
                          jacobian,
                          {frequencyColumn - 2, frequencyColumn - 1, frequencyColumn},
                          cDaeSolverMode);
    const auto residualAtFrequency = [&](double value) {
        std::vector<double> residual(state.size());
        host.residual({1.0, 0.0, value}, data, residual.data(), cDaeSolverMode);
        return residual;
    };

    constexpr double step = 1e-7;
    const auto upper = residualAtFrequency(frequency + step);
    const auto lower = residualAtFrequency(frequency - step);
    double largestFrequencyDerivative = 0.0;
    for (std::size_t row = 0; row < state.size(); ++row) {
        const auto rowIndex = static_cast<index_t>(row);
        const double finiteDifference = (upper[row] - lower[row]) / (2.0 * step);
        EXPECT_NEAR(jacobian.at(rowIndex, frequencyColumn), finiteDifference, 1e-3)
            << "row " << row;
        largestFrequencyDerivative =
            std::max(largestFrequencyDerivative, std::abs(finiteDifference));
    }
    EXPECT_GT(largestFrequencyDerivative, 100.0);
}

TEST(RenewableModels, REECB1QFlagVoltageControlInitializes)
{
    REECB1 direct;
    direct.set("qflag", 1.0);
    direct.set("vflag", 0.0);
    direct.set("pqflag", 1.0);
    direct.dynInitializeA(0.0, 0);
    IOdata fields;
    direct.dynInitializeB({1.0, kNullVal, kNullVal, kNullVal}, {0.5, 0.1}, fields);
    ASSERT_EQ(fields.size(), 2U);
    EXPECT_NEAR(fields[0], 0.5, 1e-12);
    EXPECT_NEAR(fields[1], -0.1, 1e-12);
    EXPECT_EQ(direct.getStates().size(), 6U);

    REECB1 cascaded;
    cascaded.set("qflag", 1.0);
    cascaded.set("vflag", 1.0);
    cascaded.set("pqflag", 1.0);
    cascaded.dynInitializeA(0.0, 0);
    cascaded.dynInitializeB({1.0, kNullVal, kNullVal, kNullVal, 0.1}, {0.5, 0.1}, fields);
    ASSERT_EQ(fields.size(), 2U);
    EXPECT_NEAR(fields[0], 0.5, 1e-12);
    EXPECT_NEAR(fields[1], -0.1, 1e-12);
    EXPECT_EQ(cascaded.getStates().size(), 7U);
}

TEST(RenewableModels, REECA1SpeedBranchRequiresShaftAndTracksSpeed)
{
    REECA1 control;
    control.set("pflag", 1.0);
    control.dynInitializeA(0.0, 0);
    IOdata fields;
    EXPECT_THROW(control.dynInitializeB({1.0}, {0.8, 0.1}, fields), InvalidParameterValue);
    control.dynInitializeB({1.0, kNullVal, kNullVal, kNullVal, 0.8}, {0.8, 0.1}, fields);
    EXPECT_NEAR(control.getStates()[3], 1.0, 1e-12);
    std::array<double, 6> rate{};
    control.derivative({1.0, kNullVal, kNullVal, kNullVal, 1.0},
                       emptyStateData,
                       rate.data(),
                       cLocalSolverMode);
    EXPECT_NEAR(rate[3], -10.0, 1e-10);
    EXPECT_NEAR(rate[4], 10.0, 1e-10);
    control.set("tpord", 0.0);
    EXPECT_THROW(control.dynInitializeA(0.0, 0), InvalidParameterValue);

    RenewableGenerator missingShaft;
    missingShaft.add(new REGCA1);
    auto* missingControl = new REECA1;
    missingControl->set("pflag", 1.0);
    missingShaft.add(missingControl);
    EXPECT_THROW(missingShaft.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, BusROCOFSensorRespondsToAngleStep)
{
    AcBus bus("measuredBus");
    BusROCOFSensor sensor("areaMeasurement");
    sensor.setSource(&bus);
    sensor.dynInitializeA(0.0, 0);
    IOdata sensorFields;
    sensor.dynInitializeB({}, {}, sensorFields);
    EXPECT_EQ(sensorFields, (IOdata{0.0, 0.0}));
    for (int sample = 1; sample <= 100; ++sample) {
        const double time = 0.002 * sample;
        const double angle = sample < 40 ? 0.1 : -0.03;
        bus.set("angle", angle);
        sensor.timestep(time, {}, cLocalSolverMode);
        EXPECT_TRUE(std::isfinite(sensor.getOutput(0)));
        EXPECT_TRUE(std::isfinite(sensor.getOutput(1)));
    }
    EXPECT_LT(sensor.getOutput(0), 0.0);
}

TEST(RenewableModels, MultiplePLLSensorsHaveIndependentStates)
{
    AcBus bus("measuredBus");
    PLL1Sensor first("pll1a");
    PLL1Sensor second("pll1b");
    PLL2Sensor third("pll2");
    for (auto* sensor : {static_cast<PLLSensor*>(&first),
                         static_cast<PLLSensor*>(&second),
                         static_cast<PLLSensor*>(&third)}) {
        sensor->setSource(&bus);
        sensor->dynInitializeA(0.0, 0);
        IOdata fields;
        sensor->dynInitializeB({}, {}, fields);
        EXPECT_NEAR(sensor->getOutput(0), static_cast<GridBus&>(bus).getAngle(), 1e-12);
        EXPECT_DOUBLE_EQ(sensor->getOutput(1), 0.0);
    }
    EXPECT_EQ(first.diffSize(cLocalSolverMode), 4);
    EXPECT_EQ(third.diffSize(cLocalSolverMode), 2);
    second.set("kp", 2.0);
    bus.set("angle", 0.01);
    first.timestep(0.0001, {}, cLocalSolverMode);
    second.timestep(0.0001, {}, cLocalSolverMode);
    third.timestep(0.0001, {}, cLocalSolverMode);
    EXPECT_NE(first.getOutput(1), second.getOutput(1));
    EXPECT_NE(first.getOutput(0), third.getOutput(0));
}

TEST(RenewableModels, AreaSensorOutputsSurvivePartitionedStateUpdates)
{
    AcBus bus("measuredBus");
    BusROCOFSensor sensor("frequency");
    sensor.setSource(&bus);
    sensor.dynInitializeA(0.0, 0);
    IOdata fields;
    sensor.dynInitializeB({}, {}, fields);
    auto algebraicMode = cDynAlgSolverMode;
    auto differentialMode = cDynDiffSolverMode;
    algebraicMode.pairedOffsetIndex = differentialMode.offsetIndex;
    differentialMode.pairedOffsetIndex = algebraicMode.offsetIndex;
    const auto algebraicSize = sensor.stateSize(algebraicMode);
    const auto differentialSize = sensor.stateSize(differentialMode);
    sensor.setOffset(0, algebraicMode);
    sensor.setOffset(0, differentialMode);
    std::vector<double> algebraic(algebraicSize);
    std::vector<double> differential(differentialSize);
    std::vector<double> algebraicRate(algebraic.size());
    std::vector<double> differentialRate(differential.size());
    sensor.guessState(0.0, algebraic.data(), algebraicRate.data(), algebraicMode);
    sensor.guessState(0.0, differential.data(), differentialRate.data(), differentialMode);
    algebraic[sensor.getOutputLoc(algebraicMode, 0)] = 0.03;
    algebraic[sensor.getOutputLoc(algebraicMode, 1)] = -0.02;
    sensor.setState(0.0, algebraic.data(), algebraicRate.data(), algebraicMode);
    sensor.setState(0.0, differential.data(), differentialRate.data(), differentialMode);
    StateData state(0.0, differential.data(), differentialRate.data());
    state.algState = algebraic.data();
    EXPECT_EQ(sensor.getOutputLoc(differentialMode, 0), kNullLocation);
    EXPECT_DOUBLE_EQ(sensor.getOutput({}, state, differentialMode, 0), 0.03);
    EXPECT_DOUBLE_EQ(sensor.getOutput({}, state, differentialMode, 1), -0.02);
}

TEST(RenewableModels, PLLSensorDaeJacobiansMatchResiduals)
{
    AcBus bus("measuredBus");
    std::vector<std::unique_ptr<PLLSensor>> sensors;
    sensors.push_back(std::make_unique<PLL1Sensor>("pll1"));
    sensors.push_back(std::make_unique<PLL2Sensor>("pll2"));
    for (auto& sensor : sensors) {
        sensor->setSource(&bus);
        sensor->dynInitializeA(0.0, 0);
        IOdata fields;
        sensor->dynInitializeB({}, {}, fields);
        sensor->setOffset(0, cDaeSolverMode);
        std::vector<double> state(sensor->stateSize(cDaeSolverMode));
        std::vector<double> rate(state.size());
        sensor->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
        state[sensor->getOutputLoc(cDaeSolverMode, 0)] += 0.01;
        StateData data(0.0, state.data(), rate.data());
        data.stateSize = static_cast<count_t>(state.size());
        data.cj = 1.0;
        MatrixDataSparse<double> jacobian;
        sensor->jacobianElements({}, data, jacobian, {}, cDaeSolverMode);
        const auto evaluate = [&]() {
            std::vector<double> residual(state.size());
            sensor->residual({}, data, residual.data(), cDaeSolverMode);
            return residual;
        };
        const auto base = evaluate();
        constexpr double step = 1e-7;
        for (std::size_t column = 0; column < state.size(); ++column) {
            state[column] += step;
            if (column >= 1) {
                rate[column] += step;
            }
            const auto perturbed = evaluate();
            for (std::size_t row = 0; row < state.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                            (perturbed[row] - base[row]) / step,
                            1e-4)
                    << sensor->getName() << " row " << row << " column " << column;
            }
            state[column] -= step;
            if (column >= 1) {
                rate[column] -= step;
            }
        }
    }
}

TEST(RenewableModels, BusROCOFSensorDaeJacobianMatchesResidual)
{
    AcBus bus("measuredBus");
    BusROCOFSensor sensor("frequency");
    sensor.setSource(&bus);
    sensor.dynInitializeA(0.0, 0);
    IOdata fields;
    sensor.dynInitializeB({}, {}, fields);
    sensor.setOffset(0, cDaeSolverMode);
    std::vector<double> state(sensor.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    sensor.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    state[2] = 0.01;
    state[3] = 0.005;
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    sensor.jacobianElements({}, data, jacobian, {}, cDaeSolverMode);
    const auto evaluate = [&]() {
        std::vector<double> residual(state.size());
        sensor.residual({}, data, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = evaluate();
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        state[column] += step;
        if (column >= 2) {
            rate[column] += step;
        }
        const auto perturbed = evaluate();
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (perturbed[row] - base[row]) / step,
                        1e-5);
        }
        state[column] -= step;
        if (column >= 2) {
            rate[column] -= step;
        }
    }
}

TEST(RenewableModels, FreqDivSensorsOwnCoupledAreaEquations)
{
    GridArea area("frequencyArea");
    auto* firstArea = new GridArea("firstArea");
    auto* secondArea = new GridArea("secondArea");
    area.add(firstArea);
    area.add(secondArea);
    auto* bus1 = new AcBus("bus1");
    auto* bus2 = new AcBus("bus2");
    firstArea->add(bus1);
    secondArea->add(bus2);
    auto* line = new AcLine(0.0, 0.1, "line");
    line->updateBus(bus1, 1);
    line->updateBus(bus2, 2);
    area.add(line);
    auto* first = new FreqDivSensor("freq1");
    auto* second = new FreqDivSensor("freq2");
    first->setSource(bus1);
    second->setSource(bus2);
    firstArea->add(first);
    secondArea->add(second);
    first->dynInitializeA(0.0, 0);
    second->dynInitializeA(0.0, 0);
    IOdata fields;
    first->dynInitializeB({}, {}, fields);
    second->dynInitializeB({}, {}, fields);
    first->setOffset(0, cDaeSolverMode);
    second->setOffset(1, cDaeSolverMode);
    std::array<double, 2> state{1.02, 1.0};
    std::array<double, 2> rate{};
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = 2;
    std::array<double, 2> residual{};
    first->residual({}, data, residual.data(), cDaeSolverMode);
    second->residual({}, data, residual.data(), cDaeSolverMode);
    EXPECT_NEAR(residual[0], -0.2, 1e-12);
    EXPECT_NEAR(residual[1], 0.2, 1e-12);
    MatrixDataSparse<double> jacobian;
    first->jacobianElements({}, data, jacobian, {}, cDaeSolverMode);
    second->jacobianElements({}, data, jacobian, {}, cDaeSolverMode);
    EXPECT_NEAR(jacobian.at(0, 0), -10.0, 1e-12);
    EXPECT_NEAR(jacobian.at(0, 1), 10.0, 1e-12);
    EXPECT_NEAR(jacobian.at(1, 0), 10.0, 1e-12);
    EXPECT_NEAR(jacobian.at(1, 1), -10.0, 1e-12);
    line->set("b1", 0.2);
    residual = {};
    first->residual({}, data, residual.data(), cDaeSolverMode);
    EXPECT_NEAR(residual[0], -0.196, 1e-12);
    line->disconnect();
    residual = {};
    first->residual({}, data, residual.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(residual[0], 0.0);
    std::array<double, 2> update{};
    EXPECT_THROW(first->algebraicUpdate({}, data, update.data(), cDaeSolverMode, 1.0),
                 InvalidParameterValue);
}

TEST(RenewableModels, COISensorUsesAreaInertiaWeightsAndHasConsistentJacobian)
{
    GridArea area("coiArea");
    auto* leftArea = new GridArea("leftArea");
    auto* rightArea = new GridArea("rightArea");
    area.add(leftArea);
    area.add(rightArea);
    auto* leftBus = new AcBus("leftBus");
    auto* rightBus = new AcBus("rightBus");
    leftArea->add(leftBus);
    rightArea->add(rightBus);

    auto* leftGenerator = new DynamicGenerator("leftGenerator");
    auto* leftModel = new genmodels::GenModelClassical;
    leftModel->set("h", 2.0);
    leftGenerator->set("mbase", 100.0, units::MVAR);
    leftGenerator->add(leftModel);
    leftBus->add(leftGenerator);
    auto* rightGenerator = new DynamicGenerator("rightGenerator");
    auto* rightModel = new genmodels::GenModelGENROU;
    rightModel->set("h", 6.0);
    rightGenerator->set("mbase", 200.0, units::MVAR);
    rightGenerator->add(rightModel);
    rightBus->add(rightGenerator);

    auto* coi = new COISensor("coi");
    area.add(coi);
    leftGenerator->dynInitializeA(0.0, 0);
    rightGenerator->dynInitializeA(0.0, 0);
    coi->dynInitializeA(0.0, 0);
    IOdata fields;
    coi->dynInitializeB({}, {}, fields);

    index_t offset = 0;
    leftGenerator->setOffset(offset, cDaeSolverMode);
    offset += leftGenerator->stateSize(cDaeSolverMode);
    rightGenerator->setOffset(offset, cDaeSolverMode);
    offset += rightGenerator->stateSize(cDaeSolverMode);
    coi->setOffset(offset, cDaeSolverMode);
    const auto stateSize = offset + coi->stateSize(cDaeSolverMode);
    std::vector<double> state(stateSize, 0.0);
    std::vector<double> rate(stateSize, 0.0);
    index_t leftFrequency = kNullLocation;
    index_t leftAngle = kNullLocation;
    index_t rightFrequency = kNullLocation;
    index_t rightAngle = kNullLocation;
    leftGenerator->getFreq({}, cDaeSolverMode, &leftFrequency);
    leftGenerator->getAngle({}, cDaeSolverMode, &leftAngle);
    rightGenerator->getFreq({}, cDaeSolverMode, &rightFrequency);
    rightGenerator->getAngle({}, cDaeSolverMode, &rightAngle);
    ASSERT_NE(leftFrequency, kNullLocation);
    ASSERT_NE(leftAngle, kNullLocation);
    ASSERT_NE(rightFrequency, kNullLocation);
    ASSERT_NE(rightAngle, kNullLocation);
    state[leftFrequency] = 1.02;
    state[leftAngle] = 0.1;
    state[rightFrequency] = 0.98;
    state[rightAngle] = -0.1;
    const auto coiFrequency = coi->getOutputLoc(cDaeSolverMode, 0);
    const auto coiAngle = coi->getOutputLoc(cDaeSolverMode, 1);
    ASSERT_NE(coiFrequency, kNullLocation);
    ASSERT_NE(coiAngle, kNullLocation);
    state[coiFrequency] = 6.9 / 7.0;
    state[coiAngle] = -0.5 / 7.0;

    StateData stateData(0.0, state.data(), rate.data());
    stateData.stateSize = stateSize;
    std::vector<double> residual(stateSize, 0.0);
    coi->residual({}, stateData, residual.data(), cDaeSolverMode);
    EXPECT_NEAR(residual[coiFrequency], 0.0, 1e-12);
    EXPECT_NEAR(residual[coiAngle], 0.0, 1e-12);

    MatrixDataSparse<double> jacobian;
    coi->jacobianElements({}, stateData, jacobian, {}, cDaeSolverMode);
    EXPECT_NEAR(jacobian.at(coiFrequency, coiFrequency), 1.0, 1e-12);
    EXPECT_NEAR(jacobian.at(coiFrequency, leftFrequency), -1.0 / 7.0, 1e-12);
    EXPECT_NEAR(jacobian.at(coiFrequency, rightFrequency), -6.0 / 7.0, 1e-12);
    EXPECT_NEAR(jacobian.at(coiAngle, coiAngle), 1.0, 1e-12);
    EXPECT_NEAR(jacobian.at(coiAngle, leftAngle), -1.0 / 7.0, 1e-12);
    EXPECT_NEAR(jacobian.at(coiAngle, rightAngle), -6.0 / 7.0, 1e-12);
}

TEST(RenewableModels, BusROCOFMatchesAndesForIdenticalAngleInput)
{
    const auto reference = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "reference" / "renewable_fault" / "andes_busrocof_angle_reference.csv";
    std::ifstream stream(reference);
    ASSERT_TRUE(stream.is_open());
    std::string line;
    ASSERT_TRUE(static_cast<bool>(std::getline(stream, line)));
    ASSERT_EQ(line, "time,angle,frequency_deviation,rocof");
    AcBus bus("measuredBus");
    BusROCOFSensor measurement("freq1");
    measurement.setSource(&bus);
    measurement.dynInitializeA(0.0, 0);
    bool initialized = false;
    int samples = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream row(line);
        std::array<double, 4> expected{};
        for (auto& value : expected) {
            std::string field;
            ASSERT_TRUE(static_cast<bool>(std::getline(row, field, ',')));
            value = std::stod(field);
        }
        bus.set("angle", expected[1]);
        if (!initialized) {
            IOdata fields;
            measurement.dynInitializeB({}, {}, fields);
            initialized = true;
        }
        measurement.timestep(expected[0], {}, cLocalSolverMode);
        EXPECT_NEAR(measurement.getOutput(0), expected[2], 0.0001) << expected[0];
        EXPECT_NEAR(measurement.getOutput(1), expected[3], 0.001) << expected[0];
        ++samples;
    }
    EXPECT_GE(samples, 180);
}

TEST(RenewableModels, REECA1EZeroGainMatchesREECA1)
{
    REECA1 base;
    REECA1E frequencyControl;
    frequencyControl.set("busroc", std::string_view{"freq1"});
    frequencyControl.set("kf", 0.0);
    frequencyControl.set("kdf", 0.0);
    for (auto* control : {&base, static_cast<REECA1*>(&frequencyControl)}) {
        control->set("tpord", 0.05);
        control->dynInitializeA(0.0, 0);
    }
    IOdata baseFields;
    IOdata frequencyFields;
    base.dynInitializeB({1.0}, {0.8, 0.1}, baseFields);
    frequencyControl.dynInitializeB({1.0, kNullVal, kNullVal, kNullVal, kNullVal, 0.0, 0.0},
                                    {0.8, 0.1},
                                    frequencyFields);
    EXPECT_EQ(baseFields, frequencyFields);
    for (int sample = 1; sample <= 20; ++sample) {
        const double time = 0.005 * sample;
        const double voltage = sample < 8 ? 0.9 : 1.04;
        base.timestep(time, {voltage}, cLocalSolverMode);
        frequencyControl.timestep(time,
                                  {voltage, kNullVal, kNullVal, kNullVal, kNullVal, 0.02, -0.03},
                                  cLocalSolverMode);
        ASSERT_EQ(base.getStates().size(), frequencyControl.getStates().size());
        for (std::size_t index = 0; index < base.getStates().size(); ++index) {
            EXPECT_NEAR(base.getStates()[index], frequencyControl.getStates()[index], 1e-12)
                << "sample " << sample << " state " << index;
        }
        const auto baseOutput = base.getOutputs({}, emptyStateData, cLocalSolverMode);
        const auto frequencyOutput =
            frequencyControl.getOutputs({}, emptyStateData, cLocalSolverMode);
        ASSERT_EQ(baseOutput.size(), frequencyOutput.size());
        for (std::size_t index = 0; index < baseOutput.size(); ++index) {
            EXPECT_NEAR(baseOutput[index], frequencyOutput[index], 1e-12);
        }
    }
}

TEST(RenewableModels, REECA1GSpeedFeedbackAndJacobian)
{
    REECA1G control;
    control.set("sg", std::string_view{"sync1"});
    control.set("tpord", 0.05);
    control.dynInitializeA(0.0, 0);
    IOdata fields;
    control.dynInitializeB({1.0, kNullVal, kNullVal, kNullVal, kNullVal, 1.0}, {0.8, 0.1}, fields);
    control.setOffset(0, cDaeSolverMode);
    std::vector<double> state(control.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    control.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    const auto filterLoc = control.getOutputLoc(cDaeSolverMode, 2) - 1;
    std::vector<double> residual(state.size());
    const IOdata inputs{1.0, kNullVal, kNullVal, kNullVal, kNullVal, 1.02};
    control.set("kf", 0.0);
    control.residual(inputs, data, residual.data(), cDaeSolverMode);
    const double baseline = residual[filterLoc];
    control.set("kf", 4.0);
    control.residual(inputs, data, residual.data(), cDaeSolverMode);
    EXPECT_NEAR(residual[filterLoc] - baseline, -4.0, 1e-10);
    MatrixDataSparse<double> jacobian;
    control.jacobianElements(inputs,
                             data,
                             jacobian,
                             {10, kNullLocation, kNullLocation, kNullLocation, kNullLocation, 20},
                             cDaeSolverMode);
    EXPECT_NEAR(jacobian.at(filterLoc, 20), -200.0, 1e-10);
    const double combined = residual[filterLoc];
    auto perturbed = inputs;
    perturbed[5] += 1e-7;
    control.residual(perturbed, data, residual.data(), cDaeSolverMode);
    EXPECT_NEAR((residual[filterLoc] - combined) / 1e-7, jacobian.at(filterLoc, 20), 1e-5);
}

TEST(RenewableModels, REECA1GZeroGainMatchesREECA1)
{
    REECA1 base;
    REECA1G speedControl;
    speedControl.set("sg", std::string_view{"sync1"});
    speedControl.set("kf", 0.0);
    for (auto* control : {&base, static_cast<REECA1*>(&speedControl)}) {
        control->set("tpord", 0.05);
        control->dynInitializeA(0.0, 0);
    }
    IOdata baseFields;
    IOdata speedFields;
    base.dynInitializeB({1.0}, {0.8, 0.1}, baseFields);
    speedControl.dynInitializeB({1.0, kNullVal, kNullVal, kNullVal, kNullVal, 1.0},
                                {0.8, 0.1},
                                speedFields);
    EXPECT_EQ(baseFields, speedFields);
    for (int sample = 1; sample <= 20; ++sample) {
        const double time = 0.005 * sample;
        const double voltage = sample < 8 ? 0.9 : 1.04;
        base.timestep(time, {voltage}, cLocalSolverMode);
        speedControl.timestep(time,
                              {voltage, kNullVal, kNullVal, kNullVal, kNullVal, 1.03},
                              cLocalSolverMode);
        EXPECT_TRUE(
            std::equal(base.getStates().begin(),
                       base.getStates().end(),
                       speedControl.getStates().begin(),
                       [](double left, double right) { return std::abs(left - right) < 1e-12; }));
    }
}

TEST(RenewableModels, REECA1GDyrBindsOnlyNamedSynchronousGenerator)
{
    auto missing = renewableDyrSimulation();
    loadRenewableRecords(*missing, {"REGCA1", "REECA1G"});
    auto* bus = dynamic_cast<GridBus*>(missing->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(host, nullptr);
    EXPECT_THROW(host->dynInitializeA(0.0, 0), InvalidParameterValue);
    bus->add(new Generator("sync1"));
    EXPECT_THROW(host->dynInitializeA(0.0, 0), InvalidParameterValue);

    auto inverterCase = renewableDyrSimulation();
    auto* inverterBus = dynamic_cast<GridBus*>(inverterCase->findByUserID("bus", 101));
    ASSERT_NE(inverterBus, nullptr);
    auto* inverter = new DynamicGenerator("sync1");
    inverter->add(new genmodels::GenModelInverter);
    inverterBus->add(inverter);
    loadRenewableRecords(*inverterCase, {"REGCA1", "REECA1G"});
    auto* inverterHost = dynamic_cast<RenewableGenerator*>(inverterBus->getGen(0));
    ASSERT_NE(inverterHost, nullptr);
    EXPECT_THROW(inverterHost->dynInitializeA(0.0, 0), InvalidParameterValue);

    for (const auto& order : {std::vector<std::string_view>{"REGCA1", "REECA1G"},
                              std::vector<std::string_view>{"REECA1G", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        auto* sourceBus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(sourceBus, nullptr);
        auto* synchronous = new DynamicGenerator("sync1");
        synchronous->add(new genmodels::GenModelClassical);
        sourceBus->add(synchronous);
        loadRenewableRecords(*simulation, order);
        auto* renewable = dynamic_cast<RenewableGenerator*>(sourceBus->getGen(0));
        ASSERT_NE(renewable, nullptr);
        ASSERT_NE(dynamic_cast<REECA1G*>(renewable->find("electrical_control")), nullptr);
        EXPECT_NO_THROW(renewable->dynInitializeA(0.0, 0));
    }
}

TEST(RenewableModels, REECA1EBindsNamedMeasurementAndFrequencyJacobian)
{
    REECA1E unnamed;
    EXPECT_THROW(unnamed.dynInitializeA(0.0, 0), InvalidParameterValue);
    auto missing = renewableDyrSimulation();
    loadRenewableRecords(*missing, {"REGCA1", "REECA1E"});
    auto* missingBus = dynamic_cast<GridBus*>(missing->findByUserID("bus", 101));
    ASSERT_NE(missingBus, nullptr);
    auto* missingHost = dynamic_cast<RenewableGenerator*>(missingBus->getGen(0));
    ASSERT_NE(missingHost, nullptr);
    EXPECT_THROW(missingHost->dynInitializeA(0.0, 0), InvalidParameterValue);
    auto* wrong = new FreqDivSensor("freq1");
    wrong->setSource(missingBus);
    missing->add(wrong);
    EXPECT_THROW(missingHost->dynInitializeA(0.0, 0), InvalidParameterValue);

    auto simulation = renewableDyrSimulation();
    loadRenewableRecords(*simulation, {"REGCA1", "REECA1E", "BUSROCOF"});
    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(host, nullptr);
    auto* control = dynamic_cast<REECA1E*>(host->find("electrical_control"));
    auto* measurement = dynamic_cast<BusROCOFSensor*>(simulation->getRelay(0));
    ASSERT_NE(control, nullptr);
    ASSERT_NE(measurement, nullptr);
    measurement->dynInitializeA(0.0, 0);
    IOdata fields;
    measurement->dynInitializeB({}, {}, fields);
    host->dynInitializeA(0.0, 0);
    host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    const auto sensorSize = measurement->stateSize(cDaeSolverMode);
    measurement->setOffset(0, cDaeSolverMode);
    host->setOffset(sensorSize, cDaeSolverMode);
    std::vector<double> state(sensorSize + host->stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    measurement->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    host->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    const auto deviationLoc = measurement->getOutputLoc(cDaeSolverMode, 0);
    const auto rocofLoc = measurement->getOutputLoc(cDaeSolverMode, 1);
    const auto filterLoc = control->getOutputLoc(cDaeSolverMode, 2) - 1;
    state[deviationLoc] = 0.01;
    state[rocofLoc] = 0.02;
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    std::vector<double> residual(state.size());
    const auto filterResidual = [&](double frequencyGain, double rocofGain) {
        control->set("kf", frequencyGain);
        control->set("kdf", rocofGain);
        host->residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
        return residual[filterLoc];
    };
    const double base = filterResidual(0.0, 0.0);
    EXPECT_NEAR(filterResidual(4.0, 0.0) - base, -2.0, 1e-10);
    EXPECT_NEAR(filterResidual(0.0, 0.5) - base, -0.5, 1e-10);
    EXPECT_NEAR(filterResidual(4.0, 0.5) - base, -2.5, 1e-10);
    MatrixDataSparse<double> jacobian;
    host->jacobianElements({1.0, 0.0}, data, jacobian, {30, 31}, cDaeSolverMode);
    EXPECT_NEAR(jacobian.at(filterLoc, deviationLoc), -200.0, 1e-10);
    EXPECT_NEAR(jacobian.at(filterLoc, rocofLoc), -25.0, 1e-10);
    const double combined = residual[filterLoc];
    constexpr double step = 1e-7;
    for (const auto location : {deviationLoc, rocofLoc}) {
        state[location] += step;
        host->residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
        EXPECT_NEAR(jacobian.at(filterLoc, location),
                    (residual[filterLoc] - combined) / step,
                    1e-5);
        state[location] -= step;
    }
}

TEST(RenewableModels, REECA1EReadsMeasurementInPairedSolverModes)
{
    auto simulation = renewableDyrSimulation();
    loadRenewableRecords(*simulation, {"BUSROCOF", "REGCA1", "REECA1E"});
    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(host, nullptr);
    auto* control = dynamic_cast<REECA1E*>(host->find("electrical_control"));
    auto* measurement = dynamic_cast<BusROCOFSensor*>(simulation->getRelay(0));
    ASSERT_NE(control, nullptr);
    ASSERT_NE(measurement, nullptr);
    measurement->dynInitializeA(0.0, 0);
    IOdata fields;
    measurement->dynInitializeB({}, {}, fields);
    host->dynInitializeA(0.0, 0);
    host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);

    auto algebraicMode = cDynAlgSolverMode;
    auto differentialMode = cDynDiffSolverMode;
    algebraicMode.pairedOffsetIndex = differentialMode.offsetIndex;
    differentialMode.pairedOffsetIndex = algebraicMode.offsetIndex;
    const auto sensorAlgebraicSize = measurement->stateSize(algebraicMode);
    const auto sensorDifferentialSize = measurement->stateSize(differentialMode);
    const auto algebraicSize = sensorAlgebraicSize + host->stateSize(algebraicMode);
    const auto differentialSize = sensorDifferentialSize + host->stateSize(differentialMode);
    measurement->setOffset(0, algebraicMode);
    measurement->setOffset(0, differentialMode);
    host->setOffset(sensorAlgebraicSize, algebraicMode);
    host->setOffset(sensorDifferentialSize, differentialMode);
    std::vector<double> algebraic(algebraicSize);
    std::vector<double> differential(differentialSize);
    std::vector<double> rate(differential.size());
    measurement->guessState(0.0, algebraic.data(), rate.data(), algebraicMode);
    measurement->guessState(0.0, differential.data(), rate.data(), differentialMode);
    host->guessState(0.0, algebraic.data(), rate.data(), algebraicMode);
    host->guessState(0.0, differential.data(), rate.data(), differentialMode);
    const auto deviationLoc = measurement->getOutputLoc(algebraicMode, 0);
    const auto rocofLoc = measurement->getOutputLoc(algebraicMode, 1);
    const auto filterLoc = control->getOutputLoc(differentialMode, 2) - 1;
    ASSERT_GE(deviationLoc, 0);
    ASSERT_GE(rocofLoc, 0);
    ASSERT_GE(filterLoc, 0);
    algebraic[deviationLoc] = 0.01;
    algebraic[rocofLoc] = 0.02;

    StateData algebraicData(0.0, algebraic.data(), rate.data());
    algebraicData.stateSize = static_cast<count_t>(algebraic.size());
    algebraicData.diffState = differential.data();
    algebraicData.pairIndex = differentialMode.offsetIndex;
    std::vector<double> algebraicResidual(algebraic.size());
    measurement->residual({}, algebraicData, algebraicResidual.data(), algebraicMode);
    host->residual({1.0, 0.0}, algebraicData, algebraicResidual.data(), algebraicMode);
    EXPECT_NEAR(algebraicResidual[deviationLoc], -0.01, 1e-12);
    EXPECT_NEAR(algebraicResidual[rocofLoc], -0.02, 1e-12);
    MatrixDataSparse<double> algebraicJacobian;
    measurement->jacobianElements({}, algebraicData, algebraicJacobian, {}, algebraicMode);
    host->jacobianElements({1.0, 0.0}, algebraicData, algebraicJacobian, {30, 31}, algebraicMode);
    EXPECT_NEAR(algebraicJacobian.at(deviationLoc, deviationLoc), -1.0, 1e-12);
    EXPECT_NEAR(algebraicJacobian.at(rocofLoc, rocofLoc), -1.0, 1e-12);

    StateData differentialData(0.0, differential.data(), rate.data());
    differentialData.stateSize = static_cast<count_t>(differential.size());
    differentialData.algState = algebraic.data();
    differentialData.pairIndex = algebraicMode.offsetIndex;
    differentialData.cj = 1.0;
    std::vector<double> differentialResidual(differential.size());
    const auto filterResidual = [&] {
        host->residual({1.0, 0.0}, differentialData, differentialResidual.data(), differentialMode);
        return differentialResidual[filterLoc];
    };
    control->set("kf", 0.0);
    control->set("kdf", 0.0);
    const double base = filterResidual();
    MatrixDataSparse<double> zeroGainJacobian;
    host->jacobianElements(
        {1.0, 0.0}, differentialData, zeroGainJacobian, {30, 31}, differentialMode);
    control->set("kf", 4.0);
    control->set("kdf", 0.5);
    EXPECT_NEAR(filterResidual() - base, -2.5, 1e-10);
    MatrixDataSparse<double> jacobian;
    host->jacobianElements({1.0, 0.0}, differentialData, jacobian, {30, 31}, differentialMode);
    const double combined = differentialResidual[filterLoc];
    constexpr double step = 1e-7;
    for (const auto location : {deviationLoc, rocofLoc}) {
        ASSERT_LT(location, differential.size());
        EXPECT_NEAR(jacobian.at(filterLoc, location),
                    zeroGainJacobian.at(filterLoc, location),
                    1e-12);
        algebraic[location] += step;
        EXPECT_NEAR((filterResidual() - combined) / step,
                    location == deviationLoc ? -200.0 : -25.0,
                    1e-5);
        algebraic[location] -= step;
        differential[location] += step;
        rate[location] += step;
        EXPECT_NEAR(jacobian.at(filterLoc, location), (filterResidual() - combined) / step, 1e-5);
        differential[location] -= step;
        rate[location] -= step;
    }
}

TEST(RenewableModels, SpeedCoupledWindAssemblyHasConsistentDaeJacobian)
{
    RenewableGenerator host;
    host.add(new REGCP1);
    auto* control = new REECA1;
    control->set("pflag", 1.0);
    host.add(control);
    auto* shaft = new WTDS;
    shaft->set("w0", 0.9);
    host.add(shaft);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(shaft->getOutput(0), 0.9, 1e-12);
    EXPECT_NEAR(control->getStates()[3], 0.8 / 0.9, 1e-12);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    const auto speedLoc = shaft->getOutputLoc(cDaeSolverMode, 0);
    const auto orderLoc = control->getOutputLoc(cDaeSolverMode, 2);
    state[speedLoc] = 0.95;
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    host.jacobianElements({1.0, 0.0}, data, jacobian, {30, 31}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& trialState,
                                const std::vector<double>& trialRate) {
        StateData trial(0.0, trialState.data(), trialRate.data());
        trial.stateSize = static_cast<count_t>(trialState.size());
        std::vector<double> residual(trialState.size());
        host.residual({1.0, 0.0}, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    constexpr double step = 1e-7;
    auto shifted = state;
    auto shiftedRate = rate;
    shifted[speedLoc] += step;
    shiftedRate[speedLoc] += step;
    const auto base = residualAt(state, rate);
    const auto perturbed = residualAt(shifted, shiftedRate);
    for (auto row : {orderLoc - 1, orderLoc, speedLoc}) {
        EXPECT_NEAR(jacobian.at(row, speedLoc), (perturbed[row] - base[row]) / step, 1e-4);
    }
}

TEST(RenewableModels, DyrAssemblesIndependentModelsInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECA1", "REPCA1"},
          std::vector<std::string_view>{"REPCA1", "REECA1", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* generator = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(generator, nullptr);
        EXPECT_EQ(generator->getName(), "bus101_Gen_1");
        EXPECT_EQ(generator->getUserID(), 77);
        EXPECT_NEAR(generator->get("mbase", units::MVAR), 50.0, 1e-12);
        auto* converter = dynamic_cast<REGCA1*>(generator->find("electrical"));
        auto* electrical = dynamic_cast<REECA1*>(generator->find("electrical_control"));
        auto* plant = dynamic_cast<REPCA1*>(generator->find("plant_control"));
        ASSERT_NE(converter, nullptr);
        ASSERT_NE(electrical, nullptr);
        ASSERT_NE(plant, nullptr);
        EXPECT_DOUBLE_EQ(converter->get("tg"), 0.1);
        EXPECT_DOUBLE_EQ(electrical->get("pqflag"), 1.0);
        EXPECT_DOUBLE_EQ(plant->get("refflag"), 1.0);
        generator->dynInitializeA(0.0, 0);
        IOdata fields;
        generator->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        const auto output = generator->getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode);
        EXPECT_NEAR(output[0], -0.8, 1e-12);
        EXPECT_NEAR(output[1], -0.1, 1e-12);
    }
}

TEST(RenewableModels, REPCA1DyrBindsRemoteMeasurementBus)
{
    auto simulation = renewableDyrSimulation();
    auto* remoteBus = new AcBus("remoteBus202");
    remoteBus->setUserID(202);
    remoteBus->set("voltage", 1.04);
    remoteBus->set("freq", 0.997);
    simulation->add(remoteBus);
    remoteBus->pFlowInitializeA(0.0, 0);
    remoteBus->dynInitializeA(0.0, 0);
    loadRenewableRecords(*simulation, {"REGCA1", "REECA1", "REPCA1"}, 101, 202, 1);

    auto* terminalBus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
    ASSERT_NE(terminalBus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(terminalBus->getGen(0));
    ASSERT_NE(host, nullptr);
    auto* plant = dynamic_cast<REPCA1*>(host->find("plant_control"));
    ASSERT_NE(plant, nullptr);
    EXPECT_DOUBLE_EQ(plant->get("vbus"), 202.0);
    EXPECT_DOUBLE_EQ(plant->get("fflag"), 1.0);
    EXPECT_EQ(plant->sourceBusID(RenewableSignal::terminalVoltage), 202);
    EXPECT_EQ(plant->sourceBusID(RenewableSignal::terminalFrequency), 202);

    host->dynInitializeA(0.0, 0);
    IOdata fields;
    host->dynInitializeB({1.0, 0.0, 1.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(plant->get("vref"), 1.04, 1e-12);

    remoteBus->setOffset(0, cDaeSolverMode);
    host->setOffset(2, cDaeSolverMode);
    std::vector<double> state(2 + host->stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    remoteBus->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    host->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    const auto remoteVoltageLocation = remoteBus->getOutputLoc(cDaeSolverMode, VOLTAGE_IN_LOCATION);
    ASSERT_NE(remoteVoltageLocation, kNullLocation);
    EXPECT_DOUBLE_EQ(remoteBus->getVoltage(data, cDaeSolverMode), state[remoteVoltageLocation]);

    MatrixDataSparse<double> voltageJacobian;
    host->jacobianElements({1.0, 0.0, 1.0},
                           data,
                           voltageJacobian,
                           {kNullLocation, kNullLocation, kNullLocation},
                           cDaeSolverMode);
    const auto residualAtRemoteVoltage = [&](double voltage) {
        auto shiftedState = state;
        shiftedState[remoteVoltageLocation] = voltage;
        StateData trial(0.0, shiftedState.data(), rate.data());
        trial.stateSize = static_cast<count_t>(shiftedState.size());
        trial.cj = 1.0;
        std::vector<double> residual(state.size());
        host->residual({1.0, 0.0, 1.0}, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    constexpr double voltageStep = 1e-7;
    const auto upperVoltage = residualAtRemoteVoltage(state[remoteVoltageLocation] + voltageStep);
    const auto lowerVoltage = residualAtRemoteVoltage(state[remoteVoltageLocation] - voltageStep);
    double largestRemoteVoltageDerivative = 0.0;
    for (std::size_t row = 0; row < state.size(); ++row) {
        const auto rowIndex = static_cast<index_t>(row);
        const double finiteDifference =
            (upperVoltage[row] - lowerVoltage[row]) / (2.0 * voltageStep);
        EXPECT_NEAR(voltageJacobian.at(rowIndex, remoteVoltageLocation), finiteDifference, 1e-3)
            << "row " << row;
        largestRemoteVoltageDerivative =
            std::max(largestRemoteVoltageDerivative, std::abs(finiteDifference));
    }
    EXPECT_GT(largestRemoteVoltageDerivative, 10.0);

    const auto derivativeAtRemoteFrequency = [&](double frequency) {
        remoteBus->set("freq", frequency);
        std::vector<double> derivative(state.size());
        host->derivative({1.0, 0.0, 1.0}, data, derivative.data(), cDaeSolverMode);
        return derivative;
    };
    const auto nominalFrequency = derivativeAtRemoteFrequency(1.0);
    const auto underFrequency = derivativeAtRemoteFrequency(0.997);
    double largestDerivativeChange = 0.0;
    for (std::size_t index = 0; index < state.size(); ++index) {
        largestDerivativeChange =
            std::max(largestDerivativeChange,
                     std::abs(underFrequency[index] - nominalFrequency[index]));
    }
    EXPECT_GT(largestDerivativeChange, 1e-3);
}

TEST(RenewableModels, REPCA1DyrRejectsMissingRemoteBusAndMonitoredLine)
{
    auto missingBusSimulation = renewableDyrSimulation();
    loadRenewableRecords(*missingBusSimulation, {"REGCA1", "REECA1", "REPCA1"}, 101, 202, 0);
    auto* terminalBus = dynamic_cast<GridBus*>(missingBusSimulation->findByUserID("bus", 101));
    ASSERT_NE(terminalBus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(terminalBus->getGen(0));
    ASSERT_NE(host, nullptr);
    EXPECT_THROW(host->dynInitializeA(0.0, 0), InvalidParameterValue);

    auto monitoredLineSimulation = renewableDyrSimulation();
    EXPECT_THROW(loadRenewableRecords(*monitoredLineSimulation, {"REPCA1"}, 101, 0, 0, 101, 202),
                 InvalidParameterValue);
}

TEST(RenewableModels, DyrLoadsREGCP1AndWTDSInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCP1", "REECA1", "WTDS"},
          std::vector<std::string_view>{"WTDS", "REECA1", "REGCP1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        EXPECT_NE(dynamic_cast<REGCP1*>(host->find("electrical")), nullptr);
        EXPECT_NE(dynamic_cast<WTDS*>(
                      host->getSubObject("renewable_component",
                                         static_cast<index_t>(RenewableRole::driveTrain))),
                  nullptr);
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    }
}

TEST(RenewableModels, DyrBindsREGCP1ToNamedPLLInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCP1_PLL", "PLL1"},
          std::vector<std::string_view>{"PLL1", "REGCP1_PLL"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        auto* pll = dynamic_cast<PLL1Sensor*>(simulation->getRelay(0));
        ASSERT_NE(host, nullptr);
        ASSERT_NE(pll, nullptr);
        auto* converter = dynamic_cast<REGCP1*>(host->find("electrical"));
        ASSERT_NE(converter, nullptr);
        EXPECT_EQ(converter->sourceName(RenewableSignal::measuredAngle), "pll1a");
        pll->dynInitializeA(0.0, 0);
        IOdata fields;
        pll->dynInitializeB({}, {}, fields);
        host->dynInitializeA(0.0, 0);
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        const auto activeCurrent = converter->getStates()[2];
        const auto reactiveCurrent = converter->getStates()[3];
        host->timestep(0.0, {1.0, 0.2}, cLocalSolverMode);
        EXPECT_NEAR(converter->getStates()[0],
                    (std::cos(0.2) * activeCurrent) - (std::sin(0.2) * reactiveCurrent),
                    1e-12);
        EXPECT_NEAR(converter->getStates()[1],
                    (std::sin(0.2) * activeCurrent) + (std::cos(0.2) * reactiveCurrent),
                    1e-12);
    }
    auto missing = renewableDyrSimulation();
    loadRenewableRecords(*missing, {"REGCP1_PLL"});
    auto* bus = dynamic_cast<GridBus*>(missing->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(host, nullptr);
    EXPECT_THROW(host->dynInitializeA(0.0, 0), InvalidParameterValue);

    auto wrongType = renewableDyrSimulation();
    loadRenewableRecords(*wrongType, {"REGCP1_PLL"});
    auto* wrongTypeBus = dynamic_cast<GridBus*>(wrongType->findByUserID("bus", 101));
    ASSERT_NE(wrongTypeBus, nullptr);
    auto* frequency = new FreqDivSensor("pll1a");
    frequency->setSource(wrongTypeBus);
    wrongType->add(frequency);
    auto* wrongTypeHost = dynamic_cast<RenewableGenerator*>(wrongTypeBus->getGen(0));
    ASSERT_NE(wrongTypeHost, nullptr);
    EXPECT_THROW(wrongTypeHost->dynInitializeA(0.0, 0), InvalidParameterValue);

    auto wrongBus = renewableDyrSimulation();
    loadRenewableRecords(*wrongBus, {"REGCP1_PLL"});
    auto* otherBus = new AcBus("otherBus");
    wrongBus->add(otherBus);
    auto* otherPll = new PLL1Sensor("pll1a");
    otherPll->setSource(otherBus);
    wrongBus->add(otherPll);
    auto* originalBus = dynamic_cast<GridBus*>(wrongBus->findByUserID("bus", 101));
    ASSERT_NE(originalBus, nullptr);
    auto* wrongBusHost = dynamic_cast<RenewableGenerator*>(originalBus->getGen(0));
    ASSERT_NE(wrongBusHost, nullptr);
    EXPECT_THROW(wrongBusHost->dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, REGCP1HostJacobianCouplesBusAndPLLAngles)
{
    auto simulation = renewableDyrSimulation();
    loadRenewableRecords(*simulation, {"PLL1", "REGCP1_PLL"});
    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    auto* owner = dynamic_cast<GridArea*>(bus->getParent());
    ASSERT_NE(owner, nullptr);
    auto* pll = dynamic_cast<PLL1Sensor*>(owner->getRelay(0));
    ASSERT_NE(host, nullptr);
    ASSERT_NE(pll, nullptr);
    pll->dynInitializeA(0.0, 0);
    IOdata fields;
    pll->dynInitializeB({}, {}, fields);
    host->dynInitializeA(0.0, 0);
    host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    const auto sensorSize = pll->stateSize(cDaeSolverMode);
    pll->setOffset(0, cDaeSolverMode);
    host->setOffset(sensorSize, cDaeSolverMode);
    std::vector<double> state(sensorSize + host->stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    pll->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    host->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    const auto pllAngle = pll->getOutputLoc(cDaeSolverMode, 0);
    state[pllAngle] = 0.04;
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    IOdata inputs{1.0, 0.2};
    MatrixDataSparse<double> jacobian;
    host->jacobianElements(inputs, data, jacobian, {30, 31}, cDaeSolverMode);
    auto* converter = dynamic_cast<REGCP1*>(host->find("electrical"));
    ASSERT_NE(converter, nullptr);
    const auto power = converter->getOutputLoc(cDaeSolverMode, 0);
    const auto reactive = converter->getOutputLoc(cDaeSolverMode, 1);
    const auto evaluate = [&]() {
        std::vector<double> residual(state.size());
        host->residual(inputs, data, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = evaluate();
    constexpr double step = 1e-7;
    state[pllAngle] += step;
    const auto pllShift = evaluate();
    state[pllAngle] -= step;
    inputs[1] += step;
    const auto busShift = evaluate();
    for (auto row : {power, reactive}) {
        EXPECT_NEAR(jacobian.at(row, pllAngle), (pllShift[row] - base[row]) / step, 1e-5);
        EXPECT_NEAR(jacobian.at(row, 31), (busShift[row] - base[row]) / step, 1e-5);
    }
}

TEST(RenewableModels, DyrLoadsREECA1EAndBusROCOFInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECA1E", "BUSROCOF"},
          std::vector<std::string_view>{"BUSROCOF", "REECA1E", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        auto* control = dynamic_cast<REECA1E*>(host->find("electrical_control"));
        auto* measurement = dynamic_cast<BusROCOFSensor*>(simulation->getRelay(0));
        ASSERT_NE(control, nullptr);
        ASSERT_NE(measurement, nullptr);
        EXPECT_EQ(host->find("measurement"), nullptr);
        EXPECT_EQ(measurement->getName(), "freq1");
        EXPECT_DOUBLE_EQ(control->get("kf"), 4.0);
        EXPECT_DOUBLE_EQ(control->get("kdf"), 0.5);
        measurement->dynInitializeA(0.0, 0);
        IOdata measurementFields;
        measurement->dynInitializeB({}, {}, measurementFields);
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        bus->set("angle", 0.1);
        host->timestep(0.01, {1.0, 0.1}, cLocalSolverMode);
        EXPECT_GT(measurement->getOutput(0), 0.0);
    }
}

TEST(RenewableModels, DyrLoadsMultipleAreaMeasurements)
{
    auto simulation = renewableDyrSimulation();
    loadRenewableRecords(*simulation, {"PLL1", "PLL2", "FREQDIV"});
    EXPECT_NE(dynamic_cast<PLL1Sensor*>(simulation->getRelay(0)), nullptr);
    EXPECT_NE(dynamic_cast<PLL2Sensor*>(simulation->getRelay(1)), nullptr);
    EXPECT_NE(dynamic_cast<FreqDivSensor*>(simulation->getRelay(2)), nullptr);
    EXPECT_EQ(simulation->getRelay(3), nullptr);
}

TEST(RenewableModels, REECB1LoadsAsIndependentElectricalControl)
{
    std::unique_ptr<CoreObject> factoryObject(
        CoreObjectFactory::instance()->createObject("renewable_model", "reecb1"));
    ASSERT_NE(dynamic_cast<REECB1*>(factoryObject.get()), nullptr);
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECB1"},
          std::vector<std::string_view>{"REECB1", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        auto* control = dynamic_cast<REECB1*>(host->find("electrical_control"));
        ASSERT_NE(control, nullptr);
        EXPECT_DOUBLE_EQ(control->get("imax"), 999.0);
        EXPECT_THROW(control->set("ip1", 2.0), InvalidParameterValue);
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        EXPECT_NEAR(host->getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.8, 1e-12);
        std::unique_ptr<CoreObject> copy(control->clone());
        EXPECT_NE(dynamic_cast<REECB1*>(copy.get()), nullptr);
    }
}

TEST(RenewableModels, DyrRejectsDuplicateAndUnsupportedRecords)
{
    auto duplicate = renewableDyrSimulation();
    EXPECT_THROW(loadRenewableRecords(*duplicate, {"REGCA1", "REGCA1"}), InvalidParameterValue);
    auto missingConverter = renewableDyrSimulation();
    loadRenewableRecords(*missingConverter, {"REECA1"});
    auto* bus = dynamic_cast<GridBus*>(missingConverter->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    EXPECT_THROW(generator->dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, DyrLoadsDedicatedType3WindElectricalModels)
{
    auto simulation = renewableDyrSimulation();
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_activsg25k_wt3_" + std::to_string(simulation->getID()) + ".dyr");
    {
        std::ofstream output(file);
        output << "101 'WT3G1' '1' 3 0.8 30 0 0.1 2.75 /\n";
        output << "101 'WT3E1' '1' 0 1 1 0 0 0 0.15 18 5 0 0.05 "
                  "3 0.6 1.12 0.04 0.436 -0.436 1.7 0.02 0.45 -0.45 "
                  "60 0.1 0.9 1.1 40 0.5 1.45 0.05 0.05 1 "
                  "0.69 0.78 0.98 1.12 0.74 1.2 /\n";
    }
    ASSERT_NO_THROW(loadDyr(simulation.get(), file.string(), BasicReaderInfo{}));
    std::filesystem::remove(file);
    auto* generator = dynamic_cast<RenewableGenerator*>(simulation->getBus(0)->getGen(0));
    ASSERT_NE(generator, nullptr);
    EXPECT_NE(dynamic_cast<WT3G1*>(generator->find("electrical")), nullptr);
    EXPECT_NE(dynamic_cast<WT3E1*>(generator->find("electrical_control")), nullptr);
}

TEST(RenewableModels, DyrLoadsDedicatedType4WindElectricalModels)
{
    auto simulation = renewableDyrSimulation();
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_wt4_" + std::to_string(simulation->getID()) + ".dyr");
    {
        std::ofstream output(file);
        output << "101 'WT4G1' '1' 100 0.02 0.02 0.4 0.8 1.0 1.2 0.0 10.0 0.02 /\n";
        output << "101 'WT4E1' '1' 0 0 1 0 0.02 1.0 0.1 1.0 0.1 0.0 0.02 "
                  "999 -999 2.0 0.02 999 -999 0.02 0.0 0.9 1.1 0.0 0.02 0.02 2.0 2.0 2.0 /\n";
    }
    ASSERT_NO_THROW(loadDyr(simulation.get(), file.string(), BasicReaderInfo{}));
    std::filesystem::remove(file);
    auto* generator = dynamic_cast<RenewableGenerator*>(simulation->getBus(0)->getGen(0));
    ASSERT_NE(generator, nullptr);
    EXPECT_NE(dynamic_cast<WT4G1*>(generator->find("electrical")), nullptr);
    EXPECT_NE(dynamic_cast<WT4E1*>(generator->find("electrical_control")), nullptr);
}

TEST(RenewableModels, Type3AndType4WindHostsInitializeAtSteadyState)
{
    const auto initializeAndCheck = [](RenewableGenerator& host) {
        host.dynInitializeA(0.0, 0);
        IOdata fields;
        host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        host.setOffset(0, cDaeSolverMode);
        std::vector<double> state(host.stateSize(cDaeSolverMode));
        std::vector<double> rate(state.size());
        host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
        StateData data(0.0, state.data(), rate.data());
        data.stateSize = static_cast<count_t>(state.size());
        std::vector<double> residual(state.size());
        host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
        for (const auto value : residual) {
            EXPECT_NEAR(value, 0.0, 1e-8);
        }
    };

    RenewableGenerator type3;
    type3.add(new WT3G1);
    type3.add(new WT3E1);
    initializeAndCheck(type3);

    RenewableGenerator type4;
    type4.add(new WT4G1);
    type4.add(new WT4E1);
    initializeAndCheck(type4);
}

TEST(RenewableModels, Type3AndType4WindHostDaeJacobiansMatchResiduals)
{
    const auto checkJacobian = [](RenewableGenerator& host) {
        host.dynInitializeA(0.0, 0);
        IOdata fields;
        host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        host.setOffset(0, cDaeSolverMode);
        std::vector<double> state(host.stateSize(cDaeSolverMode));
        std::vector<double> rate(state.size());
        host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
        StateData data(0.0, state.data(), rate.data());
        data.stateSize = static_cast<count_t>(state.size());
        data.cj = 1.0;
        MatrixDataSparse<double> jacobian;
        host.jacobianElements({1.0, 0.0}, data, jacobian, {20, 21}, cDaeSolverMode);
        const auto residualAt = [&](const IOdata& input,
                                    const std::vector<double>& values,
                                    const std::vector<double>& rates) {
            StateData trial(0.0, values.data(), rates.data());
            trial.stateSize = static_cast<count_t>(values.size());
            std::vector<double> residual(values.size());
            host.residual(input, trial, residual.data(), cDaeSolverMode);
            return residual;
        };
        const IOdata baseInput{1.0, 0.0};
        const auto base = residualAt(baseInput, state, rate);
        constexpr double step = 1e-7;
        for (std::size_t column = 0; column < state.size(); ++column) {
            auto shifted = state;
            auto shiftedRate = rate;
            shifted[column] += step;
            shiftedRate[column] += step;
            const auto residual = residualAt(baseInput, shifted, shiftedRate);
            for (std::size_t row = 0; row < state.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                            (residual[row] - base[row]) / step,
                            3e-4)
                    << "row " << row << " column " << column;
            }
        }
        for (std::size_t inputIndex = 0; inputIndex < baseInput.size(); ++inputIndex) {
            auto shiftedInput = baseInput;
            shiftedInput[inputIndex] += step;
            const auto residual = residualAt(shiftedInput, state, rate);
            const auto column = static_cast<index_t>(20 + inputIndex);
            for (std::size_t row = 0; row < state.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), column),
                            (residual[row] - base[row]) / step,
                            3e-4)
                    << "input " << inputIndex << " row " << row;
            }
        }
    };

    RenewableGenerator type3;
    type3.add(new WT3G1);
    type3.add(new WT3E1);
    checkJacobian(type3);

    RenewableGenerator type4;
    type4.add(new WT4G1);
    type4.add(new WT4E1);
    checkJacobian(type4);
}

TEST(RenewableModels, ThreeModelDaeJacobianIncludesSignalConnections)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    host.add(new REPCA1);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    host.jacobianElements({1.0, 0.0}, data, jacobian, {20, 21}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& values,
                                const std::vector<double>& rates) {
        StateData trial(0.0, values.data(), rates.data());
        trial.stateSize = static_cast<count_t>(values.size());
        std::vector<double> residual(values.size());
        host.residual({1.0, 0.0}, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = residualAt(state, rate);
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto shifted = state;
        auto shiftedRate = rate;
        shifted[column] += step;
        shiftedRate[column] += step;
        const auto residual = residualAt(shifted, shiftedRate);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (residual[row] - base[row]) / step,
                        2e-4)
                << "row " << row << " column " << column;
        }
    }
}

TEST(RenewableModels, WindDyrAssemblesConnectedChainInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECA1", "WTDTA1", "WTARA1", "WTPTA1", "WTTQA1"},
          std::vector<std::string_view>{
              "WTTQA1", "WTPTA1", "WTARA1", "WTDTA1", "REECA1", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        auto* drivetrain = dynamic_cast<WTDTA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::driveTrain)));
        auto* aero = dynamic_cast<WTARA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::aerodynamics)));
        auto* pitch = dynamic_cast<WTPTA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::pitchControl)));
        auto* torque = dynamic_cast<WTTQA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::torqueControl)));
        ASSERT_NE(drivetrain, nullptr);
        ASSERT_NE(aero, nullptr);
        ASSERT_NE(pitch, nullptr);
        ASSERT_NE(torque, nullptr);
        EXPECT_DOUBLE_EQ(drivetrain->get("htfrac"), 0.5);
        EXPECT_DOUBLE_EQ(pitch->get("rtetamin"), -5.0);
        EXPECT_DOUBLE_EQ(torque->get("spd4"), 1.0);
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        EXPECT_NEAR(drivetrain->getOutput(0), 1.0, 1e-12);
        EXPECT_NEAR(drivetrain->getOutput(1), 1.0, 1e-12);
        EXPECT_NEAR(aero->getOutput(0), 1.6, 1e-12);
        EXPECT_NEAR(pitch->getOutput(0), 0.0, 1e-12);
        EXPECT_NEAR(torque->getOutput(0), 1.6, 1e-12);
        host->setOffset(0, cDaeSolverMode);
        std::vector<double> state(host->stateSize(cDaeSolverMode));
        std::vector<double> rate(state.size());
        host->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
        StateData data(0.0, state.data(), rate.data());
        data.stateSize = static_cast<count_t>(state.size());
        std::vector<double> residual(state.size());
        host->residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
        for (double value : residual) {
            EXPECT_NEAR(value, 0.0, 1e-8);
        }
    }
}

TEST(RenewableModels, WindAssemblyRejectsMissingLinksAndInvalidParameters)
{
    RenewableGenerator missingTorque;
    missingTorque.add(new REGCA1);
    missingTorque.add(new REECA1);
    missingTorque.add(new WTDTA1);
    EXPECT_THROW(missingTorque.dynInitializeA(0.0, 0), InvalidParameterValue);
    WTDTA1 drivetrain;
    drivetrain.set("htfrac", 0.0);
    EXPECT_THROW(drivetrain.dynInitializeA(0.0, 0), InvalidParameterValue);
    WTPTA1 pitch;
    pitch.set("rtetamax", 0.0);
    EXPECT_THROW(pitch.dynInitializeA(0.0, 0), InvalidParameterValue);
    WTTQA1 torque;
    EXPECT_THROW(torque.set("Tflag", 0.5), InvalidParameterValue);
    torque.set("p2", 0.2);
    EXPECT_THROW(torque.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, WindChainDaeJacobianIncludesMechanicalFeedback)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    auto* drivetrain = new WTDTA1;
    auto* aero = new WTARA1;
    auto* pitch = new WTPTA1;
    auto* torque = new WTTQA1;
    torque->set("temax", 3.0);
    host.add(drivetrain);
    host.add(aero);
    host.add(pitch);
    host.add(torque);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    const auto theta = pitch->getOutputLoc(cDaeSolverMode, 0);
    const auto windGenerator = drivetrain->getOutputLoc(cDaeSolverMode, 0);
    const auto wref = torque->getOutputLoc(cDaeSolverMode, 1);
    state[theta] = 2.0;
    state[theta + 1] = 1.0;
    state[theta + 2] = 1.0;
    state[windGenerator] = 1.02;
    state[windGenerator + 1] = 1.01;
    state[wref - 1] = 0.7;
    state[wref] = 0.95;
    state[wref + 1] = 0.8;
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    host.jacobianElements({1.0, 0.0}, data, jacobian, {30, 31}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& values,
                                const std::vector<double>& rates) {
        StateData trial(0.0, values.data(), rates.data());
        trial.stateSize = static_cast<count_t>(values.size());
        std::vector<double> residual(values.size());
        host.residual({1.0, 0.0}, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = residualAt(state, rate);
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto shifted = state;
        auto shiftedRate = rate;
        shifted[column] += step;
        shiftedRate[column] += step;
        const auto residual = residualAt(shifted, shiftedRate);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (residual[row] - base[row]) / step,
                        3e-4)
                << "row " << row << " column " << column;
        }
    }
}

TEST(RenewableModels, PitchChangesMechanicalPower)
{
    WTPTA1 pitch;
    pitch.set("kpw", 10.0);
    pitch.dynInitializeA(0.0, 0);
    IOdata fields;
    pitch.dynInitializeB({1.0, 0.8, 0.8, 1.0, 0.0}, {}, fields);
    pitch.timestep(0.1, {1.05, 0.8, 0.8, 1.0, 0.0}, cLocalSolverMode);
    EXPECT_GT(pitch.getOutput(0), 0.0);
    WTARA1 aero;
    aero.dynInitializeA(0.0, 0);
    aero.dynInitializeB({0.0}, {0.8}, fields);
    aero.timestep(0.1, {pitch.getOutput(0)}, cLocalSolverMode);
    EXPECT_LT(aero.getOutput(0), 0.8);
}

TEST(RenewableModels, WindTorqueSetsNonunitySteadySpeed)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    auto* drivetrain = new WTDTA1;
    auto* torque = new WTTQA1;
    host.add(drivetrain);
    host.add(torque);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.5, 0.1}, fields);
    EXPECT_NEAR(drivetrain->getOutput(0), 0.79, 1e-12);
    EXPECT_NEAR(drivetrain->getOutput(1), 0.79, 1e-12);
    EXPECT_NEAR(torque->getOutput(0), 0.5, 1e-12);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    std::vector<double> residual(state.size());
    host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-8);
    }
}

TEST(RenewableModels, WindDyrIntegratesInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    auto simulation = std::make_unique<GridDynSimulation>();
    loadFile(simulation.get(), raw.string());
    loadRenewableRecords(*simulation,
                         {"REGCA1", "REECA1", "WTDTA1", "WTARA1", "WTPTA1", "WTTQA1"},
                         2);
    loadOtherMachines(*simulation);
    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(dynamic_cast<RenewableGenerator*>(bus->getGen(0)), nullptr);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.1), 0);
    for (double value : simulation->getState()) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

TEST(RenewableModels, SpeedCoupledWindDyrIntegratesInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    auto simulation = std::make_unique<GridDynSimulation>();
    loadFile(simulation.get(), raw.string());
    loadRenewableRecords(*simulation, {"REGCP1", "REECA1", "WTDS"}, 2);
    loadOtherMachines(*simulation);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.1), 0);
    for (double value : simulation->getState()) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

TEST(RenewableModels, REGCP1NamedPLLIntegratesInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    auto simulation = std::make_unique<GridDynSimulation>();
    loadFile(simulation.get(), raw.string());
    loadRenewableRecords(*simulation, {"PLL1", "REGCP1_PLL", "REECA1"}, 2);
    loadOtherMachines(*simulation);
    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    auto* owner = dynamic_cast<GridArea*>(bus->getParent());
    ASSERT_NE(owner, nullptr);
    auto* pll = dynamic_cast<PLL1Sensor*>(owner->getRelay(0));
    ASSERT_NE(host, nullptr);
    ASSERT_NE(pll, nullptr);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_NEAR(pll->getOutput(0), bus->getAngle(), 1e-10);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.1), 0);
    for (double value : simulation->getState()) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

TEST(RenewableModels, SolarDyrIntegratesInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    for (const auto* electricalControl : {"REECA1", "REECB1"}) {
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation.get(), raw.string());
        loadRenewableRecords(*simulation, {"REGCA1", electricalControl}, 2);
        if (std::string_view{electricalControl} == "REECA1") {
            auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
            ASSERT_NE(bus, nullptr);
            auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
            ASSERT_NE(host, nullptr);
            auto* electrical = dynamic_cast<REECA1*>(host->find("electrical_control"));
            ASSERT_NE(electrical, nullptr);
            electrical->set("thld2", 0.5);
            electrical->set("tpord", 0.0);
        }
        loadOtherMachines(*simulation);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        if (std::string_view{electricalControl} == "REECA1") {
            EXPECT_GT(simulation->rootSize(cDaeSolverMode), 0U);
        }
        EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
        ASSERT_EQ(simulation->run(0.1), 0);
    }
}

TEST(RenewableModels, DistributedConvertersIntegrateInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    for (int variant = 0; variant < 4; ++variant) {
        SCOPED_TRACE(variant);
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation.get(), raw.string());
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
        ASSERT_NE(bus, nullptr);
        auto* staticGenerator = bus->getGen(0);
        ASSERT_NE(staticGenerator, nullptr);
        auto* host = new RenewableGenerator("distributed_bus2");
        host->set("p", -staticGenerator->get("p"));
        host->set("q", -staticGenerator->get("q"));
        host->set("mbase", 100.0, units::MVAR);
        DistributedConverter* converter = nullptr;
        switch (variant) {
            case 0:
                converter = new PVD1;
                break;
            case 1:
                converter = new ESD1;
                break;
            case 2:
                converter = new EV1;
                break;
            default:
                converter = new EV2;
                break;
        }
        converter->set("vrflag", 1.0);
        converter->set("frflag", 1.0);
        converter->set("pmn", -2.0);
        if (variant == 3) {
            converter->set("pcap", 1.0);
        }
        host->add(converter);
        staticGenerator->disable();
        bus->add(host);
        if (variant == 0) {
            auto* protection = new DGPRCT1("distributed_protection");
            protection->setSource(bus);
            protection->setSink(converter);
            protection->set("fen", 0.0);
            protection->set("ven", 1.0);
            simulation->add(protection);
        }
        loadOtherMachines(*simulation);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
        ASSERT_EQ(simulation->run(0.05), 0);
        for (double value : simulation->getState()) {
            EXPECT_TRUE(std::isfinite(value));
        }
    }
}

TEST(RenewableModels, TwoBusRenewableFaultMatchesAndesReference)
{
    for (int profile = 0; profile < 6; ++profile) {
        const bool includePlant = profile == 1;
        const bool includeWind = profile == 2;
        const bool useReecb = profile == 3;
        const bool useWtds = profile == 4;
        const bool useReeca1e = profile == 5;
        SCOPED_TRACE(useReeca1e       ? "REECA1E frequency assembly" :
                         useWtds      ? "WTDS speed assembly" :
                         includeWind  ? "wind assembly" :
                         includePlant ? "REPCA1" :
                         useReecb     ? "REECB1" :
                                        "REGCA1+REECA1");
        const auto xml = std::filesystem::path(__FILE__).parent_path().parent_path() / "reference" /
            "renewable_fault" / "two_bus.xml";
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation.get(), xml.string());
        auto* bus = dynamic_cast<GridBus*>(simulation->find("solar_bus"));
        ASSERT_NE(bus, nullptr);
        auto* host = new RenewableGenerator("solar");
        host->set("p", 0.6);
        host->set("mbase", 100.0, units::MVAR);
        auto* reg = useWtds ? static_cast<REGCA1*>(new REGCP1) : new REGCA1;
        reg->set("tg", 0.02);
        reg->set("tfltr", 0.02);
        reg->set("iqrmax", 999.0);
        reg->set("iqrmin", -999.0);
        reg->set("lvplsw", 0.0);
        REECA1* ree = nullptr;
        if (useReecb) {
            ree = new REECB1;
        } else if (useReeca1e) {
            ree = new REECA1E;
        } else {
            ree = new REECA1;
        }
        ree->set("vflag", 0.0);
        ree->set("pqflag", 0.0);
        if (!useReecb) {
            ree->set("thld2", 0.5);
        }
        if (useWtds) {
            ree->set("pflag", 1.0);
        }
        if (useReeca1e) {
            ree->set("kf", 4.0);
            ree->set("kdf", 0.5);
            static_cast<REECA1E*>(ree)->set("busroc", std::string_view{"freq1"});
        }
        ree->set("tpord", useReecb || useWtds ? 0.02 : 0.0);
        ree->set("imax", 1.3);
        ree->set("qmax", 1.0);
        ree->set("qmin", -1.0);
        if (!useReecb) {
            for (int k = 1; k <= 4; ++k) {
                auto suffix = std::to_string(k);
                ree->set("iq" + suffix, 1.3);
                ree->set("ip" + suffix, 1.3);
            }
        }
        host->add(reg);
        host->add(ree);
        if (includePlant) {
            auto* plant = new REPCA1;
            plant->set("vcflag", 0.0);
            plant->set("refflag", 1.0);
            plant->set("fflag", 0.0);
            plant->set("plflag", 0.0);
            host->add(plant);
        }
        WTDTA1* shaft = nullptr;
        WTARA1* aerodynamic = nullptr;
        WTPTA1* pitch = nullptr;
        if (includeWind) {
            shaft = new WTDTA1;
            shaft->set("h", 6.0);
            aerodynamic = new WTARA1;
            pitch = new WTPTA1;
            host->add(shaft);
            host->add(aerodynamic);
            host->add(pitch);
            host->add(new WTTQA1);
        }
        WTDS* oneMass = nullptr;
        if (useWtds) {
            oneMass = new WTDS;
            host->add(oneMass);
        }
        BusROCOFSensor* measurement = nullptr;
        if (useReeca1e) {
            auto* owner = dynamic_cast<GridArea*>(bus->getParent());
            ASSERT_NE(owner, nullptr);
            measurement = new BusROCOFSensor("freq1");
            measurement->setSource(bus);
            owner->add(measurement);
        }
        bus->add(host);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        std::string referenceName = "andes_reference.csv";
        if (includePlant) {
            referenceName = "andes_plant_reference.csv";
        }
        if (includeWind) {
            referenceName = "andes_wind_reference.csv";
        }
        if (useReecb) {
            referenceName = "andes_reecb_reference.csv";
        }
        if (useWtds) {
            referenceName = "andes_wtds_reference.csv";
        }
        if (useReeca1e) {
            referenceName = "andes_reeca1e_reference.csv";
        }
        const auto reference = xml.parent_path() / referenceName;
        std::ifstream stream(reference);
        ASSERT_TRUE(stream.is_open());
        std::string line;
        ASSERT_TRUE(static_cast<bool>(std::getline(stream, line)));
        const std::string header =
            "time,voltage,converter_p,converter_q,electrical_ip,electrical_iq";
        EXPECT_EQ(line,
                  includeWind ?
                      header + ",generator_speed,turbine_speed,pitch_angle,mechanical_power" :
                      useWtds    ? header + ",generator_speed" :
                      useReeca1e ? header + ",frequency_deviation,rocof" :
                                   header);
        int samples = 0;
        while (std::getline(stream, line)) {
            if (line.empty()) {
                continue;
            }
            std::istringstream row(line);
            std::size_t expectedSize = 6;
            if (useReeca1e) {
                expectedSize = 8;
            }
            if (useWtds) {
                expectedSize = 7;
            }
            if (includeWind) {
                expectedSize = 10;
            }
            std::vector<double> expected(expectedSize);
            for (auto& value : expected) {
                std::string field;
                ASSERT_TRUE(static_cast<bool>(std::getline(row, field, ',')));
                value = std::stod(field);
            }
            const double timeValue = expected[0];
            ASSERT_EQ(simulation->run(timeValue), 0) << timeValue;
            EXPECT_NEAR(bus->getVoltage(), expected[1], 0.012) << timeValue;
            EXPECT_NEAR(reg->getStates()[0], expected[2], 0.012) << timeValue;
            EXPECT_NEAR(reg->getStates()[1], expected[3], 0.012) << timeValue;
            EXPECT_NEAR(ree->getStates()[0], expected[4], 0.012) << timeValue;
            EXPECT_NEAR(ree->getStates()[1], expected[5], 0.012) << timeValue;
            if (includeWind) {
                EXPECT_NEAR(shaft->getStates()[0], expected[6], 0.001) << timeValue;
                EXPECT_NEAR(shaft->getStates()[1], expected[7], 0.001) << timeValue;
                EXPECT_NEAR(pitch->getStates()[0], expected[8], 0.0001) << timeValue;
                EXPECT_NEAR(aerodynamic->getStates()[0], expected[9], 0.002) << timeValue;
            }
            if (useWtds) {
                EXPECT_NEAR(oneMass->getStates()[0], expected[6], 0.001) << timeValue;
            }
            if (useReeca1e) {
                EXPECT_NEAR(measurement->getStates()[0], expected[6], 0.0006) << timeValue;
                EXPECT_NEAR(measurement->getStates()[1], expected[7], 0.005) << timeValue;
            }
            ++samples;
        }
        EXPECT_EQ(samples, 8);
    }
}

TEST(RenewableModels, PartitionedEquationChecks)
{
    for (const auto profile : renewableDynamicProfiles) {
        SCOPED_TRACE(renewableDynamicProfileName(profile));
        auto host = renewableDynamicHost(profile);
        checkRenewablePartitionedHost(*host,
                                      {1.0, 0.0},
                                      {0.6, 0.1},
                                      renewableDynamicProfileName(profile));
    }
}

TEST(RenewableModels, SmallNetworkDaeAndPartitionedSweep)
{
    constexpr std::array networkCases{
        std::pair{RenewableDynamicProfile::gridFollowing, std::string_view{"partitioned"}},
        std::pair{RenewableDynamicProfile::speedCoupledWind, std::string_view{"dae"}},
        std::pair{RenewableDynamicProfile::speedCoupledWind, std::string_view{"partitioned"}},
        std::pair{RenewableDynamicProfile::type4Wind, std::string_view{"partitioned"}},
    };
    for (const auto& [profile, solverMethod] : networkCases) {
        SCOPED_TRACE(std::string{renewableDynamicProfileName(profile)} + " with " +
                     std::string{solverMethod});
        auto simulation = renewableNetworkCase(profile, solverMethod);
        ASSERT_EQ(simulation->dynInitialize(), 0);

        if (solverMethod == "dae") {
            EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
            EXPECT_EQ(runDerivativeCheck(simulation, cDaeSolverMode, false), 0);
            EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
        } else {
            // Partitioned startup configures the paired modes used by the diagnostics helpers.
            ASSERT_EQ(simulation->run(0.005), 0);
            const auto algMode = simulation->getSolverMode("dynalg");
            const auto diffMode = simulation->getSolverMode("dyndiff");
            EXPECT_EQ(runResidualCheck(simulation, algMode, false), 0);
            EXPECT_EQ(runResidualCheck(simulation, diffMode, false), 0);
            EXPECT_EQ(runAlgebraicCheck(simulation, algMode, false), 0);
            EXPECT_EQ(runDerivativeCheck(simulation, diffMode, false), 0);
            EXPECT_EQ(runJacobianCheck(simulation, algMode, false), 0);
            EXPECT_EQ(runJacobianCheck(simulation, diffMode, false), 0);
        }

        std::vector<double> initialVoltages;
        simulation->getVoltage(initialVoltages);
        ASSERT_EQ(simulation->run(0.05), 0);
        std::vector<double> finalVoltages;
        simulation->getVoltage(finalVoltages);
        ASSERT_EQ(finalVoltages.size(), initialVoltages.size());
        ASSERT_GT(finalVoltages.size(), 1U);
        EXPECT_GT(finalVoltages[1], 0.9);
        EXPECT_LT(finalVoltages[1], 1.1);
        EXPECT_LT(std::abs(finalVoltages[1] - initialVoltages[1]), 0.02);
        if (solverMethod == "dae") {
            const auto state = simulation->getState(cDaeSolverMode);
            EXPECT_TRUE(std::all_of(state.begin(), state.end(), [](double value) {
                return std::isfinite(value);
            }));
        } else {
            for (const auto mode : {cDynAlgSolverMode, cDynDiffSolverMode}) {
                const auto state = simulation->getState(mode);
                EXPECT_TRUE(std::all_of(state.begin(), state.end(), [](double value) {
                    return std::isfinite(value);
                }));
            }
        }
    }
}

TEST(RenewableModels, RenewableFaultClearingStabilityBothSolvers)
{
    struct FaultSample {
        double voltage;
        double activePower;
        double reactivePower;
    };

    for (const auto solverMethod : {std::string_view{"dae"}, std::string_view{"partitioned"}}) {
        SCOPED_TRACE(solverMethod);
        auto simulation =
            renewableNetworkCase(RenewableDynamicProfile::speedCoupledWind, solverMethod);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        auto* bus = dynamic_cast<GridBus*>(simulation->find("solar_bus"));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        auto* converter = dynamic_cast<REGCP1*>(host->find("electrical"));
        ASSERT_NE(converter, nullptr);

        const auto sample = [&]() {
            const auto converterState = converter->getStates();
            EXPECT_GE(converterState.size(), 2U);
            return FaultSample{.voltage = bus->getVoltage(),
                               .activePower = converterState[0],
                               .reactivePower = converterState[1]};
        };
        const auto checkStateBounds = [&]() {
            const auto checkState = [](const std::vector<double>& state) {
                for (const double value : state) {
                    EXPECT_TRUE(std::isfinite(value));
                    EXPECT_LT(std::abs(value), 5.0);
                }
            };
            if (solverMethod == "dae") {
                checkState(simulation->getState(cDaeSolverMode));
            } else {
                checkState(simulation->getState(simulation->getSolverMode("dynalg")));
                checkState(simulation->getState(simulation->getSolverMode("dyndiff")));
            }
        };

        ASSERT_EQ(simulation->run(0.05), 0);
        const auto preFault = sample();
        ASSERT_TRUE(std::isfinite(preFault.voltage));
        ASSERT_TRUE(std::isfinite(preFault.activePower));
        ASSERT_TRUE(std::isfinite(preFault.reactivePower));
        checkStateBounds();

        FaultSample duringFault{};
        FaultSample recovery{};
        FaultSample settled{};
        for (int step = 11; step <= 180; ++step) {
            const double time = 0.005 * static_cast<double>(step);
            ASSERT_EQ(simulation->run(time), 0) << "at t=" << time;
            const auto values = sample();
            EXPECT_TRUE(std::isfinite(values.voltage));
            EXPECT_TRUE(std::isfinite(values.activePower));
            EXPECT_TRUE(std::isfinite(values.reactivePower));
            EXPECT_GT(values.voltage, 0.5) << "at t=" << time;
            EXPECT_LT(values.voltage, 1.2) << "at t=" << time;
            EXPECT_LT(std::abs(values.activePower), 2.0) << "at t=" << time;
            EXPECT_LT(std::abs(values.reactivePower), 2.0) << "at t=" << time;
            checkStateBounds();

            if (step == 30) {
                duringFault = values;
            } else if (step == 80) {
                recovery = values;
            } else if (step == 180) {
                settled = values;
            }
        }

        EXPECT_LT(duringFault.voltage, preFault.voltage - 0.1);
        EXPECT_LT(duringFault.activePower, preFault.activePower - 0.05);
        EXPECT_GT(duringFault.reactivePower, preFault.reactivePower + 0.05);
        for (const auto& values : {recovery, settled}) {
            EXPECT_NEAR(values.voltage, preFault.voltage, 0.02);
            EXPECT_NEAR(values.activePower, preFault.activePower, 0.02);
            EXPECT_NEAR(values.reactivePower, preFault.reactivePower, 0.02);
        }
    }
}
