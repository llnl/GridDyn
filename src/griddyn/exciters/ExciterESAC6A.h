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
/** PSS/E ESAC6A IEEE Type AC6A excitation system. */
class ExciterESAC6A final: public Exciter {
  public:
    explicit ExciterESAC6A(const std::string& objName = "exciterESAC6A_#");
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
    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  private:
    static constexpr index_t maximumStates = 5;
    struct StateLayout {
        index_t sensedVoltage = kInvalidLocation;
        index_t regulator = kInvalidLocation;
        index_t leadLag = kInvalidLocation;
        index_t exciter = kInvalidLocation;
        index_t feedback = kInvalidLocation;
        index_t count = 0;
    };
    struct ModelEvaluation {
        double fieldOutput = 0.0;
        double exciterLimitDrive = 0.0;
        std::array<double, maximumStates> fieldStateDerivatives{};
        std::array<double, exciterInputCount> fieldInputDerivatives{};
        std::array<double, maximumStates> rates{};
        std::array<std::array<double, maximumStates>, maximumStates> rateStateDerivatives{};
        std::array<std::array<double, exciterInputCount>, maximumStates> rateInputDerivatives{};
    };

    [[nodiscard]] StateLayout stateLayout() const;
    [[nodiscard]] ModelEvaluation evaluateModel(const IOdata& inputs, const double state[]) const;
    bool updateExciterLimitFlag(const IOdata& inputs, const double state[]);

    enum ESAC6AFlags {
        EXCITER_AT_LOWER_LIMIT = OBJECT_FLAG5,
    };

    model_parameter Tr = 0.0;
    model_parameter Tk = 0.0;
    model_parameter Tb = 0.0;
    model_parameter Tc = 0.0;
    model_parameter Vamax = 1.0;
    model_parameter Vamin = -1.0;
    model_parameter Te = 0.5;
    model_parameter Vfelim = 0.0;
    model_parameter Kh = 1.0;
    model_parameter Vhmax = 1.0;
    model_parameter Th = 0.0;
    model_parameter Tj = 0.0;
    model_parameter Kc = 0.0;
    model_parameter Kd = 0.0;
    model_parameter Ke = 0.1;
    model_parameter E1 = 2.8;
    model_parameter Se1 = 0.08;
    model_parameter E2 = 3.7;
    model_parameter Se2 = 0.33;
    bool speedMultiplier = false;
    utilities::Saturation saturation{utilities::Saturation::SaturationType::CUTOFF_QUADRATIC};
};
}  // namespace griddyn::exciters
