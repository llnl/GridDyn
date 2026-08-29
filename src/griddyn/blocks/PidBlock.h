/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/**
 * @brief Filtered PID controller with a first-order output stage.
 *
 * With error @f$e=u+b@f$, integral state @f$i@f$, derivative-filter state
 * @f$d@f$, and output state @f$y@f$, the model uses
 * @f[\dot i=I e,\qquad T_1\dot d=D e-d,\qquad
 * T_d\dot y=K(Pe+\dot d+i)-y.\f]
 * The derivative path is omitted when `d` is zero.  Parameters `p`, `i`,
 * `d`, `t1`, `td`, and `iv`/`initial_value` set the displayed quantities;
 * inherited GridBlock limits apply to the output state.
 */
class PidBlock: public GridBlock {
  public:
  protected:
    double m_P = 1;  //!< proportional control constant
    double m_I = 0;  //!< integral control constant
    double m_D = 0;  //!< differential control constant
    double m_T1 = 0.01;  //!< filtering delay on the input for the differential calculation
    double m_Td = 0.01;  //!< filtering delay on the output
    double iv = 0;  //!< intermediate value for calculations
    bool& no_D;  //!< ignore the derivative part of the calculations
  public:
    /** @brief default constructor*/
    explicit PidBlock(const std::string& objName = "pidBlock_#");
    /** @brief alternate constructor
@param[in] proportionalGain the proportional gain
@param[in] integralGain the integral gain
@param[in] derivativeGain the derivative gain
*/
    PidBlock(double proportionalGain,
             double integralGain,
             double derivativeGain,
             const std::string& objName = "pidBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const StateData& stateDataValue,
                                 double deriv[],
                                 const SolverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataValue,
                                       MatrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const SolverMode& sMode) override;
    virtual double step(CoreTime time, double inputA) override;
    virtual stringVec localStateNames() const override;
};

}  // namespace griddyn::blocks
