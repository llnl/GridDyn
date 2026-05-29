/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include "../blocks/ControlBlock.h"
#include "../blocks/DeadbandBlock.h"
#include "../blocks/DelayBlock.h"
#include <string>
#include <vector>

namespace griddyn::governors {
class GovernorIeeeSimple: public Governor {
  public:
  protected:
    double T3;  //!< [s]    servo motor time constant
    double Pup;  //!< [pu] upper ramp limit
    double Pdown;  //!< [pu] lower ramp limit
  public:
    explicit GovernorIeeeSimple(const std::string& objName = "govIeeeSimple_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorIeeeSimple();
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    virtual void residual(const IOdata& inputs,
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& sD,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const StateData& sD,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    // virtual void setTime(coreTime time){prevTime=time;};
};

}  // namespace griddyn::governors
