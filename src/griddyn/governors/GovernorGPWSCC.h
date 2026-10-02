/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <array>
#include <string>

namespace griddyn::governors {
/** PSLF GPWSCC PID governor-turbine model.
 *
 * GPWSCC is a gate-based PID governor with an intentional speed deadband,
 * filtered electrical-power droop feedback, a rate- and position-limited
 * gate servo, a six-point gate-to-power curve, and the turbine transfer
 * function @f$(1+s A_{turb}T_{turb})/(1+s B_{turb}T_{turb})@f$.
 *
 * The PSLF model document defines the internal gate and turbine powers on
 * MWCap.  GridDyn governor signals are on the generator machine base, so the
 * implementation converts at the model boundary with MWCap/MVABase.  The
 * input setpoint is used as a change from the initialized dispatch; Paux is
 * initialized to balance electrical-power feedback, avoiding a startup step.
 *
 * The published drawing does not define the exact memory convention for db1,
 * eps, or db2.  This realization uses continuous symmetric dead zones.  eps
 * extends db1's conservative release band and db2 is a gate-position dead
 * zone.  An all-zero six-point curve, used by the supplied PSLF case, is
 * treated as the identity curve.
 *
 * Source: "Governor Model: GPWSCC", Powertech Labs PSLF model documentation.
 */
class GovernorGPWSCC final: public Governor {
  public:
    explicit GovernorGPWSCC(const std::string& objName = "govGPWSCC_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
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
    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

  private:
    struct DeadbandEvaluation {
        double value;
        double derivative;
    };
    struct CurveEvaluation {
        double value;
        double slope;
    };
    struct Signals {
        double error;
        double controllerOutput;
        double gateRate;
        double gatePower;
        double turbinePower;
        double mechanicalPower;
        double speedDerivative;
        double gatePowerDerivative;
        bool integralBlocked;
        bool gateRateActive;
    };

    model_parameter MWCap = 100.0;
    model_parameter MVABase = 100.0;
    model_parameter R = 0.05;
    model_parameter Td = 0.05;
    model_parameter Tf = 0.05;
    model_parameter Tp = 0.1;
    model_parameter Velopen = 0.1;
    model_parameter Velclose = -0.1;
    model_parameter Kp = 1.0;
    model_parameter Kd = 0.0;
    model_parameter Ki = 0.0;
    model_parameter Kg = 1.0;
    model_parameter Tturb = 0.0;
    model_parameter Aturb = 0.0;
    model_parameter Bturb = 1.0;
    model_parameter Tt = 0.0;
    model_parameter db1 = 0.0;
    model_parameter eps = 0.0;
    model_parameter db2 = 0.0;
    model_parameter gateMaximum = 1.0;
    model_parameter gateMinimum = 0.0;
    model_parameter Paux = 0.0;
    std::array<model_parameter, 6> curveGate{};
    std::array<model_parameter, 6> curvePower{};
    double initializedInputReference = 0.0;

    double powerScale() const;
    bool hasIdentityCurve() const;
    bool hasUsableCurve() const;
    std::size_t lastCurvePoint() const;
    static DeadbandEvaluation evaluateDeadband(double value, double width);
    CurveEvaluation evaluateCurve(double gate) const;
    double inverseCurve(double power) const;
    double gateOutput(double rawGate) const;
    double inverseGateOutput(double gate) const;
    void updateOutputLimits();
    Signals evaluate(const IOdata& inputs, const double state[]) const;
    static double mechanicalPower(const Signals& signals);
};
}  // namespace griddyn::governors
