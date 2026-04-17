/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "EqGridDyn.h"

#include "../common/Timer.h"
#include "../math/paradaeArrayData.h"
#include "coupling/GhostSwingBus.h"
#include "fileInput/fileInput.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/simulation/gridSimulation.h"
#include "runner/gridDynRunner.h"
#include <cmath>
#include <iostream>
#include <vector>

#include <boost/container/small_vector.hpp>

namespace griddyn::paradae {

using std::cerr;
using std::endl;
using std::vector;

EquationGridDyn::EquationGridDyn(Real t0_,
                                 Real Tmax_,
                                 int N_unistep_,
                                 gridDynSimulation* gds_,
                                 const Vector& y0_,
                                 solverMode* mode_,
                                 vector<double>& discontinuities)
{
    // n=gds_->stateSize(cDaeSolverMode);
    n = gds_->stateSize(*mode_);
    t0 = t0_;
    Tmax = Tmax_;
    N_unistep = N_unistep_;
    y0.Resize(n);
    y0.CopyData(y0_);
    if (gds_ == NULL) {
        cerr << "Calling constructor with noninitialised gridDynSimulation pointer..." << endl;
        abort();
    }
    gds = gds_;
    mode = mode_;
    name = "EquationGridDyn";

    // Old code setting up 100 square pulse discontinuities at t=[0.5, 1.5, 2.5, ...]
    // roots=RootManager(100,1,0,1e-10);
    // roots.n_sactive=100;
    // for (int i=0; i<100; i++)
    //{
    //    roots.is_active(i) = 1;
    //    roots.t_sroot(i) = 0.5 + i;
    //}

    // New code, import discontinuity locations from XML file
    roots = RootManager(discontinuities.size(), 1, 0, 1e-10);
    roots.n_sactive = discontinuities.size();
    for (int i = 0; i < discontinuities.size(); i++) {
        roots.is_active(i) = 1;
        roots.t_sroot(i) = discontinuities[i];
    }

    // This will print out all of the variable names
    // stringVec stNames;
    // gds->getStateName(stNames, cDaeSolverMode);
    // cout << "\nVariable Names\n";
    // for(int ii; ii< stNames.size(); ii++)
    //{
    //   cout << stNames[ii] << "\n";
    //}
    // cout << "\n\n";
}

void EquationGridDyn::function(const Real t,
                               const Vector& y,
                               const Vector& dy,
                               const Vector& state,
                               Vector& Fydy)
{
    nb_calls++;
    // gds->residualFunction(t,y.GetData(),dy.GetData(),Fydy.GetData(),cDaeSolverMode);
    gds->residualFunction(t, y.GetData(), dy.GetData(), Fydy.GetData(), *mode);
}

void EquationGridDyn::jacobian_ypcdy(const Real t,
                                     const Vector& y,
                                     const Vector& dy,
                                     const Vector& state,
                                     const Real cj,
                                     Matrix& J)
{
    // J.dump(std::cout);
    nb_calls_jac++;
    SparseMatrix& pJ = dynamic_cast<SparseMatrix&>(J);
    paradaeArrayData a1(&pJ);
    // gds->jacobianFunction(t,y.GetData(),dy.GetData(),a1,cj,cDaeSolverMode);
    gds->jacobianFunction(t, y.GetData(), dy.GetData(), a1, cj, *mode);
    // static int switcheroo = 0;
    // if(switcheroo == 0)
    //{
    //   std::cout << "\n\n  Time value " << t << "\n\n";
    //   J.dump("new_jac.txt");
    //   switcheroo = 1;
    //}
}

void EquationGridDyn::init(const Real t, Vector& y)
{
    y.CopyData(y0);
}
/*
void EquationGridDyn::root_init_state(const Real t, Vector& state)
{
if (roots.is_active(0))
if (t>=roots.t_sroot(0))
  state(0)=1;
}
*/

}  // namespace griddyn::paradae
