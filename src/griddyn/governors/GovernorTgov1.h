/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GovernorIeeeSimple.h"
#include <string>
#include <vector>

namespace griddyn::governors {
/**
 * @brief PSS/e TGOV1 turbine governor.
 *
 * The valve state follows \f$\dot v=(-v+P_{ref}-\Delta\omega/R)/T_1\f$,
 * with the PSS/e VMIN/VMAX limits.  The turbine output is the lead-lag
 * \f$(1+sT_2)/(1+sT_3)\f$ response of the valve state, minus
 * \f$D_t\Delta\omega\f$.
 *
 * These equations match ANDES v2.0.0
 * `andes/models/governor/tgov1.py`, TGOV1Model.
 */
class GovernorTgov1: public GovernorIeeeSimple {
  public:
  protected:
    double Dt = 0.0;  //!< speed damping constant
  public:
    explicit GovernorTgov1(const std::string& objName = "govTgov1_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorTgov1();
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateData,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateData,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
};
}  // namespace griddyn::governors
