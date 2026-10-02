/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RenewableComponent.h"
#include <array>
#include <span>
#include <string>
#include <vector>

namespace griddyn {

/**
 * PSLF EPCGEN controlled-voltage-source converter.
 *
 * The original ASU model has ten continuous states.  The PSLF epsbes.p
 * version-7 record supplies the coupling impedance, frequency response,
 * voltage breakpoint, current limit, active-power limits, and active-power
 * reference.  The remaining controller constants are the model defaults
 * used by the supplied EPCGEN equations. The primary mathematical reference
 * is Ramasubramanian, "Impact of Converter Interfaced Generation and Load on
 * Grid Performance," ASU dissertation (2017), Chapter 4, sections 4.3.1 and
 * 4.3.3, with the EPCL implementation reproduced in Appendix D. See
 * docs/developer-guide/epcgen-reference.md for the source artifact and page
 * mapping.
 */
class EPCGEN final: public TerminalElectricalModel {
  public:
    explicit EPCGEN(const std::string& name = "EPCGEN_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
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
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    IOdata getOutputs(const IOdata& inputs,
                      const StateData& stateData,
                      const SolverMode& sMode) const override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
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

  private:
    enum State : index_t {
        qIntegrator,
        voltageFilter,
        governor,
        leadLag,
        reactiveCurrent,
        activeCurrent,
        internalD,
        internalQ,
        activeCorrection,
        reactiveCorrection,
        stateCount
    };

    struct Evaluation {
        std::array<double, 2> power{};
        std::array<double, stateCount> rates{};
    };

    double rsrc = 0.0;
    double xsrc = 0.0;
    double tfrq = 0.1;
    double ofpdb = 60.1;
    double ufpdb = 59.9;
    double ofpdroop = 1.67;
    double ufpdroop = 1.67;
    double vbreak = 0.7;
    double imax = 1.0;
    double pmax = 1.0;
    double pmin = -1.0;
    double pref = 0.0;

    // Constants from the supplied ASU EPCGEN equations.  The epsbes.p v7
    // record does not expose these values as named fields.
    double kp = 1.0;
    double ki = 1.0;
    double kip = 1.0;
    double kiq = 1.0;
    double rq = 0.0;
    double tq = 0.02;
    double tg = 0.1;
    double t1 = 0.0;
    double t2 = 0.1;
    double td = 0.1;
    double ted = 0.1;
    double teq = 0.1;
    double qmax = 1.0;
    double qmin = -1.0;
    double voltageTripDelta = 0.2;
    double voltageTripTime = 0.1;

    double initialVoltageReference = 1.0;
    CoreTime prevTime = 0.0;
    bool tripped = false;

    Evaluation evaluate(const IOdata& inputs, const double state[]) const;
    double frequencyHz(const IOdata& inputs) const;
    double reactiveLimit(double voltage, double activePower) const;
    void copyParametersTo(EPCGEN* target) const;
};

}  // namespace griddyn
