/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"
#include "core/coreOwningPtr.hpp"
#include <string>

namespace griddyn {
namespace blocks {
    class pidBlock;
    class delayBlock;
}  // namespace blocks

namespace links {
    /** class defines an object that converts operation between dc and ac, can act as a inverter, a
     * rectifier or a bidirectional mode
     */
    class acdcConverter: public Link {
      public:
        enum inverter_flags {
            fixed_power_control = object_flag6,
        };
        enum class mode_t { rectifier, inverter, bidirectional };

      protected:
        enum class control_mode_t { current, power, voltage };
        model_parameter r = 0.0;  //!< [puOhm] per unit resistance
        model_parameter x = 0.001;  //!< [puOhm] per unit reactance
        model_parameter tap = 1.0;  //!< converter tap
        double angle = 0.0;  //!< converter firing or extinction angle
        model_parameter Idcmax = kBigNum;  //!<[puA] max reference current
        model_parameter Idcmin = -kBigNum;  //!<[puA] min reference current
        model_parameter mp_Ki = 0.03;  //!< integral gain angle control
        model_parameter mp_Kp = 0.97;  //!< proportional gain angle control
        double Idc = 0.0;  //!< storage for dc current
        mode_t type = mode_t::bidirectional;  //!< converter type
        model_parameter vTarget = 1.0;  //!< [puV] ac voltage target
        model_parameter mp_controlKi = -0.03;  //!< integral gain angle control
        model_parameter mp_controlKp = -0.97;  //!< proportional gain angle control
        model_parameter tD = 0.01;  //!< controller time delay
        model_parameter baseTap = 1.0;  //!< base l evel tap of the converter
        double dirMult = 1.0;
        model_parameter minAngle = -kPI / 2.0;  //!< [rad] minimum tap angle
        model_parameter maxAngle = kPI / 2.0;  //!< [rad]  maximum tap angle
        control_mode_t control_mode = control_mode_t::voltage;

        coreOwningPtr<blocks::pidBlock> firingAngleControl;  //!< block controlling firing angle
        coreOwningPtr<blocks::pidBlock> powerLevelControl;  //!< block controlling power
        coreOwningPtr<blocks::delayBlock> controlDelay;  //!< delayblock for control of tap

      public:
        explicit acdcConverter(const std::string& objName = "acdcConveter_$");
        // name will be based on opType
        acdcConverter(mode_t opType, const std::string& objName = "");
        acdcConverter(double resistanceParameter,
                      double reactanceParameter,
                      const std::string& objName = "acdcConveter_$");

        virtual ~acdcConverter();
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual double getMaxTransfer() const override;

        // virtual void pFlowCheck (std::vector<Violation> &Violation_vector);
        // virtual void getVariableType (double sdata[], const solverMode &sMode);      //has no
        // state variables
        virtual void updateBus(gridBus* bus, index_t busnumber) override;

        virtual void updateLocalCache() override;
        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& stateDataValue,
                                      const solverMode& sMode) override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        virtual double quickupdateP() override { return 0; }

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void
            set(std::string_view param, double val, units::unit unitType = units::defunit) override;

        // dynInitializeB dynamics
        // virtual void dynObjectInitializeA (coreTime time0, std::uint32_t flags);

        using Link::ioPartialDerivatives;
        virtual void ioPartialDerivatives(id_type_t busId,
                                          const stateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode) override;

        virtual void outputPartialDerivatives(const IOdata& inputs,
                                              const stateData& stateDataValue,
                                              matrixData<double>& matrixDataValue,
                                              const solverMode& sMode) override;

        virtual void outputPartialDerivatives(id_type_t busId,
                                              const stateData& stateDataValue,
                                              matrixData<double>& matrixDataValue,
                                              const solverMode& sMode) override;
        virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;
        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& stateDataValue,
                                      matrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
        virtual void residual(const IOdata& inputs,
                              const stateData& stateDataValue,
                              double resid[],
                              const solverMode& sMode) override;
        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;
        virtual void guessState(coreTime time,
                                double state[],
                                double dstate_dt[],
                                const solverMode& sMode) override;
        // for computing all the Jacobian elements at once
        virtual int fixRealPower(double power,
                                 id_type_t terminal,
                                 id_type_t fixedTerminal = 0,
                                 units::unit unitType = units::defunit) override;
        virtual int fixPower(double rPower,
                             double qPower,
                             id_type_t measureTerminal,
                             id_type_t fixedTerminal = 0,
                             units::unit unitType = units::defunit) override;

        virtual void getStateName(stringVec& stNames,
                                  const solverMode& sMode,
                                  const std::string& prefix = "") const override;

      private:
        /** build out the components of the converter*/
        void buildSubsystem();
    };

}  // namespace links
}  // namespace griddyn
