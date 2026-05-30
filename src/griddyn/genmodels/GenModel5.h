/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModel4.h"
#include <string>

namespace griddyn::genmodels {

class GenModel5: public GenModel4 {
  protected:
    double Tqopp = 0.1;
    double Taa = 0.0;
    double Tdopp = 0.8;
    double Xdpp = 0.175;
    double Xqpp = 0.175;

  public:
    explicit GenModel5(const std::string& objName = "genModel5_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
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
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& sD,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& sD,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
};

}  // namespace griddyn::genmodels
