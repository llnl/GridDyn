/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn::loads {

struct MotorDPower {
    double p = 0.0;
    double q = 0.0;
};

/** WECC single-phase A/C compressor performance model (Motor D core). */
class MotorDLoad: public GridLoad {
  public:
    explicit MotorDLoad(const std::string& objName = "motorD_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    StateSizes localStateSizes(const SolverMode& sMode) const override;
    count_t localJacobianCount(const SolverMode& sMode) const override;
    stringVec localStateNames() const override;
    std::pair<count_t, count_t> LocalRootCount(const SolverMode& sMode) const override;
    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
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
    void ioPartialDerivatives(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode) override;
    count_t outputDependencyCount(index_t outputNum, const SolverMode& sMode) const override;

    bool isStalled() const { return hasStalled; }
    bool isRestartablePartStalled() const { return restartablePartStalled; }
    bool isUndervoltageTripped() const { return undervoltageTripped; }
    double stallBreakVoltage() const { return stallBreak; }

    /** CMPLDW run-state equations, in per unit on the Motor D MVA base. */
    static MotorDPower characteristicPower(double p0,
                                            double compPF,
                                            double voltage,
                                            double frequencyDeviation,
                                            double stallBreakVoltage,
                                            double gStall,
                                            double bStall);
    static double inverseStallCycles(double voltage);
    static double thermalOnlineFraction(double temperature,
                                        double tripStart,
                                        double tripComplete);

  protected:
    void computeStallBreak();
    MotorDPower modelPower(double voltage, double frequency) const;
    double voltageInput(const IOdata& inputs, const StateData& stateData, const SolverMode& sMode) const;
    double frequencyInput(const IOdata& inputs,
                          const StateData& stateData,
                          const SolverMode& sMode) const;
    double measuredVoltage(const IOdata& inputs,
                           const StateData& stateData,
                           const SolverMode& sMode) const;
    MotorDPower modelPower(double voltage,
                           double frequency,
                           double thermalFractionA,
                           double thermalFractionB) const;
    double thermalFraction(double temperature) const;
    double thermalHeating(double voltage, double frequency, bool stalled) const;
    bool contactorProtectionEnabled() const;
    bool thermalProtectionEnabled() const;
    index_t voltageStateIndex() const;
    index_t thermalAStateIndex() const;
    index_t thermalBStateIndex() const;
    double stateValue(const StateData& stateData, const SolverMode& sMode, index_t index) const;
    double memberStateValue(index_t index) const;
    void updateContactor(double voltage);
    void updateInverseTimeStall(CoreTime time, double voltage);

  private:
    double loadFactor = 0.85;  //!< LFm: load MW / motor MVA rating
    double compPF = 0.97;
    double stallVoltage = 0.65;
    double stallResistance = 0.05;
    double stallReactance = 0.20;
    double stallDelay = 0.033;
    double restartableFraction = 0.0;
    double restartVoltage = 0.85;
    double restartDelay = 0.2;
    double undervoltageFraction = 0.0;
    double tripVoltage1 = 2.0;
    double tripDelay1 = 0.0;
    double tripVoltage2 = 2.0;
    double tripDelay2 = 0.0;
    double contactorVoltageOff1 = 0.0;
    double contactorVoltageOff2 = 0.0;
    double contactorVoltageOn1 = 0.0;
    double contactorVoltageOn2 = 0.0;
    double thermalTimeConstant = 0.0;
    double thermalTripStart = 0.0;
    double thermalTripComplete = 0.0;
    double voltageMeasurementLag = 0.0;
    double motorBaseScale = 1.0;
    double stallBreak = 0.4;
    double gStall = 0.0;
    double bStall = 0.0;

    bool hasStalled = false;
    bool restartablePartStalled = false;
    bool undervoltageTripped = false;
    bool stallTimerActive = false;
    bool restartTimerActive = false;
    bool tripTimerActive1 = false;
    bool tripTimerActive2 = false;
    CoreTime stallStart = timeZero;
    CoreTime restartStart = timeZero;
    CoreTime tripStart1 = timeZero;
    CoreTime tripStart2 = timeZero;
    double contactorFraction = 1.0;
    double previousContactorVoltage = 1.0;
    bool inverseTimeActive = false;
    CoreTime inverseTimeStart = timeZero;
    double inverseTimeCycles = 0.0;
    double inverseTimeAccumulation = 0.0;
};

}  // namespace griddyn::loads
