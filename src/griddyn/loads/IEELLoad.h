/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include "IEELParameters.h"

namespace griddyn::loads {
/** Load implementing the full PSS/E IEEL voltage and frequency characteristic. */
class IEELLoad: public GridLoad {
  private:
    IEELParameters parameters;

    double voltageFactor(bool reactive, double voltage) const;
    double voltageDerivative(bool reactive, double voltage) const;
    double frequencyCoefficient(bool reactive) const;

  public:
    explicit IEELLoad(const std::string& objName = "ieelLoad_$");

    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void setIEELParameters(const IEELParameters& newParameters);
    const IEELParameters& getIEELParameters() const { return parameters; }

    void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

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
