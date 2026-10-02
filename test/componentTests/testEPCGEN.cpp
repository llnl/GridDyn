/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactory.hpp"
#include "fileInput/fileInput.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/renewables/EPCGEN.h"
#include "utilities/MatrixDataSparse.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace griddyn;

namespace {

void setDissertationParameters(EPCGEN& model)
{
    // Coupling values used for the controlled-voltage-source examples in the
    // dissertation (Figure 5.59), with the 10 ms inner/PWM delays described
    // on printed page 51.
    model.set("rsrc", 0.0025);
    model.set("xsrc", 0.06);
    model.set("tr", 0.02);
    model.set("kp", 1.0);
    model.set("ki", 1.0);
    model.set("kip", 1.0);
    model.set("kiq", 1.0);
    model.set("rq", 0.0);
    model.set("tq", 0.01);
    model.set("tg", 0.01);
    model.set("t1", 0.0);
    model.set("t2", 0.01);
    model.set("td", 0.01);
    model.set("ted", 0.01);
    model.set("teq", 0.01);
    model.set("imax", 1.7);
    model.set("pmax", 1.7);
    model.set("pmin", -1.7);
    model.set("qmax", 1.0);
    model.set("qmin", -1.0);
    model.set("pref", 0.4);
}

}  // namespace

TEST(EPCGENReference, DissertationOperatingPointInitializes)
{
    auto object = std::unique_ptr<CoreObject>(
        CoreObjectFactory::instance()->createObject("renewable_model", "epcgen"));
    auto* model = dynamic_cast<EPCGEN*>(object.get());
    ASSERT_NE(model, nullptr);
    setDissertationParameters(*model);

    model->dynInitializeA(0.0, 0);
    IOdata fields;
    model->dynInitializeB({1.0, 1.0}, {0.4, 0.1}, fields);

    EXPECT_EQ(model->localStateNames().size(), 10U);
    EXPECT_EQ(model->getStates().size(), 12U);  // two algebraic P/Q entries plus ten states
    for (const auto value : model->getStates()) {
        EXPECT_TRUE(std::isfinite(value));
    }

    std::vector<double> residual(model->getStates().size());
    model->residual({1.0, 1.0}, emptyStateData, residual.data(), cLocalSolverMode);
    for (const auto value : residual) {
        EXPECT_NEAR(value, 0.0, 1.0e-10);
    }

    const auto output = model->getOutputs({1.0, 1.0}, emptyStateData, cLocalSolverMode);
    ASSERT_EQ(output.size(), 2U);
    EXPECT_NEAR(output[0], 0.4, 1.0e-12);
    EXPECT_NEAR(output[1], 0.1, 1.0e-12);
}

TEST(EPCGENReference, DaeJacobianMatchesFiniteDifference)
{
    EPCGEN model;
    setDissertationParameters(model);
    model.dynInitializeA(0.0, 0);
    IOdata fields;
    model.dynInitializeB({1.0, 1.0}, {0.4, 0.1}, fields);
    model.setOffset(0, cDaeSolverMode);

    std::vector<double> state(model.stateSize(cDaeSolverMode));
    std::vector<double> derivative(state.size());
    model.guessState(0.0, state.data(), derivative.data(), cDaeSolverMode);
    StateData stateData(0.0, state.data(), derivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());

    MatrixDataSparse<double> jacobian;
    model.jacobianElements({1.0, 1.0}, stateData, jacobian, {40, 41}, cDaeSolverMode);

    const auto residual = [&]() {
        std::vector<double> values(state.size());
        model.residual({1.0, 1.0}, stateData, values.data(), cDaeSolverMode);
        return values;
    };
    const auto base = residual();
    constexpr double perturbation = 1.0e-7;

    for (std::size_t column = 0; column < state.size(); ++column) {
        state[column] += perturbation;
        const auto shifted = residual();
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (shifted[row] - base[row]) / perturbation,
                        1.0e-4)
                << "row " << row << ", column " << column;
        }
        state[column] -= perturbation;
    }

    IOdata inputs{1.0, 1.0};
    for (std::size_t column = 0; column < inputs.size(); ++column) {
        inputs[column] += perturbation;
        const auto shifted = [&]() {
            std::vector<double> values(state.size());
            model.residual(inputs, stateData, values.data(), cDaeSolverMode);
            return values;
        }();
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row),
                                    static_cast<index_t>(40U + column)),
                        (shifted[row] - base[row]) / perturbation,
                        1.0e-4)
                << "input " << column << ", row " << row;
        }
        inputs[column] -= perturbation;
    }
}

TEST(EPCGENReference, NamedV7DydRecordAttaches)
{
    const auto epcPath = std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "IEEE_test_cases" /
        "IEEE 14 bus.epc";
    const auto dydPath = std::filesystem::temp_directory_path() / "griddyn_epcgen_v7.dyd";
    {
        std::ofstream output(dydPath);
        ASSERT_TRUE(output.is_open());
        output << "models\n"
                  "epcgen 3 \"Bus 3\" 138.0 \"1\" : #4 \"epsbes.p\" 7 "
                  "\"rsrc\" 0.0025 \"xsrc\" 0.06 \"tfrq\" 0.02 "
                  "\"ofpdb\" 60.1 \"ufpdb\" 59.9 \"ofpdroop\" 1.67 "
                  "\"ufpdroop\" 1.67 /\n"
                  "\"vbreak\" 0.70 \"imax\" 1.7 \"pmax\" 1.7 "
                  "\"pmin\" -1.7 \"pref\" 0.4\n";
    }

    auto simulation = std::make_unique<GridDynSimulation>();
    ASSERT_NO_THROW(loadFile(simulation, epcPath.string()));
    ASSERT_NO_THROW(loadFile(simulation, dydPath.string()));
    std::error_code removeError;
    std::filesystem::remove(dydPath, removeError);

    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 3));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* model = dynamic_cast<EPCGEN*>(generator->find("electrical"));
    ASSERT_NE(model, nullptr);
    EXPECT_DOUBLE_EQ(model->get("rsrc"), 0.0025);
    EXPECT_DOUBLE_EQ(model->get("xsrc"), 0.06);
    EXPECT_DOUBLE_EQ(model->get("tfrq"), 0.02);
    EXPECT_DOUBLE_EQ(model->get("imax"), 1.7);
    EXPECT_DOUBLE_EQ(model->get("pref"), 0.4);
}
