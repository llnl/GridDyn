/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ExciterIEEEtype1.h"
#include <string>
namespace griddyn::exciters {
/** @brief DC1A exciter
 */
class ExciterDC1A: public ExciterIEEEtype1 {
  protected:
    double Tc = 0.0;
    double Tb = 1.0;

  public:
    explicit ExciterDC1A(const std::string& objName = "exciterDC1A_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stringVec localStateNames() const override;

    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateDataValue,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double root[],
                          const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;
    // virtual void setTime(CoreTime time){prevTime=time;};
  protected:
    /** @brief the Jacobian entries for the limiter
@param[in] V the voltage
@param[in] Vloc the location of the voltage
@param[in] refLoc  the location of the reference
@param[in] cj  the differential scale variable
@param[out] matrixDataValue the array structure to store the Jacobian data in
*/
    virtual void limitJacobian(double V,
                               int Vloc,
                               int refLoc,
                               double cj,
                               MatrixData<double>& matrixDataValue);
    double measuredVoltage(const IOdata& inputs, const double state[]) const;
};
}  // namespace griddyn::exciters
