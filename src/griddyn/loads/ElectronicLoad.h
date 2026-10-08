/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include "IEELParameters.h"
#include <string>
#include <string_view>

namespace griddyn::loads {
/**
 * Aggregate power-electronic load with the CMPLDW low-voltage trip/recovery
 * characteristic and optional independent IEEL-style P/Q voltage and frequency curves.
 *
 * With the default curves and frequency coefficients, P and Q are constant above Vd1,
 * scale together through the Vd1/Vd2 trip characteristic, and use the configured PFel.
 * Set PFel to zero to use the reactive base power assigned by the containing load/composite.
 */
class ElectronicLoad: public GridLoad {
  public:
    explicit ElectronicLoad(const std::string& objName = "electronicLoad_$");

    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void setIEELParameters(const IEELParameters& newParameters);
    const IEELParameters& getIEELParameters() const { return parameters; }

    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

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

  private:
    IEELParameters parameters;
    double voltageTripStart = 0.7;
    double voltageTripComplete = 0.5;
    double recoveryFraction = 0.8;
    double powerFactor = 1.0;
    double minimumVoltage = 1.0;
    bool usePowerFactor = false;

    double voltageFactor(bool reactive, double voltage) const;
    double voltageDerivative(bool reactive, double voltage) const;
    double frequencyCoefficient(bool reactive) const;
    double tripFactor(double voltage) const;
    double tripDerivative(double voltage) const;
    double reactiveBasePower() const;
    double
        frequency(const IOdata& inputs, const StateData& stateData, const SolverMode& sMode) const;
    void validateTripParameters() const;
};
}  // namespace griddyn::loads
