/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include "utilities/Saturation.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn::exciters {
/** IEEE AC5A excitation system using the ANDES ESAC5A state equations. */
class ExciterESAC5A final: public Exciter {
  public:
    explicit ExciterESAC5A(const std::string& objName = "exciterESAC5A_#");
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
    using Exciter::set;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  private:
    static constexpr index_t maximumStates = 5;
    struct Layout {
        index_t sensed = kInvalidLocation;
        index_t regulator = kInvalidLocation;
        index_t leadLag = kInvalidLocation;
        index_t washout = kInvalidLocation;
        index_t field = kInvalidLocation;
        index_t count = 0;
    };
    struct Evaluation {
        std::array<double, maximumStates> rates{};
        std::array<std::array<double, maximumStates>, maximumStates> stateJac{};
        std::array<std::array<double, exciterInputCount>, maximumStates> inputJac{};
        double regulatorDrive = 0.0;
    };
    [[nodiscard]] Layout layout() const;
    [[nodiscard]] Evaluation evaluate(const IOdata& inputs, const double state[]) const;
    bool updateRegulatorLimit(const IOdata& inputs, const double state[]);

    model_parameter Tr = 0.01;
    model_parameter Te = 0.8;
    model_parameter Kf = 0.03;
    model_parameter Tf1 = 1.0;
    model_parameter Tf2 = 0.8;
    model_parameter Tf3 = 1.0;
    model_parameter Ke = 1.0;
    model_parameter E1 = 0.0;
    model_parameter Se1 = 0.0;
    model_parameter E2 = 1.0;
    model_parameter Se2 = 1.0;
    utilities::Saturation saturation{utilities::Saturation::SaturationType::CUTOFF_QUADRATIC};
};
}  // namespace griddyn::exciters
