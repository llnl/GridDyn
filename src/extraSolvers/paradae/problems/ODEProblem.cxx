/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ODEProblem.h"

#include "../equations/AllEquations.h"
#include "../timeintegrators/AllTimeIntegrators.h"
#include <iomanip>
#include <sstream>

using namespace std;
namespace griddyn {
namespace paradae {
    ODEProblem::ODEProblem(MPI_Comm comm_)
    {
        comm = comm_;
        MPI_Comm_rank(comm, &mpi_rank);
        equation = NULL;
        TI = NULL;
        output = NULL;
        print_solution = true;
    }

    // Note that this is a different SetEquation than was originally in paradae.
    // The ability to switch between different equations, and to read t0, Tmax, and
    // Nsteps from params.ini has been removed.
    void ODEProblem::SetEquation(Equation* equation_, Real t0, Real Tmax, int Nsteps)
    {
        equation = equation_;
        if (t0 >= 0) equation->SetT0(t0);
        if (Tmax >= 0) equation->SetTmax(Tmax);
        if (Nsteps >= 0) equation->SetNsteps(Nsteps);
    }

    ODEProblem::~ODEProblem()
    {
        if (output != NULL) delete output;
        if (equation != NULL) delete equation;
        if (TI != NULL) delete TI;
    }

    /*!
  Create the Time Integrator required. Possible keywords for Runge-Kutta methods are:
  <CENTER>
  |Keyword            | Related class  | Type          | Properties                           |
  |-------------------|----------------|---------------|--------------------------------------|
  |`RK_FEuler_1`      | ForwardEuler   | Runge-Kutta   | Explicit                             |
  |`RK_Ralston_2`     | Ralston        | Runge-Kutta   | Explicit                             |
  |`RK_ExpMidPoint_2` | ExpMidPoint    | Runge-Kutta   | Explicit                             |
  |`RK_ExpTrap_2`     | ExpTrapezoidal | Runge-Kutta   | Explicit                             |
  |`RK_Kutta_3`       | Kutta3         | Runge-Kutta   | Explicit                             |
  |`RK_Kutta_4`       | Kutta4         | Runge-Kutta   | Explicit                             |
  |`RK_ExpFE_12`      | FE_ExpTrap_12  | Runge-Kutta   | Explicit, variable time step         |
  |`RK_ExpBS_23`      | BogaSham_23    | Runge-Kutta   | Explicit, variable time step         |
  |`RK_ExpDP_45`      | DormPrince_45  | Runge-Kutta   | Explicit, variable time step         |
  |`RK_BEuler_1`      | BackwardEuler  | Runge-Kutta   | Implicit, DIRK                       |
  |`RK_ImpMidPoint_2` | ImpMidPoint    | Runge-Kutta   | Implicit, DIRK                       |
  |`RK_ImpTrap_2`     | ImpTrapezoidal | Runge-Kutta   | Implicit, DIRK                       |
  |`RK_Radau_3`       | Radau3         | Runge-Kutta   | Implicit                             |
  |`RK_Gauss_4`       | Gauss4         | Runge-Kutta   | Implicit                             |
  |`RK_Gauss_6`       | Gauss6         | Runge-Kutta   | Implicit                             |
  |`RK_Imp_12`        | ImpVarUnk_12   | Runge-Kutta   | Implicit, DIRK, variable time step   |
  |`RK_SDIRK_12`      | SDIRK_12       | Runge-Kutta   | Implicit, SDIRK, variable time step   |
  |`RK_ImpBi_23`      | Billington_23  | Runge-Kutta   | Implicit, SDIRK, variable time step   |
  |`RK_ImpCa_24`      | Cash_24        | Runge-Kutta   | Implicit, SDIRK, variable time step   |
  |`RK_ImpCa_34`      | Cash_34        | Runge-Kutta   | Implicit, SDIRK, variable time step   |
  |`RK_ImpFu_45`      | Fudziah_45     | Runge-Kutta   | Implicit, DIRK, variable time step   |
  |`BDF_1`            | BackwardDiff   | Backward diff | Implicit, variable time step         |
  |`BDF_2`            | BackwardDiff   | Backward diff | Implicit, variable time step         |
  |`BDF_3`            | BackwardDiff   | Backward diff | Implicit, variable time step         |
  |`BDF_4`            | BackwardDiff   | Backward diff | Implicit, variable time step         |
  |`BDF_5`            | BackwardDiff   | Backward diff | Implicit, variable time step         |
  |`BDF_6`            | BackwardDiff   | Backward diff | Implicit, variable time step         |
  </CENTER>
*/
    void ODEProblem::SetTimeIntegrator(const MapParam& param)
    {
        string stepper = param.GetStrParam("time_int");
        bool do_varstep = param.GetBoolParam("varstep");
        bool dense_mat = param.GetBoolParam("dense_mat", true);
        Real rtol = param.GetRealParam("rtol", 1e-6);
        SVector atol = param.GetVectorParam("atol", SVector(1, rtol));
        Real max_rfactor = param.GetRealParam("max_rfactor", 10.0);

        if (!strcmp(stepper.c_str(), "RK_FEuler_1"))
            TI = new ForwardEuler(equation);
        else if (!strcmp(stepper.c_str(), "RK_Ralston_2"))
            TI = new Ralston(equation);
        else if (!strcmp(stepper.c_str(), "RK_ExpMidPoint_2"))
            TI = new ExpMidPoint(equation);
        else if (!strcmp(stepper.c_str(), "RK_ExpTrap_2"))
            TI = new ExpTrapezoidal(equation);
        else if (!strcmp(stepper.c_str(), "RK_Kutta_3"))
            TI = new Kutta3(equation);
        else if (!strcmp(stepper.c_str(), "RK_Kutta_4"))
            TI = new Kutta4(equation);
        else if (!strcmp(stepper.c_str(), "RK_ExpFE_12"))
            TI = new FE_ExpTrap_12(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_ExpBS_23"))
            TI = new BogaSham_23(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_ExpDP_45"))
            TI = new DormPrince_45(equation, do_varstep);

        else if (!strcmp(stepper.c_str(), "RK_BEuler_1"))
            TI = new BackwardEuler(equation);
        else if (!strcmp(stepper.c_str(), "RK_ImpMidPoint_2"))
            TI = new ImpMidPoint(equation);
        else if (!strcmp(stepper.c_str(), "RK_ImpTrap_2"))
            TI = new ImpTrapezoidal(equation);
        else if (!strcmp(stepper.c_str(), "RK_Radau_3"))
            TI = new Radau3(equation);
        else if (!strcmp(stepper.c_str(), "RK_Gauss_4"))
            TI = new Gauss4(equation);
        else if (!strcmp(stepper.c_str(), "RK_Gauss_6"))
            TI = new Gauss6(equation);
        else if (!strcmp(stepper.c_str(), "RK_Imp_12"))
            TI = new ImpVarUnk_12(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_SDIRK_12"))
            TI = new SDIRK_12(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_ImpBi_23"))
            TI = new Billington_23(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_ImpCa_24"))
            TI = new Cash_24(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_ImpCa_34"))
            TI = new Cash_34(equation, do_varstep);
        else if (!strcmp(stepper.c_str(), "RK_ImpFu_45"))
            TI = new Fudziah_45(equation, do_varstep);

        else {
            bool force_fixedleading = param.GetBoolParam("bdf_fixedleading", false);
            if (!strcmp(stepper.c_str(), "BDF_1"))
                TI = new BackwardDiff(1, equation, do_varstep, force_fixedleading);
            else if (!strcmp(stepper.c_str(), "BDF_2"))
                TI = new BackwardDiff(2, equation, do_varstep, force_fixedleading);
            else if (!strcmp(stepper.c_str(), "BDF_3"))
                TI = new BackwardDiff(3, equation, do_varstep, force_fixedleading);
            else if (!strcmp(stepper.c_str(), "BDF_4"))
                TI = new BackwardDiff(4, equation, do_varstep, force_fixedleading);
            else if (!strcmp(stepper.c_str(), "BDF_5"))
                TI = new BackwardDiff(5, equation, do_varstep, force_fixedleading);
            else if (!strcmp(stepper.c_str(), "BDF_6"))
                TI = new BackwardDiff(6, equation, do_varstep, force_fixedleading);
            else {
                if (mpi_rank == 0) cerr << "Unknown type of time integrator : " << stepper << endl;
                throw -2;
            }
        }
        TI->SetRTol(rtol);
        TI->SetATol(atol);
        TI->SetDenseMatrix(dense_mat);
        TI->SetMaxRFactor(max_rfactor);

        int nmaxiter = param.GetIntParam("newton_maxiter", 100);
        Real ntol = param.GetRealParam("newton_tol", -1);
        bool nupdate_jac = param.GetBoolParam("newton_updatejac", false);
        TI->SetNewtonSolverOpt(nmaxiter, ntol, nupdate_jac);
    }

    void ODEProblem::SetOutputFile(const MapParam& param, string file)
    {
        print_solution = param.GetBoolParam("print_solution", true);
        print_all_iter_solution = param.GetBoolParam("braid_print_all_iter", false);
        if (!strcmp(file.c_str(), "")) {
            ostringstream sfile;
            ostringstream num;
            num << setw(3) << setfill('0') << mpi_rank;
            sfile << equation->GetName() << "_";
            sfile << equation->GetTmax() << "_";
            sfile << TI->GetName() << "_";
            if (TI->DoVarstep())
                sfile << "varstep_";
            else
                sfile << "Nsteps-" << equation->GetNsteps() << "_";
            sfile << ((TI->DoBraid()) ? "braid_" : "seq_");
            sfile << num.str() << ".dat";
            file = sfile.str();
        }
        output_filename = file;

        if (print_solution) {
            output = new ofstream;
            output->open(file.c_str());
            *output << setprecision(20);
        }
    }
}  // namespace paradae
}  // namespace griddyn
