/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "StabilizerPss2a.h"
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::stabilizers {
/** PSS/E PSS2B dual-input power-system stabilizer.
 *
 * With clipped selected inputs u1/u2, washout W(T)=s*T/(1+s*T),
 * lag L(T)=1/(1+s*T), and lead-lag LL(a,b)=(1+s*a)/(1+s*b):
 * \f[
 * b_1=W(T_{W1})W(T_{W2})L(T_6)u_1,\quad
 * b_2=K_{S2}W(T_{W3})W(T_{W4})L(T_7)u_2,
 * \f]
 * \f[
 * R(s)=\frac{(1+sT_8)^N}{(1+sT_9)^{M+N}},\quad
 * v=K_{S1}\big[R(s)(b_1+K_{S3}b_2)-b_2\big],
 * \f]
 * \f$V_{SS}=\operatorname{clamp}(LL(T_1,T_2)LL(T_3,T_4)
 * LL(T_{10},T_{11})v,V_{STMIN},V_{STMAX})\f$.
 * Input bounds are [VS1MIN,VS1MAX] and [VS2MIN,VS2MAX]. Zero washout
 * times bypass their stages; a zero lag denominator requires a zero lead.
 * R is bypassed if M or N is zero; otherwise T9 must be positive.
 * M/N are integers in [0,8], with M*N<=8. States initialize at DC equilibrium.
 *
 * The supported local signal subset is MODE 0: zero, 1/2: machine speed
 * deviation, 3: terminal electrical power, 4: mechanical-power deviation
 * from initialization, 5: terminal voltage. MODE 2 is a local speed proxy;
 * MODE 4 does not subtract electrical power. Nonzero BUSR and MODE 6 are
 * rejected. The shared PSS2A realization retains its electrical-torque input.
 *
 * @par Equation source
 * OpenIPSL Electrical.Controls.PSSE.PSS.PSS2B and
 * NonElectrical.Continuous.RampTrackingFilter, commit 8155c73f.
 * Independent external trajectories remain unverified.
 */
class StabilizerPss2b final: public StabilizerPss2a {
  public:
    explicit StabilizerPss2b(const std::string& objName = "pssPSS2B_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    const ModelDescription& getModelDescription() const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    using StabilizerPss2a::set;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    MachineControllerSignal electricalPowerSignal() const override
    {
        return MachineControllerSignal::ELECTRICAL_POWER;
    }
    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;

  protected:
    LinearValue selectedInput(const IOdata& inputs, int mode, bool first) const override;

  private:
    double Vs1max = 0.08;
    double Vs1min = -0.08;
    double Vs2max = 1.25;
    double Vs2min = -1.25;
};
}  // namespace griddyn::stabilizers
