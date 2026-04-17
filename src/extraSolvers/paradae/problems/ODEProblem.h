/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../common/MapParam.h"
#include "../common/def.h"
#include "../equations/Equation.h"
#include "../timeintegrators/TimeIntegrator.h"
#include "mpi.h"
#include <fstream>
#include <string>

enum ODE_error { ODE_FAILED };
namespace griddyn::paradae {
class ODEProblem {
    Equation* equation;
    TimeIntegrator* TI;
    std::string output_filename;
    std::ofstream* output;

    MPI_Comm comm;
    int mpi_rank;
    bool print_solution;
    bool print_all_iter_solution;

  public:
    ODEProblem(MPI_Comm comm_);
    ~ODEProblem();
    void SetEquation(const MapParam& param);
    void SetEquation(Equation* equation_, Real t0, Real Tmax, int Nsteps);
    void SetTimeIntegrator(const MapParam& param);
    void SetOutputFile(const MapParam& param, std::string file = "");
    inline int GetM() { return equation->GetM(); };
    inline Equation* GetEq() { return equation; };
    inline TimeIntegrator* GetTI() { return TI; };
    inline std::ofstream* GetOutput() { return output; };
    inline std::string GetOutputFilename() { return output_filename; };
    inline MPI_Comm GetComm() { return comm; };
    inline bool PrintSolution() { return print_solution; };
    inline bool PrintAllITERSolution() { return print_all_iter_solution; };
};
}  // namespace griddyn::paradae
