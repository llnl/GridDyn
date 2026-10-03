/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include "WSCCParameters.h"

namespace griddyn::loads {
/** Load implementing the full WSCC voltage and frequency characteristic.
 *
 * With P4=Q4=0, the model is
 * P=P0(P1 V^2+P2 V+P3)(1+LPD(f-1)) and
 * Q=Q0(Q1 V^2+Q2 V+Q3)(1+LQD(f-1)).
 * If either P4 or Q4 is nonzero, the model is
 * P=P0(P1 V^2+P2 V+P3+P4(1+LPD(f-1))) and
 * Q=Q0(Q1 V^2+Q2 V+Q3+Q4(1+LQD(f-1))).
 * P0 and Q0 are scaled at import to match the existing load at its initial
 * voltage and frequency. VMIN is the optional final DYD field assumed by this
 * implementation: below a positive VMIN, power is continued as constant
 * impedance from the value at VMIN. The WSCC equations follow the PowerWorld
 * reference:
 * https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Load%20Characteristic%20WSCC.htm
 */
class WSCCLoad: public GridLoad {
  private:
    WSCCParameters parameters;

    bool hasFrequencyDependence() const;
    double voltagePolynomial(bool reactive, double voltage) const;
    double voltageDerivative(bool reactive, double voltage) const;
    double frequencyDerivative(bool reactive, double voltage) const;
    double powerAtVoltage(bool reactive, double voltage, double frequency) const;
    double powerVoltageDerivative(bool reactive, double voltage, double frequency) const;
    double powerFrequencyDerivative(bool reactive, double voltage, double frequency) const;

  public:
    explicit WSCCLoad(const std::string& objName = "wsccLoad_$");

    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void setWSCCParameters(const WSCCParameters& newParameters);
    const WSCCParameters& getWSCCParameters() const { return parameters; }

    void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param,
               units::unit unitType = units::defunit) const override;

    void ioPartialDerivatives(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
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
};
}  // namespace griddyn::loads
