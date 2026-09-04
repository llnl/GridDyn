/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn::exciters {
/** PSS/E SCRX bus-fed or solid-fed static excitation system. */
class ExciterSCRX final: public Exciter {
  public:
    explicit ExciterSCRX(const std::string& objName = "exciterSCRX_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;
    ChangeCode rootCheck(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode,
                         CheckLevel level) override;
    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  private:
    static constexpr index_t maximumStates = 2;
    struct StateLayout {
        index_t leadLag = kInvalidLocation;
        index_t amplifier = kInvalidLocation;
        index_t count = 0;
    };
    struct ModelEvaluation {
        double fieldOutput = 0.0;
        double amplifierLimitDrive = 0.0;
        std::array<double, maximumStates> fieldStateDerivatives{};
        std::array<double, exciterInputCount> fieldInputDerivatives{};
        std::array<double, maximumStates> rates{};
        std::array<std::array<double, maximumStates>, maximumStates> rateStateDerivatives{};
        std::array<std::array<double, exciterInputCount>, maximumStates> rateInputDerivatives{};
    };

    [[nodiscard]] StateLayout stateLayout() const;
    [[nodiscard]] ModelEvaluation evaluateModel(const IOdata& inputs, const double state[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);

    enum SCRXFlags {
        AMPLIFIER_LIMITED = OBJECT_FLAG5,
        AMPLIFIER_LIMIT_HIGH = OBJECT_FLAG6,
    };

    model_parameter TaOverTb = 0.1;
    model_parameter Tb = 1.0;
    model_parameter Te = 0.005;
    model_parameter rCrFd = 10.0;
    bool solidFed = false;
};
}  // namespace griddyn::exciters
