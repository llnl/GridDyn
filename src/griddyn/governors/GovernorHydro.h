/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include "blocks/LeadLag.h"
#include <string>

namespace griddyn::governors {
/**
 * @brief CGMES simplified IEEE hydro governor (GovHydroIEEE0).
 *
 * The speed path is @f$K(1+T_2s)/(1+T_1s)@f$, followed by the gate
 * actuator @f$1/(1+T_3s)@f$.  That response is subtracted from the power
 * reference and limited to @f$[P_{min},P_{max}]@f$ before the turbine and
 * waterway transfer @f$(1-T_ws)/(1+0.5T_ws)@f$.
 */
class GovernorHydro: public Governor {
  public:
  protected:
    model_parameter Tw = 1.0;  //!< [s] water starting time
    blocks::LeadLagKernel governorLeadLag;
    blocks::LeadLagKernel waterway;

  public:
    explicit GovernorHydro(const std::string& objName = "govHydro_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorHydro();

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
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateData,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
    // only called if the genModel is not present

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual stringVec localStateNames() const override;
};

}  // namespace griddyn::governors
