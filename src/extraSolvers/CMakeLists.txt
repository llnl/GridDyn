# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2018-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(extra_solver_sources extraSolvers.cpp)

set(extra_solver_headers extraSolvers.h)

set(paradae_headers
    paradae/common/def.h
    paradae/equations/AllEquations.h
    paradae/equations/Equation.h
    paradae/equations/Equation_DAE.h
    paradae/equations/Equation_DAE_full.h
    paradae/equations/Equation_ODE.h
    paradae/equations/EqGridDyn.h
    paradae/math/DBlockTriMatrix.h
    paradae/math/DenseMatrix.h
    paradae/math/IPoly.h
    paradae/math/IVander.h
    paradae/math/IVanderExt.h
    paradae/math/PMultiVector.h
    paradae/math/PVector.h
    paradae/math/SBlockTriMatrix.h
    paradae/math/SMultiVector.h
    paradae/math/SVector.h
    paradae/math/SparseMatrix.h
    paradae/math/Vector.h
    paradae/math/VirtualMatrix.h
    paradae/math/VirtualVector.h
    paradae/math/paradaeArrayData.h
    paradae/problems/ODEProblem.h
    paradae/solvers/LineSearch.h
    paradae/solvers/Newton.h
    paradae/solvers/NewtonStats.h
    paradae/solvers/Solver.h
    paradae/timeintegrators/AllTimeIntegrators.h
    paradae/timeintegrators/BackwardDiff.h
    paradae/timeintegrators/BackwardEuler.h
    paradae/timeintegrators/Billington_23.h
    paradae/timeintegrators/BogaSham_23.h
    paradae/timeintegrators/Cash_24.h
    paradae/timeintegrators/Cash_34.h
    paradae/timeintegrators/DormPrince_45.h
    paradae/timeintegrators/ExpMidPoint.h
    paradae/timeintegrators/ExpTrapezoidal.h
    paradae/timeintegrators/FE_ExpTrap_12.h
    paradae/timeintegrators/ForwardEuler.h
    paradae/timeintegrators/Fudziah_45.h
    paradae/timeintegrators/Gauss4.h
    paradae/timeintegrators/Gauss6.h
    paradae/timeintegrators/ImpMidPoint.h
    paradae/timeintegrators/ImpTrapezoidal.h
    paradae/timeintegrators/ImpVarUnk_12.h
    paradae/timeintegrators/Kutta3.h
    paradae/timeintegrators/Kutta4.h
    paradae/timeintegrators/Radau3.h
    paradae/timeintegrators/Ralston.h
    paradae/timeintegrators/RungeKutta.h
    paradae/timeintegrators/RungeKutta_DIRK.h
    paradae/timeintegrators/RungeKutta_Explicit.h
    paradae/timeintegrators/RungeKutta_Implicit.h
    paradae/timeintegrators/RungeKutta_SDIRK.h
    paradae/timeintegrators/SDIRK_12.h
    paradae/timeintegrators/TimeIntegrator.h
)

set(paradae_sources
    paradae/common/def.cxx
    paradae/equations/Equation.cxx
    paradae/equations/Equation_ODE.cxx
    paradae/equations/EqGridDyn.cxx
    paradae/math/DBlockTriMatrix.cxx
    paradae/math/DenseMatrix.cxx
    paradae/math/IPoly.cxx
    paradae/math/IVander.cxx
    paradae/math/IVanderExt.cxx
    paradae/math/PMultiVector.cxx
    paradae/math/PVector.cxx
    paradae/math/SBlockTriMatrix.cxx
    paradae/math/SMultiVector.cxx
    paradae/math/SVector.cxx
    paradae/math/SparseMatrix.cxx
    paradae/math/Vector.cxx
    paradae/math/VirtualMatrix.cxx
    paradae/math/paradaeArrayData.cxx
    paradae/problems/ODEProblem.cxx
    paradae/solvers/LineSearch.cxx
    paradae/solvers/Newton.cxx
    paradae/solvers/NewtonStats.cxx
    paradae/solvers/Solver.cxx
    paradae/timeintegrators/BackwardDiff.cxx
    paradae/timeintegrators/BackwardEuler.cxx
    paradae/timeintegrators/Billington_23.cxx
    paradae/timeintegrators/BogaSham_23.cxx
    paradae/timeintegrators/Cash_24.cxx
    paradae/timeintegrators/Cash_34.cxx
    paradae/timeintegrators/DormPrince_45.cxx
    paradae/timeintegrators/ExpMidPoint.cxx
    paradae/timeintegrators/ExpTrapezoidal.cxx
    paradae/timeintegrators/FE_ExpTrap_12.cxx
    paradae/timeintegrators/ForwardEuler.cxx
    paradae/timeintegrators/Fudziah_45.cxx
    paradae/timeintegrators/Gauss4.cxx
    paradae/timeintegrators/Gauss6.cxx
    paradae/timeintegrators/ImpMidPoint.cxx
    paradae/timeintegrators/ImpTrapezoidal.cxx
    paradae/timeintegrators/ImpVarUnk_12.cxx
    paradae/timeintegrators/Kutta3.cxx
    paradae/timeintegrators/Kutta4.cxx
    paradae/timeintegrators/Radau3.cxx
    paradae/timeintegrators/Ralston.cxx
    paradae/timeintegrators/RungeKutta.cxx
    paradae/timeintegrators/RungeKutta_DIRK.cxx
    paradae/timeintegrators/RungeKutta_Explicit.cxx
    paradae/timeintegrators/RungeKutta_Implicit.cxx
    paradae/timeintegrators/RungeKutta_SDIRK.cxx
    paradae/timeintegrators/SDIRK_12.cxx
    paradae/timeintegrators/TimeIntegrator.cxx
)

option(BUILD_BRAID_SOLVER "enable the braid parallel in time solver integration" OFF)
option(BUILD_SUPERLUMT_SOLVER "enable a superLU solver" OFF)
option(BUILD_PARADAE_SOLVERS "enable inclusion of PARADAE solvers" OFF)

set(GRIDDYN_ENABLE_PARADAE_SOLVERS OFF)
set(GRIDDYN_ENABLE_BRAID_SOLVER OFF)

if(BUILD_PARADAE_SOLVERS OR BUILD_BRAID_SOLVER)
    find_package(LAPACKE QUIET)
    if(NOT LAPACKE_FOUND)
        message(
            WARNING
                "PARADAE and Braid solvers require LAPACKE; extra solver support will be limited"
        )
    endif()
endif()

if(BUILD_PARADAE_SOLVERS AND LAPACKE_FOUND)
    list(APPEND extra_solver_sources ${paradae_sources})
    list(APPEND extra_solver_headers ${paradae_headers})
    set(GRIDDYN_ENABLE_PARADAE_SOLVERS ON)
elseif(BUILD_PARADAE_SOLVERS)
    message(WARNING "PARADAE solvers requested but LAPACKE was not found; skipping PARADAE solvers")
endif()

if(BUILD_BRAID_SOLVER)
    find_package(Braid QUIET)
    if(NOT GRIDDYN_ENABLE_MPI)
        message(WARNING "Braid requires MPI support; skipping Braid solver")
    elseif(NOT BRAID_FOUND)
        message(WARNING "Braid solver requested but Braid was not found; skipping Braid solver")
    elseif(NOT LAPACKE_FOUND)
        message(WARNING "Braid solver requested but LAPACKE was not found; skipping Braid solver")
    else()
        if(NOT GRIDDYN_ENABLE_PARADAE_SOLVERS)
            list(APPEND extra_solver_sources ${paradae_sources})
            list(APPEND extra_solver_headers ${paradae_headers})
            set(GRIDDYN_ENABLE_PARADAE_SOLVERS ON)
        endif()
        list(APPEND extra_solver_sources braid/braidSolver.cpp braid/braid_driver.cxx)
        list(APPEND extra_solver_headers braid/braidInterface.h braid/braid_driver.h)
        set(GRIDDYN_ENABLE_BRAID_SOLVER ON)
    endif()
endif()

if(GRIDDYN_ENABLE_MPI AND GRIDDYN_ENABLE_PARADAE_SOLVERS)
    list(APPEND extra_solver_sources paradae/common/MapParam.cxx paradae/common/Timer.cxx)
    list(APPEND extra_solver_headers paradae/common/def_mpi.h paradae/common/MapParam.h
         paradae/common/Timer.h
    )
endif()

add_library(extraSolverLibrary STATIC ${extra_solver_sources} ${extra_solver_headers})
set_target_properties(extraSolverLibrary PROPERTIES FOLDER libraries)

target_link_libraries(extraSolverLibrary PUBLIC griddyn)
target_link_libraries(extraSolverLibrary PUBLIC ${SuiteSparse_UMFPACK_LIBRARY_RELEASE})

if(LAPACKE_FOUND)
    target_link_libraries(extraSolverLibrary PUBLIC ${LAPACKE_LIBRARIES})
    target_include_directories(extraSolverLibrary PRIVATE ${LAPACKE_INCLUDE_DIRS})
endif()

if(GRIDDYN_ENABLE_PARADAE_SOLVERS)
    # source_group("paradae" FILES ${paradae_sources} ${paradae_headers})
    source_group("paradae\\timeIntegrators" REGULAR_EXPRESSION "paradae/timeintegrators/.*")
    source_group("paradae\\math" REGULAR_EXPRESSION "paradae/math/.*")
    source_group("paradae\\common" REGULAR_EXPRESSION "paradae/common/.*")
    source_group("paradae\\solvers" REGULAR_EXPRESSION "paradae/solvers/.*")
    source_group("paradae\\equations" REGULAR_EXPRESSION "paradae/equations/.*")
endif()

if(GRIDDYN_ENABLE_BRAID_SOLVER)
    target_link_libraries(extraSolverLibrary PUBLIC braid::braid)
    source_group("braid" REGULAR_EXPRESSION "braid/.*")
    target_compile_definitions(extraSolverLibrary PUBLIC "-DENABLE_BRAID")
endif()

if(GRIDDYN_ENABLE_MPI AND GRIDDYN_ENABLE_BRAID_SOLVER)
    target_link_libraries(extraSolverLibrary PUBLIC MPI::MPI_C)
endif()

target_include_directories(extraSolverLibrary PRIVATE ${SuiteSparse_DIRECT_INCLUDE_DIR})

# install( TARGETS extraSolverLibrary EXPORT griddyn-targets DESTINATION ${CMAKE_INSTALL_LIBDIR}
# COMPONENT libraries )

install(FILES ${extra_solver_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn/extraSolver
        COMPONENT headers
)

list(APPEND optional_library_key_headers extraSolvers/extraSolvers.h)
list(APPEND optional_library_functions loadExtraSolvers)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)
