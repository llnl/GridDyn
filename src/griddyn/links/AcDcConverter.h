/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"
#include "core/CoreOwningPtr.hpp"
#include <string>

namespace griddyn {
namespace blocks {
    class PidBlock;
    class DelayBlock;
}  // namespace blocks

namespace links {
    /** class defines an object that converts operation between dc and ac, can act as a inverter, a
     * rectifier or a bidirectional mode
     */
    class AcDcConverter: public Link {
      public:
        enum InverterFlags {
            FIXED_POWER_CONTROL = object_flag6,
        };
        enum class Mode { RECTIFIER, INVERTER, BIDIRECTIONAL };

      protected:
        enum class ControlMode { CURRENT, POWER, VOLTAGE };
        model_parameter r = 0.0;  //!< [puOhm] per unit resistance
        model_parameter x = 0.001;  //!< [puOhm] per unit reactance
        model_parameter tap = 1.0;  //!< converter tap
        double angle = 0.0;  //!< converter firing or extinction angle
        model_parameter Idcmax = kBigNum;  //!<[puA] max reference current
        model_parameter Idcmin = -kBigNum;  //!<[puA] min reference current
        model_parameter mp_Ki = 0.03;  //!< integral gain angle control
        model_parameter mp_Kp = 0.97;  //!< proportional gain angle control
        double Idc = 0.0;  //!< storage for dc current
        Mode type = Mode::BIDIRECTIONAL;  //!< converter type
        model_parameter vTarget = 1.0;  //!< [puV] ac voltage target
        model_parameter mp_controlKi = -0.03;  //!< integral gain angle control
        model_parameter mp_controlKp = -0.97;  //!< proportional gain angle control
        model_parameter tD = 0.01;  //!< controller time delay
        model_parameter baseTap = 1.0;  //!< base l evel tap of the converter
        double dirMult = 1.0;
        model_parameter minAngle = -kPI / 2.0;  //!< [rad] minimum tap angle
        model_parameter maxAngle = kPI / 2.0;  //!< [rad]  maximum tap angle
        ControlMode controlMode = ControlMode::VOLTAGE;

        CoreOwningPtr<blocks::PidBlock> firingAngleControl;  //!< block controlling firing angle
        CoreOwningPtr<blocks::PidBlock> powerLevelControl;  //!< block controlling power
        CoreOwningPtr<blocks::DelayBlock> controlDelay;  //!< delayblock for control of tap

      public:
        explicit AcDcConverter(const std::string& objName = "acdcConveter_$");
        // name will be based on opType
        AcDcConverter(Mode opType, const std::string& objName = "");
        AcDcConverter(double resistanceParameter,
                      double reactanceParameter,
                      const std::string& objName = "acdcConveter_$");

        virtual ~AcDcConverter();
        virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

        virtual double getMaxTransfer() const override;

        // virtual void pFlowCheck (std::vector<Violation> &Violation_vector);
        // virtual void getVariableType (double sdata[], const SolverMode &sMode);      //has no
        // state variables
        virtual void updateBus(GridBus* bus, index_t busnumber) override;

        virtual void updateLocalCache() override;
        virtual void updateLocalCache(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      const SolverMode& sMode) override;
        virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void
            timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

        virtual double quickupdateP() override { return 0; }

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void
            set(std::string_view param, double val, units::unit unitType = units::defunit) override;

        // dynInitializeB dynamics
        // virtual void dynObjectInitializeA (CoreTime time0, std::uint32_t flags);

        using Link::ioPartialDerivatives;
        virtual void ioPartialDerivatives(id_type_t busId,
                                          const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const IOlocs& inputLocs,
                                          const SolverMode& sMode) override;

        virtual void outputPartialDerivatives(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              MatrixData<double>& matrixDataValue,
                                              const SolverMode& sMode) override;

        virtual void outputPartialDerivatives(id_type_t busId,
                                              const StateData& stateDataValue,
                                              MatrixData<double>& matrixDataValue,
                                              const SolverMode& sMode) override;
        virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
        virtual void jacobianElements(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
        virtual void residual(const IOdata& inputs,
                              const StateData& stateDataValue,
                              double resid[],
                              const SolverMode& sMode) override;
        virtual void setState(CoreTime time,
                              const double state[],
                              const double dstateDt[],
                              const SolverMode& sMode) override;
        virtual void guessState(CoreTime time,
                                double state[],
                                double dstateDt[],
                                const SolverMode& sMode) override;
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
                                  const SolverMode& sMode,
                                  const std::string& prefix = "") const override;

      private:
        /** build out the components of the converter*/
        void buildSubsystem();
    };

}  // namespace links
}  // namespace griddyn
