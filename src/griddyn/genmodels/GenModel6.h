/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModel5.h"
#include <string>

namespace griddyn::genmodels {

class GenModel6: public GenModel5 {
  protected:
  public:
    explicit GenModel6(const std::string& objName = "genModel6_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

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

