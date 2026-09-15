/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ZipLoad.h"

namespace griddyn::loads {
/**
 * @brief Fixed shunt with ANDES-compatible instantaneous phase-voltage outputs.
 *
 * The electrical behavior remains the ordinary fixed-admittance ZipLoad.  The
 * additional outputs reconstruct the three phase voltages from the positive
 * sequence bus voltage and angle.
 */
class ShuntTD: public ZipLoad {
  public:
    enum ShuntTDOutputs {
        VTA_OUTPUT = 2,
        VTB_OUTPUT = 3,
        VTC_OUTPUT = 4,
    };

    explicit ShuntTD(const std::string& objName = "shuntTD_$");
    ShuntTD(double realPower, double reactivePower, const std::string& objName = "shuntTD_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual double getOutput(const IOdata& inputs,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             index_t outputNum = 0) const override;
    virtual double getOutput(index_t outputNum = 0) const override;

    virtual IOdata getOutputs(const IOdata& inputs,
                              const StateData& stateData,
                              const SolverMode& sMode) const override;

    virtual const std::vector<stringVec>& outputNames() const override;
    virtual units::unit outputUnits(index_t outputNum) const override;

  private:
    double phaseVoltage(const IOdata& inputs,
                        const StateData& stateData,
                        const SolverMode& sMode,
                        double phaseOffset) const;
};
}  // namespace griddyn::loads
