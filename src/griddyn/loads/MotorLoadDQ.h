/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include <array>
#include <string>
#include <string_view>

namespace griddyn::loads {

/** Balanced single-cage induction motor with stator and rotor flux dynamics.
 *
 * States are [psi_sd, psi_sq, psi_rd, psi_rq, speed] in the fixed system
 * synchronous frame. Fluxes and circuit data are per unit on the motor MVA
 * base; speed is rotor electrical speed / system base electrical frequency.
 * The symmetric stator/rotor inductance matrix determines currents. Positive
 * terminal P/Q denotes consumption. See docs/developer-guide/motor-dq-model.md
 * for equations, reference conventions, initialization, and source limits.
 */
class MotorLoadDQ: public GridLoad {
  public:
    explicit MotorLoadDQ(const std::string& objName = "motor_dq_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    StateSizes localStateSizes(const SolverMode& sMode) const override;
    count_t localJacobianCount(const SolverMode& sMode) const override;
    stringVec localStateNames() const override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    void ioPartialDerivatives(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode) override;
    count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;

    double getRealPower(const IOdata& inputs,
                        const StateData& stateData,
                        const SolverMode& sMode) const override;
    double getReactivePower(const IOdata& inputs,
                            const StateData& stateData,
                            const SolverMode& sMode) const override;
    double getRealPower(double voltage) const override;
    double getReactivePower(double voltage) const override;
    double getRealPower() const override;
    double getReactivePower() const override;

  protected:
    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;

  private:
    using MotorState = std::array<double, 5>;
    /** [stator d, stator q, rotor d, rotor q] currents on the motor base. */
    std::array<double, 4> currents(const double* state) const;
    MotorState rates(const IOdata& inputs, const double* state) const;
    MotorState electricalEquilibrium(double voltage, double angle, double slip) const;
    void validateParameters() const;
    void initializeRunningPoint(double voltage, double angle);
    double torqueShape(double speed) const;
    double electricalTorque(const double* state) const;

    double mRs = 0.01;
    double mRr = 0.02;
    double mXls = 0.10;
    double mXlr = 0.10;
    double mXm = 3.0;
    double mInertia = 0.5;
    double mRating = 0.0;  //!< MVA; zero chooses allocated MW / load_factor.
    double mLoadFactor = 0.8;
    double mScale = 1.0;  //!< Motor MVA / system MVA, fixed at initialization.
    double mTorqueConstant = 0.0;
    double mTorqueLinear = 0.0;
    double mTorqueQuadratic = 1.0;
    double mDamping = 0.0;
    double mLoadTorque = 1.0;
    double mInitialSpeed = 0.0;
    bool mTorqueSpecified = false;
    bool mStarting = false;
};

}  // namespace griddyn::loads
