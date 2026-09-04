/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GovernorSteamNR.h"
#include <string>

namespace griddyn::governors {
/** IEEE reheat steam-turbine governor with chest, reheater, and crossover lags.
 *
 * The speed-control and valve equations are inherited from GovernorSteamNR.
 * With chest, reheater, and crossover states \f$c\f$, \f$r\f$, and \f$q\f$,
 * this class appends
 * \f[
 * T_{ch}\dot c=v-c,\qquad T_{rh}\dot r=c-r,\qquad
 * T_{co}\dot q=r-q,
 * \qquad P_m=F_{ch}c+F_{ip}r+F_{lp}q.
 * \f]
 * The fractions need not be normalized: initialization divides the requested
 * mechanical power by their sum, so the displayed output starts at the
 * requested value.  All fractions must be nonnegative and their sum positive.
 */
class GovernorSteamTCSR: public GovernorSteamNR {
  public:
  protected:
    model_parameter Trh = 0.0;  //!< [s] reheater time constant
    model_parameter Tco = 0.0;  //!< [s] crossover-pipe time constant
    model_parameter Fch = 0.0;  //!< [pu] high-pressure (chest) power fraction
    model_parameter Fip = 0.0;  //!< [pu] intermediate-pressure power fraction
    model_parameter Flp = 0.0;  //!< [pu] low-pressure power fraction
  public:
    GovernorSteamTCSR(const std::string& objName = "govSteamTCSR_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorSteamTCSR();
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
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& sD,
                            double deriv[],
                            const SolverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  MatrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
};

}  // namespace griddyn::governors
