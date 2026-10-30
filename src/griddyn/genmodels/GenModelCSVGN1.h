/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GenModel.h"
#include <array>
#include <string>

namespace griddyn::genmodels {
/**
 * @brief PSS/E CSVGN1 SCR controlled static var source model.
 *
 * CSVGN1 is attached through a PSS/E machine record, but represents a shunt
 * compensator rather than a synchronous machine. It has no rotor, mechanical
 * power, or field dynamics. Its states are the two regulator lags and the
 * thyristor reactor command.
 *
 * The implementation realizes the two lead-lag blocks and reactor lag in the
 * CSVGN1 block diagram. With voltage and states in per unit on the machine
 * base, the equations are
 * \f[
 * G_r(s)=K\frac{(1+sT_1)(1+sT_2)}{(1+sT_3)(1+sT_4)},
 * \qquad G_T(s)=\frac{1}{1+sT_5},
 * \f]
 * with regulator and reactor limits applied as follows:
 * \f[
 * \begin{aligned}
 * e &= V - V_{ref}, & T_3\dot{x}_1 &= e-x_1,\\
 * u_1 &= \frac{T_1}{T_3}e+\left(1-\frac{T_1}{T_3}\right)x_1,
 *   & T_2\dot{x}_2 &= u-x_2,\\
 * u_2 &= K\frac{T_2}{T_4}u_1+\left(1-\frac{T_2}{T_4}\right)x_2,
 *   & u &= \operatorname{clip}(u_2, V_{min}, V_{max}),\\
 * r_B &= \frac{u-B}{T_5}, & \dot{B} &= r_B
 * \end{aligned}
 * \f]
 * These equations apply when T2, T4, and T5 are positive. With T2 = 0,
 * regulator 2 is a non-windup limited lag of K*u1 with time constant T4.
 * With T2 = T4 = 0 it is an algebraic limited gain; with T5 = 0 the reactor
 * command is algebraic too. T3 must be positive, and a positive T2 needs T4 > 0.
 * The reactor derivative is held at zero when it would move outward through
 * either limit, \f$B_{min}=R_{min}/M_{base}\f$ or \f$B_{max}=1\f$. The
 * compensator's capacitive injection is
 * \f[
 * Q_{svc}=\left(\frac{C_{base}}{M_{base}}-
 *   \operatorname{clip}(B,B_{min},1)\right)V^2.
 * \f]
 * `GenModel` uses the opposite sign for reactive output, so the model returns
 * \f$-Q_{svc}\f$. `RMIN` and `CBASE` are in Mvar; `MBASE` is in MVA; the
 * voltage, control, and reactor quantities are per unit. The block diagram
 * also shows a supplementary `VOTHSG` input; this implementation currently
 * assumes that input is zero.
 *
 * @par Equation reference
 * The published source is a block diagram, not a numbered equation set:
 * PowerWorld, *PowerWorld Simulator 17 Block Diagrams*, p. 256,
 * “Machine Model CSVGN1” (identified as supported by PSS/E):
 * https://www.powerworld.com/files/Block-Diagrams-17.pdf
 * The limited lead-lag realization and zero-time-constant branches also follow
 * OpenIPSL CSVGN1 at commit 9a595ebf (2026-10-25).
 *
 * @see \ref csvgn1_model_equations "CSVGN1 model equations" for parameter
 * ordering, initialization equations, sign conventions, and implementation limits.
 */
class GenModelCSVGN1: public GenModel {
  public:
    /**
     * @param objName GridDyn object name for this model instance.
     */
    explicit GenModelCSVGN1(const std::string& objName = "genModelCSVGN1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;

    /**
     * @brief Set a CSVGN1 parameter from its textual form.
     * @param param Parameter name.
     * @param val Text representation of the parameter value.
     */
    void set(std::string_view param, std::string_view val) override;
    /**
     * @brief Set a CSVGN1 parameter from a numeric value.
     * @param param Parameter name.
     * @param val Numeric parameter value.
     * @param unitType Units associated with val.
     */
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    stringVec localStateNames() const override;
    void derivative(const IOdata& inputs,
                    const StateData& stateDataValue,
                    double deriv[],
                    const SolverMode& sMode) override;
    void residual(const IOdata& inputs,
                  const StateData& stateDataValue,
                  double resid[],
                  const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateDataValue,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    IOdata getOutputs(const IOdata& inputs,
                      const StateData& stateDataValue,
                      const SolverMode& sMode) const override;

    using GenModel::getOutput;
    double getOutput(const IOdata& inputs,
                     const StateData& stateDataValue,
                     const SolverMode& sMode,
                     index_t outNum = 0) const override;
    double getOutput(index_t outNum = 0) const override;

    void jacobianElements(const IOdata& inputs,
                          const StateData& stateDataValue,
                          MatrixData<double>& matrixDataValue,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const SolverMode& sMode) override;
    count_t outputDependencyCount(index_t outNum, const SolverMode& sMode) const override;
    void ioPartialDerivatives(const IOdata& inputs,
                              const StateData& stateDataValue,
                              MatrixData<double>& matrixDataValue,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode) override;

  private:
    struct RegulatorSignals {
        double error = 0.0;
        double firstOutput = 0.0;
        double limitedOutput = 0.0;
        double rate = 0.0;
        // Derivatives with respect to regulator1, regulator2, and voltage.
        std::array<double, 3> outputDerivatives{};
        std::array<double, 3> rateDerivatives{};
    };

    std::array<double, 3> stateValues(const double alg[], const double diff[]) const;
    std::array<index_t, 3> stateLocations(index_t algOffset, index_t diffOffset) const;
    RegulatorSignals getRegulatorSignals(double voltage, const std::array<double, 3>& state) const;
    double voltageReference() const;
    double reactorMinimumPU() const;
    double reactiveOutput(double voltage, double reactorCommand) const;

    double K = 1.0;
    double T1 = 0.0;
    double T2 = 0.0;
    double T3 = 0.1;
    double T4 = 0.1;
    double T5 = 0.05;
    double RMIN = 0.0;  //!< minimum reactor loading [Mvar]
    double VMAX = 1.0;
    double VMIN = 0.0;
    double CBASE = 0.0;  //!< parallel capacitor size [Mvar]
    double m_voltageReference = 1.0;
};
}  // namespace griddyn::genmodels
