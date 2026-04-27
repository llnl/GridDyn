/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../common/MapParam.h"
#include "Equation_DAE_full.h"
#include <memory>
#include <vector>
namespace griddyn {
class gridDynSimulation;
class solverMode;
}  // namespace griddyn

using namespace std;
namespace griddyn::paradae {
/** @brief class that connects ParaDAE and GridDyn
 */
class EquationGridDyn: public Equation_DAE_full {
    griddyn::gridDynSimulation* gds;

  public:
    // EquationGridDyn specific
    EquationGridDyn(Real t0_,
                    Real Tmax_,
                    int N_unistep_,
                    griddyn::gridDynSimulation* gds_,
                    const Vector& y0_,
                    griddyn::solverMode* mode_,
                    vector<double>& discontinuities,
                    vector<int>& rootsfound);
    static EquationGridDyn Default(const MapParam& param);
    griddyn::solverMode* mode;  //!< to the solverMode

    // Redefinition of inherited virtual methods
    virtual void function(const Real t,
                          const Vector& y,
                          const Vector& dy,
                          const Vector& state,
                          Vector& Fydy);
    virtual void jacobian_ypcdy(const Real t,
                                const Vector& y,
                                const Vector& dy,
                                const Vector& state,
                                const Real cj,
                                Matrix& J);
    virtual void init(const Real t, Vector& y);
    virtual void root_functions(const Real t,
                                const Vector& y,
                                const Vector& dy,
                                const Vector& state,
                                Vector& rv);
    virtual void
        root_action(const Real troot, Vector& yroot, Vector& dyroot, const Vector& iroot);
    virtual ~EquationGridDyn() {};
};

}  // namespace griddyn::paradae
