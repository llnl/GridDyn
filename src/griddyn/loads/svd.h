/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "rampLoad.h"
#include <string>
#include <vector>

namespace griddyn {
namespace loads {
    /** @brief defining the interface for a static var device*/
    class svd: public rampLoad {
      public:
        /** flags used for svd operation*/
        enum svd_flags {
            continuous_flag = object_flag6,
            locked_flag = object_flag7,
            reactive_control_flag = object_flag8,
            reverse_control_flag = object_flag9,
            reverse_toggled_flag = object_flag10,  // indicator that the reverse flag has been
                                                   // toggled so don't try it again
        };

      protected:
        gridBus* controlBus = nullptr;  //!< pointer to the control bus
        model_parameter Qmin = -kBigNum;  //!<[puMVA] the minimum reactive power
        model_parameter Qmax = kBigNum;  //!<[puMVA] the maximum reactive power output
        model_parameter Vmin = 0.8;  //!<[puV] the low voltage threshold
        model_parameter Vmax = 1.2;  //!<[puV] the high voltage threshold

        model_parameter Qlow = 0.0;  //!<[puMVA] the lowest available Q block level
        model_parameter Qhigh = kBigNum;  //!<[puMVA] the maximum reactive power block level
        int currentStep = 0;  //!< the current step level
        int stepCount = 0;  //!< the total number of steps available
        std::vector<std::pair<int, double>>
            Cblocks;  // a vector containing the capacitive blocks (count, size[puMW])

        model_parameter participation = 1.0;  //!< a participation factor

      public:
        svd(const std::string& objName = "svd_$");
        svd(double rP, double rQ, const std::string& objName = "svd_$");
        virtual ~svd();

        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;
        virtual void setLoad(double level, units::unit unitType = units::defunit) override;
        virtual void
            setLoad(double Plevel, double Qlevel, units::unit unitType = units::defunit) override;
        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;  // for saving the state
        virtual void
            guessState(coreTime time,
                       double state[],
                       double dstate_dt[],
                       const solverMode& sMode) override;  // for initial setting of the state

        virtual void getVariableType(double sdata[], const solverMode& sMode) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        /** define which bus the svd is controlling voltage on if it is not otherwise specified it
         * is assumed to be the parent bus
         */
        virtual void setControlBus(gridBus* cBus);

        /** add a reactive block to the controller
    @param[in] steps the number of steps in the block
    @param[in] Qstep  the size of each step
    @param[in] unitType  the units of Qstep
    */
        void addBlock(int steps, double Qstep, units::unit unitType = units::defunit);

        virtual change_code powerFlowAdjust(const IOdata& inputs,
                                            std::uint32_t flags,
                                            check_level_t level) override;
        virtual void reset(reset_levels level = reset_levels::minimal) override;

        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;

        virtual void derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode) override;

        virtual void outputPartialDerivatives(const IOdata& inputs,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              const solverMode& sMode) override;

        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
        virtual void getStateName(stringVec& stNames,
                                  const solverMode& sMode,
                                  const std::string& prefix) const override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

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

      protected:
        /** get the setting corresponding to a specific output level
    @param[in] level the reactive output level desired [puMW]
    @return the step number corresponding to that level (best effort)
    */
        virtual int checkSetting(double level);
        /** change the output setting to correspond to a specific step number
    @param step the step number for the update
    */
        virtual void updateSetting(int step);
    };
}  // namespace loads
}  // namespace griddyn
