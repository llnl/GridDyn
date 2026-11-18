/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include <string>

namespace griddyn::loads {
/** @brief a load with powers as a exponential function of voltage*/
class ExponentialLoad: public GridLoad {
  public:
  protected:
    model_parameter alphaP = 0.0;  //!< the voltage exponent parameter for the real power output
    model_parameter alphaQ = 0.0;  //!< the voltage exponent parameter for the reactive power output

  public:
    explicit ExponentialLoad(const std::string& objName = "expLoad_$");
    /** constructor taking power arguments
@param[in] realPower the real power of the load
@param[in] reactivePower the reactive power of the load
@param[in] objName the name of the object
*/
    ExponentialLoad(double realPower,
                    double reactivePower,
                    const std::string& objName = "expLoad_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    const ModelDescription& getModelDescription() const override;

    using GridLoad::set;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& sD,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual double getRealPower(const IOdata& inputs,
                                const StateData& sD,
                                const SolverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const StateData& sD,
                                    const SolverMode& sMode) const override;
    virtual double getRealPower(double voltage) const override;
    virtual double getReactivePower(double voltage) const override;
    virtual double getRealPower() const override;
    virtual double getReactivePower() const override;
};
}  // namespace griddyn::loads
