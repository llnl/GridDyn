/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "motorLoad3.h"
#include <string>
#include <vector>
namespace griddyn::loads {
/** @brief class implementing a model of a 3rd order induction motor
 */
class motorLoad5: public motorLoad3 {
  private:
    /** @brief private enumerations of state variable locations in powerflow*/
    enum pLocA { irA = 0, imA = 1, slipA = 2, erpA = 3, empA = 4, erppA = 5, emppA = 6 };
    /** @brief private enumerations of state variable locations in dynamic calculations*/
    enum pLocD { slipD = 0, erpD = 1, empD = 2, erppD = 3, emppD = 4 };

  protected:
    double r2 = 0.002;  //!< 3rd level loop resistance
    double x2 = 0.04;  //!< 3 impedance loop reactance
    double T0pp = 0.0;  //!< subtransient time constant
    double xpp = 0.0;  //!< subtransient reactance
  public:
    /** @brief constructor
@param[in] objName  the name of the object
*/
    explicit motorLoad5(const std::string& objName = "motor5_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

  protected:
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;

    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& sMode) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t level) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix) const override;

    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const override;
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    // TODO(phlpt): Change to algebraic update.
    virtual void
        updateCurrents(const IOdata& inputs, const stateData& sD, const solverMode& sMode) override;

  private:
    /** @brief estimate the initial state values of the motor
     */
    void converge();
};
}  // namespace griddyn::loads
