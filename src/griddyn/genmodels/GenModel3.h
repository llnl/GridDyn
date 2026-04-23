/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModelClassical.h"
#include <string>

namespace griddyn::genmodels {

class GenModel3: public GenModelClassical {
  protected:
    double Xl = 0.0;  //!< [pu] leakage reactance
    double Xdp = 0.35;  //!< [pu] d-axis transient reactance
    double E = 0;  //!< constant Eb' field
    double Xq = 0.85;  //!< [pu] q-axis reactance
    double Tdop = 8.0;  //!< [s]    d-axis time constant

  public:
    explicit GenModel3(const std::string& objName = "genModel3_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stringVec localStateNames() const override;
    // dynamics
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const stateData& sD,
                                 double update[],
                                 const solverMode& sMode,
                                 double alpha) override;
};

}  // namespace griddyn::genmodels
