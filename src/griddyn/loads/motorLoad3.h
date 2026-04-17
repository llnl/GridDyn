/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "motorLoad.h"
#include <string>
#include <vector>
namespace griddyn::loads {
    /** @brief class implementing a model of a 3rd order induction motor
     */
    class motorLoad3: public motorLoad {
      protected:
        double xp = 0.0;  //!< transient reactance of the motor
        double T0p = 0.0;  //!< transient time constant of the motor
        double x0 = 0.0;  //!< x0 parameter

        // double theta=0;
      public:
        /** @brief constructor
    @param[in] objName  the name of the object
    */
        motorLoad3(const std::string& objName = "motor3_$");

        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;  // for saving the state
        virtual void guessState(coreTime time,
                                double state[],
                                double dstate_dt[],
                                const solverMode& sMode) override;
        virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

        virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;

        virtual void derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode)
            override;  // return D[0]=dP/dV D[1]=dP/dtheta,D[2]=dQ/dV,D[3]=dQ/dtheta
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

        virtual void outputPartialDerivatives(const IOdata& inputs,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              const solverMode& sMode) override;
        virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;

        virtual void ioPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode) override;
        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
        virtual void getStateName(stringVec& stNames,
                                  const solverMode& sMode,
                                  const std::string& prefix) const override;
        virtual index_t findIndex(const std::string& field, const solverMode& sMode) const override;
        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        virtual double getRealPower(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode) const override;
        virtual double getReactivePower(const IOdata& inputs,
                                        const stateData& sD,
                                        const solverMode& sMode) const override;
        virtual double getRealPower(double voltage) const override;
        virtual double getReactivePower(double voltage) const override;
        virtual double getRealPower() const override;
        virtual double getReactivePower() const override;
        virtual void
            updateCurrents(const IOdata& inputs, const stateData& sD, const solverMode& sMode);

      private:
        /** @brief estimate the initial state values of the motor
         */
        void converge();
    };

}  // namespace griddyn::loads
