/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "MotorLoad.h"
#include <string>
#include <vector>
namespace griddyn::loads {
/** @brief class implementing a model of a 3rd order induction motor
 */
class MotorLoad3: public MotorLoad {
  protected:
    double xp = 0.0;  //!< transient reactance of the motor
    double T0p = 0.0;  //!< transient time constant of the motor
    double x0 = 0.0;  //!< x0 parameter

    // double theta=0;
  public:
    /** @brief constructor
@param[in] objName  the name of the object
*/
    MotorLoad3(const std::string& objName = "motor3_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const SolverMode& sMode) override;  // for saving the state
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const SolverMode& sMode) override;
    virtual stateSizes localStateSizes(const SolverMode& sMode) const override;

    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;

    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const SolverMode& sMode)
        override;  // return D[0]=dP/dV D[1]=dP/dtheta,D[2]=dQ/dV,D[3]=dQ/dtheta
    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const stateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const SolverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    virtual void timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual double getRealPower(const IOdata& inputs,
                                const stateData& sD,
                                const SolverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const stateData& sD,
                                    const SolverMode& sMode) const override;
    virtual double getRealPower(double voltage) const override;
    virtual double getReactivePower(double voltage) const override;
    virtual double getRealPower() const override;
    virtual double getReactivePower() const override;
    virtual void updateCurrents(const IOdata& inputs, const stateData& sD, const SolverMode& sMode);

  private:
    /** @brief estimate the initial state values of the motor
     */
    void converge();
};

}  // namespace griddyn::loads
