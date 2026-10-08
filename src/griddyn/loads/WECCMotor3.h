/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CIMLoad.h"
#include "MotorProtectionGroups.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::loads {

/** WECC CMPLDW three-phase induction motor using direct CIM5/6-family data.
 *
 * The electrical states and equations are inherited from CIM6.  Unlike the
 * older native CIM models, this class accepts the CMPLDW direct parameters
 * (Rs, Ls, Lp, Lpp, Tpo, Tppo) and the CMPLDW speed-torque law.  It adds two
 * cumulative undervoltage trip/reclose groups; the old motor and CIM factory
 * types keep their existing defaults and behavior.
 */
class WECCMotor3: public CIM6 {
  public:
    explicit WECCMotor3(const std::string& objName = "weccMotor3_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    std::pair<count_t, count_t> LocalRootCount(const SolverMode& sMode) const override;
    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;

    double onlineFraction() const { return protectionGroups.onlineFraction(); }
    double mechanicalTorqueAtSpeed(double speed) const;
    double rotorSpeed() const { return (m_state.size() > 2) ? 1.0 - m_state[2] : 1.0; }

  protected:
    void converge() override;
    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    double motorOutputFraction() const override;
    double mechPower(double slip) const override;
    double dmechds(double slip) const override;

  private:
    struct ProtectionParameters {
        double tripVoltage = 2.0;
        double tripDelay = 0.0;
        double tripFraction = 0.0;
        double reconnectVoltage = 2.0;
        double reconnectDelay = 0.0;
    };

    void setProtectionParameter(std::size_t stage, std::string_view parameter, double value);
    void configureProtectionStage(std::size_t stage);

    double loadFactor = 0.85;  //!< CMPLDW LFm, MW/MVA rating
    double torqueExponent = 2.0;  //!< CMPLDW Etrq
    double torqueNominal = 1.0;  //!< initialized to mechanical power on motor base
    bool torqueNominalSet = false;
    bool explicitMotorBase = false;
    double synchronousReactance = 2.5;  //!< Ls
    double transientReactance = 0.2;  //!< Lp
    double subtransientReactance = 0.15;  //!< Lpp
    double transientTimeConstant = 0.44;  //!< Tpo
    double subtransientTimeConstant = 0.0026;  //!< Tppo
    std::array<ProtectionParameters, MotorProtectionGroups::stageCount> protectionParameters{};
    MotorProtectionGroups protectionGroups;
};

}  // namespace griddyn::loads
