/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "exponentialLoad.h"
#include <string>
namespace griddyn::loads {
/** @brief a load with powers as a exponential function of voltage and frequency*/
class fDepLoad: public exponentialLoad {
  public:
  protected:
    model_parameter betaP = 0.0;  //!< the frequency exponent parameter for the real power output
    model_parameter betaQ =
        0.0;  //!< the frequency exponent parameter for the reactive power output

  public:
    explicit fDepLoad(const std::string& objName = "fdepLoad_$");
    /** constructor taking power arguments
@param[in] rP the real power of the load
@param[in] qP the reactive power of the load
@param[in] objName the name of the object
*/
    fDepLoad(double rP, double qP, const std::string& objName = "fdepLoad_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
    virtual double getRealPower(const IOdata& inputs,
                                const stateData& sD,
                                const solverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode) const override;
    virtual double getRealPower(double V) const override;
    virtual double getReactivePower(double V) const override;
    virtual double getRealPower() const override;
    virtual double getReactivePower() const override;
    /** get the real power input as a function of V and f
@param[in] V the voltage input in pu
@param[in] f the frequency input
@return the real load
*/
    virtual double getRealPower(double V, double f) const;
    /** get the reactive power input as a function of V and f
@param[in] V the voltage input in pu
@param[in] f the frequency input
@return the reactive load
*/
    virtual double getReactivePower(double V, double f) const;
};
}  // namespace griddyn::loads
