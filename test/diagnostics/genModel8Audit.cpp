/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Reports equation diagnostics without changing the model. Exit status indicates
// execution success; the printed measurements must be assessed independently.
#include "griddyn/GridPrimary.h"
#include "griddyn/genmodels/GenModel8.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace griddyn;

class Probe: public genmodels::GenModel8 {
  public:
    double frequency() const { return systemBaseFrequency; }
};

void configure(Probe& model, double taa)
{
    const std::array<std::pair<const char*, double>, 16> parameters{{{"h", 4.0},
                                                                     {"d", 0.04},
                                                                     {"r", 0.001},
                                                                     {"xl", 0.12},
                                                                     {"xd", 1.60},
                                                                     {"xq", 1.55},
                                                                     {"xdp", 0.32},
                                                                     {"xqp", 0.55},
                                                                     {"xdpp", 0.22},
                                                                     {"xqpp", 0.25},
                                                                     {"tdop", 5.0},
                                                                     {"tqop", 0.70},
                                                                     {"tdopp", 0.05},
                                                                     {"tqopp", 0.10},
                                                                     {"taa", taa},
                                                                     {"s10", 0.0}}};
    for (const auto& parameter : parameters) {
        model.set(parameter.first, parameter.second);
    }
    model.dynInitializeA(0.0, 0);
}

double maxAbs(const std::vector<double>& values)
{
    double maximum = 0.0;
    for (const double value : values)
        maximum = std::max(maximum, std::abs(value));
    return maximum;
}

int main()
{
    std::cout << std::setprecision(12);
    index_t sequence = 100;
    for (const double taa : {0.0, 0.02}) {
        Probe model;
        configure(model, taa);
        IOdata inputs{0.97, 0.17, 0.0, 0.0};
        IOdata fields(4, 0.0);
        model.dynInitializeB(inputs, {0.8, 0.25}, fields);
        inputs[2] = fields[2];
        inputs[3] = fields[3];
        model.loadStateSizes(cDaeSolverMode);
        model.setOffset(0, cDaeSolverMode);
        auto state = model.getStates();
        if (state.size() != 10) {
            std::cerr << "Expected two algebraic and eight differential states\n";
            return 2;
        }
        std::vector<double> rates(state.size(), 0.0);
        auto residual = [&](const IOdata& terminal,
                            const std::vector<double>& point,
                            const std::vector<double>& derivatives) {
            std::vector<double> result(point.size(), 0.0);
            StateData data(0.0, point.data(), derivatives.data(), ++sequence);
            data.stateSize = static_cast<count_t>(point.size());
            model.residual(terminal, data, result.data(), cDaeSolverMode);
            return result;
        };
        auto initialResidual = residual(inputs, state, rates);
        std::cout << "taa " << taa << " initialized_residual_max " << maxAbs(initialResidual)
                  << " eqp_rate " << initialResidual[5] << " eqpp_rate " << initialResidual[7]
                  << " field_voltage " << fields[2] << '\n';
        if (taa != 0.0) {
            auto correctedState = state;
            correctedState[5] -= 2.0 * taa / 5.0 * fields[2];
            std::cout << "taa_sign_only_probe_residual_max "
                      << maxAbs(residual(inputs, correctedState, rates)) << '\n';
        }

        // The next point deliberately moves away from equilibrium.
        state[0] += 0.03;
        state[1] -= 0.02;
        state[2] += 0.07;
        state[3] = 0.91;
        state[4] += 0.03;
        state[5] -= 0.04;
        state[6] -= 0.02;
        state[7] += 0.01;
        state[8] += 0.05;
        state[9] -= 0.06;
        for (std::size_t i = 2; i < rates.size(); ++i)
            rates[i] = 0.003 * i;
        StateData data(0.0, state.data(), rates.data(), ++sequence);
        data.cj = 2.3;
        data.stateSize = static_cast<count_t>(state.size());
        IOlocs inputLocations{10, 11, 12, 13};
        MatrixDataSparse<double> jacobian;
        model.jacobianElements(inputs, data, jacobian, inputLocations, cDaeSolverMode);
        jacobian.compact();
        MatrixDataSparse<double> outputJacobian;
        model.outputPartialDerivatives(inputs, data, outputJacobian, cDaeSolverMode);
        model.ioPartialDerivatives(inputs, data, outputJacobian, inputLocations, cDaeSolverMode);
        outputJacobian.compact();
        constexpr double step = 1e-6;
        double error = 0.0;
        double outputError = 0.0;
        for (std::size_t column = 0; column < 14; ++column) {
            auto plusState = state;
            auto minusState = state;
            auto plusInputs = inputs;
            auto minusInputs = inputs;
            auto plusRates = rates;
            auto minusRates = rates;
            if (column < 10) {
                plusState[column] += step;
                minusState[column] -= step;
                if (column >= 2) {
                    plusRates[column] += data.cj * step;
                    minusRates[column] -= data.cj * step;
                }
            } else {
                plusInputs[column - 10] += step;
                minusInputs[column - 10] -= step;
            }
            const auto plus = residual(plusInputs, plusState, plusRates);
            const auto minus = residual(minusInputs, minusState, minusRates);
            for (std::size_t row = 0; row < 10; ++row) {
                error = std::max(error,
                                 std::abs(jacobian.at(static_cast<index_t>(row),
                                                      static_cast<index_t>(column)) -
                                          (plus[row] - minus[row]) / (2.0 * step)));
            }
            const StateData plusData(0.0, plusState.data(), plusRates.data(), ++sequence);
            const StateData minusData(0.0, minusState.data(), minusRates.data(), ++sequence);
            for (const index_t output : {POUT_LOCATION, QOUT_LOCATION}) {
                const double plusOutput =
                    model.getOutput(plusInputs, plusData, cDaeSolverMode, output);
                const double minusOutput =
                    model.getOutput(minusInputs, minusData, cDaeSolverMode, output);
                outputError =
                    std::max(outputError,
                             std::abs(outputJacobian.at(output, static_cast<index_t>(column)) -
                                      (plusOutput - minusOutput) / (2.0 * step)));
            }
        }
        std::cout << "state_and_input_jacobian_max_error " << error << '\n';
        std::cout << "power_output_jacobian_max_error " << outputError << '\n';

        // Verify the stator power identity at that non-equilibrium point.
        const auto value = residual(inputs, state, std::vector<double>(10, 0.0));
        const double vd = -inputs[0] * std::sin(state[2] - inputs[1]);
        const double vq = inputs[0] * std::cos(state[2] - inputs[1]);
        const double power = vd * state[0] + vq * state[1];
        const double copper = 0.001 * (state[0] * state[0] + state[1] * state[1]);
        const double torque = state[8] * state[1] - state[9] * state[0];
        const double fluxTerm = (state[0] * value[8] + state[1] * value[9]) / model.frequency();
        std::cout << "stator_power_identity_error " << power + copper - state[3] * torque - fluxTerm
                  << '\n';

        auto updateCurrents = [&](const IOdata& terminal, const std::vector<double>& point) {
            auto result = point;
            StateData updateData(0.0, point.data(), rates.data(), ++sequence);
            model.algebraicUpdate(terminal, updateData, result.data(), cDaeSolverMode, 1.0);
            return result;
        };
        const auto baseline = updateCurrents(inputs, state);
        auto changedFlux = state;
        changedFlux[8] += 0.1;
        changedFlux[9] -= 0.1;
        const auto fluxResponse = updateCurrents(inputs, changedFlux);
        auto sagInputs = inputs;
        sagInputs[0] *= 0.9;
        const auto voltageResponse = updateCurrents(sagInputs, state);
        std::cout << "flux_perturbation_current_change "
                  << std::hypot(fluxResponse[0] - baseline[0], fluxResponse[1] - baseline[1])
                  << " voltage_step_current_change "
                  << std::hypot(voltageResponse[0] - baseline[0], voltageResponse[1] - baseline[1])
                  << '\n';
    }
    Probe singular;
    configure(singular, 0.0);
    singular.set("xdp", 0.12);  // Exactly the leakage reactance: drat denominator is zero.
    IOdata singularFields(4, 0.0);
    singular.dynInitializeB({0.97, 0.17, 0.0, 0.0}, {0.8, 0.25}, singularFields);
    const auto singularState = singular.getStates();
    std::cout << "singular_reactance_initialization_nonfinite_states "
              << std::count_if(singularState.begin(),
                               singularState.end(),
                               [](double value) { return !std::isfinite(value); })
              << '\n';
}
