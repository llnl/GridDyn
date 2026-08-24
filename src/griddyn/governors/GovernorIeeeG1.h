/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn::governors {
/** IEEE Type 1 speed-governing model (PSS/E IEEEG1).
 *
 * This class follows frozen ANDES v2.0.0
 * `andes/models/governor/ieeeg1.py`. It retains the published block structure
 * while eliminating algebraic variables internal to ordinary lead-lag and lag
 * blocks. A zero T1 or turbine-stage time constant is therefore an exact
 * bypass and owns no differential state.
 *
 * With speed deviation @f$w_d=1-\omega@f$, lead-lag state @f$x_g@f$,
 * valve position @f$p_v@f$, and turbine-stage outputs @f$p_4,\ldots,p_7@f$,
 *
 * @f[
 * T_1\dot x_g=w_d-x_g,\qquad
 * y_g=K\left[x_g+\frac{T_2}{T_1}(w_d-x_g)\right],
 * @f]
 * @f[
 * v_s=\frac{y_g+P_0-p_v}{T_3},\quad
 * \dot p_v=\operatorname{AW}_{[P_{MIN},P_{MAX}]}
 *             (\operatorname{limit}_{[U_C,U_O]}(v_s)),
 * @f]
 * @f[
 * T_i\dot p_i=p_{i-1}-p_i,\quad i=4,\ldots,7.
 * @f]
 *
 * The two outputs are the normalized odd- and even-coefficient sums:
 * @f$P_{HP}=K_{1n}p_4+K_{3n}p_5+K_{5n}p_6+K_{7n}p_7@f$ and
 * @f$P_{LP}=K_{2n}p_4+K_{4n}p_5+K_{6n}p_6+K_{8n}p_7@f$.
 */
class GovernorIeeeG1: public Governor {
  public:
    enum IEEEG1Flags {
        RATE_LIMITED = OBJECT_FLAG9,
        RATE_LIMIT_HIGH = OBJECT_FLAG10,
    };

    static constexpr index_t hpOutput = 0;
    static constexpr index_t lpOutput = 1;

  protected:
    model_parameter Uo = 0.1;
    model_parameter Uc = -0.1;
    std::array<model_parameter, 4> turbineTime{{0.4, 8.0, 0.5, 0.05}};
    std::array<model_parameter, 8> powerFraction{{0.5, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0}};

    index_t leadLagState = kInvalidLocation;
    index_t valveState = kInvalidLocation;
    std::array<index_t, 4> turbineState{
        {kInvalidLocation, kInvalidLocation, kInvalidLocation, kInvalidLocation}};
    std::array<double, 2> initializationTarget{{0.0, 0.0}};
    std::array<bool, 2> initializationTargetSet{{false, false}};
    bool initializationActive = false;

  public:
    explicit GovernorIeeeG1(const std::string& objName = "govIeeeG1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    bool setOutputInitializationTarget(index_t outputIndex, double target) override;

    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

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
    const std::vector<stringVec>& outputNames() const override;

  private:
    std::array<double, 8> normalizedFractions() const;
    double leadLagOutput(const IOdata& inputs, const double diffState[]) const;
    double unlimitedValveRate(const IOdata& inputs, const double diffState[]) const;
    double limitedValveRate(const IOdata& inputs, const double diffState[]) const;
    std::array<double, 4> turbineOutputs(const double diffState[]) const;
    std::array<double, 2> powerOutputs(const double diffState[]) const;
    int rateLimitStatus(const IOdata& inputs, const double diffState[]) const;
    int valveLimitStatus(const IOdata& inputs, const double diffState[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double diffState[]);
    void initializeStatesFromTargets();
};
}  // namespace griddyn::governors
