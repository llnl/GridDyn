/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/voltagecompensators/VoltageCompensatorIeeeVC.h"
#include "utilities/MatrixDataSparse.hpp"
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace griddyn;

TEST(VoltageCompensatorTests, IeeeVCMatchesComplexVoltageEquation)
{
    auto object = std::unique_ptr<CoreObject>(
        CoreObjectFactory::instance()->createObject("voltagecompensator", "ieeevc"));
    auto* model = dynamic_cast<voltagecompensators::VoltageCompensatorIeeeVC*>(object.get());
    ASSERT_NE(model, nullptr);
    model->set("rc", 0.02);
    model->set("xc", 0.10);
    const IOdata inputs{1.0, 0.2, -0.1, 0.9, 0.4, 0.0, 0.0, 0.0};
    model->dynInitializeA(0.0, 0);
    IOdata fieldSet;
    model->dynInitializeB(inputs, {}, fieldSet);

    const double expected =
        std::hypot(0.9 + (0.02 * 0.2) - (0.10 * -0.1), 0.4 + (0.02 * -0.1) + (0.10 * 0.2));
    EXPECT_NEAR(model->getOutput(), expected, 1e-14);
    EXPECT_NEAR(fieldSet[0], expected, 1e-14);
    std::vector<double> residual(1, 0.0);
    model->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.0, 1e-14);
}

TEST(VoltageCompensatorTests, IeeeVCAnalyticJacobianMatchesFiniteDifference)
{
    voltagecompensators::VoltageCompensatorIeeeVC model;
    model.set("rc", 0.02);
    model.set("xc", 0.10);
    const IOdata inputs{1.0, 0.2, -0.1, 0.9, 0.4, 0.0, 0.0, 0.0};
    model.dynInitializeA(0.0, 0);
    IOdata fieldSet;
    model.dynInitializeB(inputs, {}, fieldSet);
    model.setOffset(0, cDaeSolverMode);
    std::vector<double> state{fieldSet[0] + 0.01};
    std::vector<double> stateDerivative(1, 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = 1;
    MatrixDataSparse<double> jacobian;
    const IOlocs inputLocs{20, 21, 22, 23, 24, 25, 26, 27};
    model.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);

    const auto residual = [&model, &state](const IOdata& values) {
        std::vector<double> result(1, 0.0);
        std::vector<double> derivative(1, 0.0);
        StateData trialData(0.0, state.data(), derivative.data());
        trialData.stateSize = 1;
        model.residual(values, trialData, result.data(), cDaeSolverMode);
        return result[0];
    };
    constexpr double step = 1e-7;
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        auto shifted = inputs;
        shifted[input] += step;
        EXPECT_NEAR(jacobian.at(0, inputLocs[input]),
                    (residual(shifted) - residual(inputs)) / step,
                    1e-5)
            << "input " << input;
    }
}
