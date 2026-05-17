/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModel3.h"
#include "utilities/saturation.h"
#include <string>
namespace griddyn::genmodels {
class GenModel4: public GenModel3 {
  protected:
    double Xqp = 0.35;  //!< [pu] q-axis transient reactance
    double Tqop = 1.0;  //!< [s]    q-axis time constant
    double S10 = 1.0;  //!< the saturation S (1.0) const
    double S12 = 1.0;  //!< the saturation S(1.2)
    utilities::saturation sat;  //!< saturation function object
  public:
    explicit GenModel4(const std::string& objName = "genModel4_#");
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
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
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

