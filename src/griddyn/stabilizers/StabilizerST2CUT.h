/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Stabilizer.h"
#include <string>
#include <vector>

namespace griddyn::stabilizers {
/**
 * @brief PSS/E ST2CUT dual-input power-system stabilizer.
 *
 * This is the frozen ANDES 2.0.0 `andes/models/pss/st2cut.py` realization:
 * two transducer lags, their sum, a washout (or lag when T3 is zero), and
 * three lead-lag stages feeding the LSMIN/LSMAX output limiter.  The result
 * is gated to zero when terminal voltage is outside VCL/VCU plus the
 * initialized terminal-voltage reference.
 *
 * The GridDyn PSS signal contract exposes rotor speed, terminal voltage,
 * mechanical power, and electrical power.  It therefore supports local
 * MODE/MODE2 values 0 (disabled), 1 (rotor speed), 3 (electrical power),
 * 4 (accelerating power), and 5 (terminal voltage).  ANDES modes 2
 * (BusFreq) and 6 (voltage derivative), and nonzero remote BUSR/BUSR2,
 * require cross-bus measurement routing that GridDyn does not yet provide;
 * they are rejected at initialization rather than silently approximated.
 */
class StabilizerST2CUT: public Stabilizer {
  public:
    enum ST2CUTFlags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
        VOLTAGE_GATED = OBJECT_FLAG7,
    };

  protected:
    int mode1 = 1;
    int remoteBus1 = 0;
    int mode2 = 0;
    int remoteBus2 = 0;
    double K1 = 1.0;
    double K2 = 1.0;
    double T1 = 1.0;
    double T2 = 1.0;
    double T3 = 1.0;
    double T4 = 0.2;
    double T5 = 1.0;
    double T6 = 0.5;
    double T7 = 1.0;
    double T8 = 1.0;
    double T9 = 1.0;
    double T10 = 0.2;
    double Lsmax = 0.3;
    double Lsmin = -0.3;
    double Vcu = 999.0;
    double Vcl = -999.0;
    double initialVoltage = 1.0;
    double initialPmech = 0.0;

  public:
    explicit StabilizerST2CUT(const std::string& objName = "pssST2CUT_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;

    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
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

  private:
    double selectedInput(const IOdata& inputs, int mode) const;
    double washoutOutput(const double state[]) const;
    static double leadLagOutput(double input, double state, double leadTime, double lagTime);
    double unlimitedOutput(const double state[]) const;
    bool voltageEnabled(const IOdata& inputs) const;
    double output(const IOdata& inputs, const double state[]) const;
    int outputLimitStatus(const double state[]) const;
    bool updateFlags(const IOdata& inputs, const double state[]);
    static bool supportedMode(int mode);
};
}  // namespace griddyn::stabilizers
