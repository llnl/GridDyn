/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../paradae/common/MapParam.h"
#include "../paradae/common/def.h"
#include "../paradae/equations/EqGridDyn.h"
#include "../paradae/equations/Equation.h"
#include "../paradae/math/Vector.h"
#include "../paradae/math/paradaeArrayData.h"
#include "../paradae/problems/ODEProblem.h"
#include "../paradae/timeintegrators/TimeIntegrator.h"
#include "braidInterface.h"
#include "braid_driver.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/string_viewConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/solvers/solverInterface.h"
#include "mpi.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <string>

namespace griddyn::braid {
using griddyn::paradae::BackwardDiff;
using griddyn::paradae::BDF;
using griddyn::paradae::Equation;
using griddyn::paradae::EquationGridDyn;
using griddyn::paradae::MapParam;
using griddyn::paradae::ODE;
using griddyn::paradae::ODEProblem;
using griddyn::paradae::PVector;
using griddyn::paradae::RCODE;
using griddyn::paradae::Real;
using griddyn::paradae::SparseMatrix;
using griddyn::paradae::SVector;
using griddyn::paradae::TimeIntegrator;
using std::abort;
using std::ceil;
using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;
using std::istringstream;
using std::list;
using std::make_unique;
using std::max;
using std::memcpy;
using std::sort;
using std::string;
//-------------------------------------------------------------------
//-------------------------------------------------------------------
// Functions taken from Matt's paradae/src/programs/rk_braid.cxx
//-------------------------------------------------------------------

void BuildGrid(ODEProblem* ode, const MapParam& param, int& Nsteps, Real*& timegrid)
{
    string gridfile = param.GetStrParam("gridfile", "");
    if (gridfile.empty()) {
        Real t0 = ode->GetEq()->GetT0();
        Real Tmax = ode->GetEq()->GetTmax();
        timegrid = new Real[Nsteps + 1 + ode->GetEq()->GetNSRoots()];
        timegrid[0] = t0;

        Real dt = (Tmax - t0) / Nsteps;
        Real t;
        int idx = 1;
        bool scheduled_root;
        int iroot;
        for (int i = 1; i < Nsteps; i++) {
            t = t0 + i * dt;
            scheduled_root = ode->GetEq()->CheckScheduledRoots(timegrid[idx - 1], t, iroot);
            timegrid[idx] = t;
            idx++;
            if (scheduled_root && abs(t - t0 - i * dt) > 1e-12) i--;
        }
        Nsteps = idx;
        timegrid[idx] = Tmax;
    } else {
        int refine_by = param.GetIntParam("refine_grid_by", 1);
        if (!ode->GetTI()->DoBraid()) ode->GetTI()->DoFalseVarstep() = true;

        list<Real> timelist;
        ifstream infile(gridfile);
        string line;
        Real t;
        Nsteps = 0;
        while (getline(infile, line)) {
            istringstream ss(line);
            ss >> t;
            timelist.push_back(t);
        }
        Nsteps = timelist.size();
        int Nfsteps = refine_by * (Nsteps - 1) + 1;
        if (Nfsteps <= 1) {
            cerr << "Error reading the file " << gridfile << " for time grid." << endl;
            abort();
        }
        timegrid = new Real[Nfsteps];
        list<Real>::const_iterator it = timelist.begin();
        int idx = 0;
        timegrid[idx] = *it;
        for (it++; it != timelist.end(); it++) {
            Real dt = (*it - timegrid[idx]) / refine_by;
            for (int i = 1; i < refine_by; i++)
                timegrid[idx + i] = timegrid[idx] + i * dt;
            timegrid[idx + refine_by] = *it;
            idx += refine_by;
        }
        Nsteps = Nfsteps - 1;
    }
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------

// TODO(phlpt): Confirm whether setting mode.algebraic is sufficient here.
braidSolver::braidSolver(const std::string& objName): SolverInterface(objName)
{
    mode.dynamic = true;
    mode.differential = true;
    mode.algebraic = true;
    flags[block_mode_only] = true;
}
braidSolver::braidSolver(gridDynSimulation* gds, const solverMode& sMode):
    SolverInterface(gds, sMode)
{
}

std::unique_ptr<SolverInterface> braidSolver::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<braidSolver>();
    braidSolver::cloneTo(si.get(), fullCopy);
    return si;
}

void braidSolver::cloneTo(SolverInterface* si, bool fullCopy) const
{
    SolverInterface::cloneTo(si, fullCopy);
    auto bos = dynamic_cast<braidSolver*>(si);
    if (bos == nullptr) {
        return;
    }
    bos->deltaT = deltaT;
}

double* braidSolver::state_data() noexcept
{
    return state.data();
}
double* braidSolver::deriv_data() noexcept
{
    return deriv.data();
}
double* braidSolver::type_data() noexcept
{
    return type.data();
}
const double* braidSolver::state_data() const noexcept
{
    return state.data();
}
const double* braidSolver::deriv_data() const noexcept
{
    return deriv.data();
}
const double* braidSolver::type_data() const noexcept
{
    return type.data();
}

void braidSolver::allocate(count_t stateCount, count_t numRoots)
{
    // load the vectors
    if (stateCount != svsize) {
        state.resize(stateCount);
        deriv.resize(stateCount);
        state2.resize(stateCount);
        svsize = stateCount;
        flags.reset(initialized_flag);
        flags.set(allocated_flag);
    }
    rootCount = numRoots;
    rootsfound.resize(numRoots);
}

void braidSolver::initialize(coreTime t0)
{
    if (!flags[allocated_flag]) {
        throw(InvalidSolverOperation(-2));
    }
    flags.set(initialized_flag);
    solverCallCount = 0;
    solveTime = t0;

    // USE  EquationGridDyn(t0_,Tmax_,N_unistep_,gds_,y0_);
    int n = m_gds->stateSize(mode);  // cDaeSolverMode);
    SVector y0_(n, 0.0), y0p_(n, 0.0);

    // Compute global time domain values
    double stopTime = static_cast<double>(m_gds->getStopTime());
    tStart = t0;
    double dt = static_cast<double>(m_gds->getStepTime());
    int N_unistep_ = ceil((stopTime - static_cast<double>(tStart)) / dt);

    m_gds->guessState(t0, y0_.GetData(), y0p_.GetData(), mode);  // cDaeSolverMode);
    // y0_.dump("new_code_init_con.txt");
    equation = new EquationGridDyn(static_cast<double>(tStart),
                                   stopTime,
                                   N_unistep_,
                                   m_gds,
                                   y0_,
                                   &mode,
                                   discontinuities,
                                   rootsfound);
}

double braidSolver::get(std::string_view param) const
{
    if (param == "deltat") {
        return deltaT;
    } else {
        return SolverInterface::get(param);
    }
}
void braidSolver::set(std::string_view param, std::string_view val)
{
    if ((param == "configfile") || (param == "file") || (param == "config_file")) {
        configFile = std::string{val};
    } else if (param == "discontinuities") {
        discontinuities = gmlc::utilities::str2vector<double>(std::string{val}, 0.0);
        std::sort(discontinuities.begin(), discontinuities.end(), std::less<>());
    } else {
        SolverInterface::set(param, val);
    }
}
void braidSolver::set(std::string_view param, double val)
{
    if ((param == "delta") || (param == "deltat") || (param == "step") || (param == "steptime")) {
        deltaT = val;
    } else {
        SolverInterface::set(param, val);
    }
}

int braidSolver::RunBraid(ODEProblem* ode, MapParam* param, Real*& timegrid, int Ngridpoints)
{
    TimeIntegrator* TI = ode->GetTI();
    Equation* equation = ode->GetEq();
    BDF_STRAT bdf_strat = nobdf;
    int lowered_by_level = 1;
    int min_order = 1;

    Real t0 = equation->GetT0();
    Real Tmax = equation->GetTmax();
    int Nsteps = Ngridpoints - 1;
    int braid_Nsteps = Nsteps;
    Real udt = (Tmax - t0) / Nsteps;

    ode->SetOutputFile(*param);

    Braid_App* app = new Braid_App(ode);
    Real* braid_timegrid = timegrid;
    if (TI->GetType() == BDF) {
        int toadd = (TI->GetNbSteps() - (Ngridpoints % TI->GetNbSteps())) % TI->GetNbSteps();
        Tmax += udt * toadd;
        Nsteps += toadd;

        equation->SetTmax(Tmax);
        equation->SetNsteps(Nsteps);
        braid_Nsteps = (Nsteps) / TI->GetNbSteps();
        if (toadd > 0) {
            Real* new_timegrid = new Real[Nsteps + 1];
            memcpy(new_timegrid, timegrid, Ngridpoints * sizeof(Real));
            for (int i = 0; i < toadd - 1; i++)
                new_timegrid[Ngridpoints + i] = new_timegrid[Ngridpoints + i - 1] + udt;
            new_timegrid[Ngridpoints + toadd - 1] = Tmax;
            delete[] timegrid;
            timegrid = new_timegrid;
        }
        braid_timegrid = new Real[braid_Nsteps + 1];
        for (int i = 0; i < braid_Nsteps + 1; i++)
            braid_timegrid[i] = timegrid[i * TI->GetNbSteps()];

        string str_bdf_strat = param->GetStrParam("braid_bdf_strat", "usual-c");
        if (!strcmp(str_bdf_strat.c_str(), "usual"))
            bdf_strat = usual;
        else if (!strcmp(str_bdf_strat.c_str(), "usual-c"))
            bdf_strat = usual_c;
        else if (!strcmp(str_bdf_strat.c_str(), "inject"))
            bdf_strat = inject;
        else if (!strcmp(str_bdf_strat.c_str(), "inject-c"))
            bdf_strat = inject_c;
        else if (!strcmp(str_bdf_strat.c_str(), "extrap"))
            bdf_strat = extrap;
        else if (!strcmp(str_bdf_strat.c_str(), "extrap-c"))
            bdf_strat = extrap_c;
        else if (!strcmp(str_bdf_strat.c_str(), "uni0"))
            bdf_strat = uni0;
        else if (!strcmp(str_bdf_strat.c_str(), "uni0-c"))
            bdf_strat = uni0_c;
        else if (!strcmp(str_bdf_strat.c_str(), "uni1"))
            bdf_strat = uni1;
        else if (!strcmp(str_bdf_strat.c_str(), "uni1-c"))
            bdf_strat = uni1_c;
        else {  // NOLINT
            cerr << "Wrong bdf strategy" << endl;
            abort();
        }

        lowered_by_level = param->GetIntParam("braid_bdf_loweredbylvl", 1);
        if (lowered_by_level < 0) lowered_by_level = 0;

        min_order = param->GetIntParam("braid_bdf_minorder", 1);
        if (min_order < 1) min_order = 1;
        if (min_order > app->nb_multisteps) min_order = app->nb_multisteps;

        /*
       do_bdf_uniform=param->GetBoolParam("braid_bdf_uniform",false);
       do_lowerorder=param->GetBoolParam("braid_bdf_lowerorder",true);
       do_paracoarse=param->GetBoolParam("braid_bdf_paracoarse",false);
       do_interp=param->GetStrParam("braid_bdf_interp","none");
       if (do_bdf_uniform)
       do_interp="none";
       */
    }
    app->nb_initial = Ngridpoints;
    app->grid_initial = timegrid;
    app->braid_grid_initial = braid_timegrid;
    /*
   app->do_bdf_uniform=do_bdf_uniform;
   app->do_lowerorder=do_lowerorder;
   app->do_interp=do_interp;
   */
    app->bdf_strat = bdf_strat;
    app->lowered_by_level = lowered_by_level;
    app->min_order = min_order;

    braid_Core core;
    braid_Init(MPI_COMM_WORLD,
               ode->GetComm(),
               t0,
               Tmax,
               braid_Nsteps,
               app,
               my_Step,
               my_Init,
               my_Clone,
               my_Free,
               my_Sum,
               my_SpatialNorm,
               my_Access,
               my_BufSize,
               my_BufPack,
               my_BufUnpack,
               &core);
    braid_SetTimeGrid(core, my_TimeGrid);
    braid_SetSpatialRefine(core, my_SpatialRefine);
    braid_SetSpatialCoarsen(core, my_SpatialCoarsen);
    // braid_SetShell(core, my_InitShell, my_CloneShell, my_FreeShell, my_PropagateShell);
    braid_SetShell(core, my_InitShell, my_CloneShell, my_FreeShell);

    // User-specified parameters
    int cutoff = param->GetIntParam("braid_cutoff", 100000);
    int max_refinements = param->GetIntParam("braid_max_refinements", 10);
    int min_coarse = param->GetIntParam("braid_min_coarse", 16);
    int max_levels = param->GetIntParam("braid_max_lvl", 100);
    int cfactor = param->GetIntParam("braid_coarsening", 2);
    int cfactor0 = param->GetIntParam("braid_coarsening0", 2);
    int max_iter = param->GetIntParam("braid_max_iter", 100);
    bool fmg = param->GetBoolParam("braid_fmg", true);
    bool seq_soln = param->GetBoolParam("braid_seqsoln", false);
    Real tol = param->GetRealParam("braid_tol", 1e-10);
    int access_lvl = param->GetIntParam("braid_access_lvl", 1);
    int io_lvl = param->GetIntParam("braid_io_lvl", 1);
    int skip = param->GetIntParam("braid_skip", 1);
    braid_SetMaxRefinements(core, max_refinements);
    braid_SetTPointsCutoff(core, cutoff);
    braid_SetSeqSoln(core, seq_soln);
    braid_SetFileIOLevel(core, io_lvl);
    braid_SetMaxLevels(core, max_levels);
    braid_SetAbsTol(core, tol);
    braid_SetCFactor(core, -1, cfactor);
    braid_SetCFactor(core, 0, cfactor0);
    braid_SetMinCoarse(core, min_coarse);
    if (bdf_strat == usual || bdf_strat == inject || bdf_strat == extrap) {
        if (lowered_by_level == 0) {
            cerr << "Wrong lowered_by_level option with this BDF strategy" << endl;
            abort();
        }
        int nblvl = ceil(Real(TI->GetOrder() - min_order) / Real(lowered_by_level));
        for (int i = 0; i < nblvl; i++)
            braid_SetCFactor(core, i, 1);
    } else if (bdf_strat == uni0 || bdf_strat == uni1) {
        braid_SetCFactor(core, 0, 1);
    }
    braid_SetMaxIter(core, max_iter);
    braid_SetAccessLevel(core, access_lvl);
    braid_SetSkip(core, skip);
    if (fmg) {
        braid_SetFMG(core);
    }

    // Hard-coded parameters
    int nrelax = 1;
    int nrelax0 = -1;
    braid_SetStorage(core, -1);
    braid_SetPrintLevel(core, 2);
    braid_SetRefine(core, 1);
    braid_SetNRelax(core, -1, nrelax);
    if (nrelax0 > -1) {
        braid_SetNRelax(core, 0, nrelax0);
    }

    // Run Braid simulation
    braid_Drive(core);

    // Last processor owns the solution at t-final. Extract the t-final solution
    // and broadcast to all other processors
    MPI_Comm comm = ode->GetComm();
    int mpi_rank, mpi_size;
    MPI_Comm_rank(comm, &mpi_rank);
    MPI_Comm_size(comm, &mpi_size);
    braid_BufferStatus bstatus = (braid_BufferStatus)core;
    int bsize = -1;
    void* buffer;
    my_BufSize(app, &bsize, bstatus);
    buffer = malloc(bsize);
    if (mpi_rank == mpi_size - 1) {
        my_BufPack(app, app->solution_tfinal, buffer, bstatus);
    }
    MPI_Bcast(buffer, bsize, MPI_BYTE, mpi_size - 1, comm);
    if (mpi_rank < mpi_size - 1) {
        my_BufUnpack(app, buffer, &(app->solution_tfinal), bstatus);
    }
    // Move the data from the app to GridDyn internals
    double* data = app->solution_tfinal->xprev.GetData();
    state.insert(state.end(), &data[0], &data[ode->GetEq()->GetM()]);
    data = app->solution_tfinal->dxprev.GetData();
    deriv.insert(deriv.end(), &data[0], &data[ode->GetEq()->GetM()]);
    free(buffer);

    // Clean up
    braid_Destroy(core);
    if (TI->GetType() == BDF) delete[] braid_timegrid;
    delete app;

    return 0;
}

int braidSolver::solve(coreTime tStop, coreTime& tReturn, step_mode stepMode)
{
    int mpi_rank;
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_rank(comm, &mpi_rank);

    if (solveTime == tStop) {
        tReturn = tStop;
        return FUNCTION_EXECUTION_SUCCESS;
    }

    MapParam param(comm);
    param.ReadFile(configFile);

    int Nsteps = equation->GetNsteps();
    ODEProblem ode(comm);
    ode.SetEquation(equation, tStart, tStop, Nsteps);
    ode.SetTimeIntegrator(param);

    double* timegrid;
    ode.GetTI()->DoBraid() = true;
    BuildGrid(&ode, param, Nsteps, timegrid);
    this->RunBraid(&ode, &param, timegrid, Nsteps + 1);
    delete[] timegrid;

    // Begin diagnostic output on Braid run
    int loc_nbfuncalls = ode.GetEq()->GetNbFunCalls();
    int loc_nbjaccalls = ode.GetEq()->GetNbJacCalls();
    int loc_nbrootcalls = ode.GetEq()->GetNbRootCalls();
    int loc_nbstepsdone = ode.GetTI()->GetNbStepsDone();
    int nbfuncalls, nbjaccalls, nbrootcalls, nbstepsdone;
    MPI_Reduce(&loc_nbfuncalls, &nbfuncalls, 1, MPI_INT, MPI_SUM, 0, comm);
    MPI_Reduce(&loc_nbjaccalls, &nbjaccalls, 1, MPI_INT, MPI_SUM, 0, comm);
    MPI_Reduce(&loc_nbrootcalls, &nbrootcalls, 1, MPI_INT, MPI_SUM, 0, comm);
    MPI_Reduce(&loc_nbstepsdone, &nbstepsdone, 1, MPI_INT, MPI_SUM, 0, comm);

    if (ode.PrintSolution()) {
        ode.GetOutput()->close();
    }

    if (mpi_rank == 0) {
        cout << "Number of steps done : " << nbstepsdone << endl;
        cout << "Number of fun call   : " << nbfuncalls << endl;
        cout << "Number of jac call   : " << nbjaccalls << endl;
        cout << "Number of root call  : " << nbrootcalls << endl;
    }

    tReturn = tStop;
    return FUNCTION_EXECUTION_SUCCESS;
}

int braidSolver::calcIC(coreTime t0, coreTime tstep0, ic_modes mode, bool constraints)
{
    return 0;
}

void braidSolver::getRoots()
{
    if (equation == nullptr) {
        return;
    }
    const auto& rootData = equation->GetRoots();
    if (rootData.GetNURoots() != static_cast<int>(rootsfound.size())) {
        rootsfound.resize(rootData.GetNURoots());
    }
    for (int ii = 0; ii < rootData.GetNURoots(); ++ii) {
        rootsfound[ii] = static_cast<int>(rootData.iroot(rootData.n_sroots + ii));
    }
}
}  // namespace griddyn::braid
