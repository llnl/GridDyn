# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#
# GridDyn now expects SUNDIALS to be available as the tracked submodule in
# ThirdParty/sundials rather than downloading a separate source tree during
# configure.
#

set(sundials_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/sundials")
set(sundials_BINARY_DIR "${PROJECT_BINARY_DIR}/ThirdParty/sundials-v6")

if(NOT EXISTS "${sundials_SOURCE_DIR}/CMakeLists.txt")
    message(
        FATAL_ERROR
            "The SUNDIALS submodule was not found at ${sundials_SOURCE_DIR}. "
            "Initialize/update submodules before enabling the local SUNDIALS build."
    )
endif()

set(sundials_klu_module_dir "${sundials_SOURCE_DIR}/cmake/tpl")
set(sundials_klu_module "${sundials_klu_module_dir}/SundialsKLU.cmake")
set(sundials_klu_backup "${sundials_klu_module_dir}/SundialsKLUorig.cmake")

if(EXISTS "${sundials_klu_module}")
    if(NOT EXISTS "${sundials_klu_backup}")
        file(RENAME "${sundials_klu_module}" "${sundials_klu_backup}")
    endif()
    file(COPY "${PROJECT_SOURCE_DIR}/config/cmake/SundialsKLU.cmake"
         DESTINATION "${sundials_klu_module_dir}")
else()
    message(
        WARNING
            "Unable to find SUNDIALS KLU module at ${sundials_klu_module}; "
            "GridDyn will not override the bundled KLU detection logic."
    )
endif()

# Map deprecated SUNDIALS cache options to their current names if present from
# older build trees, then clear the deprecated cache keys to avoid warning spam.
set(_sundials_deprecated_options
    F2003_INTERFACE_ENABLE BUILD_FORTRAN_MODULE_INTERFACE
    MPI_ENABLE ENABLE_MPI
    OPENMP_ENABLE ENABLE_OPENMP
    OPENMP_DEVICE_ENABLE ENABLE_OPENMP_DEVICE
    SKIP_OPENMP_DEVICE_CHECK OPENMP_DEVICE_WORKS
    PTHREAD_ENABLE ENABLE_PTHREAD
    CUDA_ENABLE ENABLE_CUDA
    LAPACK_ENABLE ENABLE_LAPACK
    SUPERLUDIST_ENABLE ENABLE_SUPERLUDIST
    SUPERLUMT_ENABLE ENABLE_SUPERLUMT
    KLU_ENABLE ENABLE_KLU
    HYPRE_ENABLE ENABLE_HYPRE
    PETSC_ENABLE ENABLE_PETSC
    Trilinos_ENABLE ENABLE_TRILINOS
    RAJA_ENABLE ENABLE_RAJA
)
list(LENGTH _sundials_deprecated_options _sundials_opt_len)
math(EXPR _sundials_last_pair "${_sundials_opt_len} - 2")
foreach(_sundials_idx RANGE 0 ${_sundials_last_pair} 2)
    math(EXPR _sundials_new_idx "${_sundials_idx} + 1")
    list(GET _sundials_deprecated_options ${_sundials_idx} _old_var)
    list(GET _sundials_deprecated_options ${_sundials_new_idx} _new_var)
    if(DEFINED ${_old_var} AND (NOT DEFINED ${_new_var}))
        set(${_new_var} "${${_old_var}}" CACHE INTERNAL "")
    endif()
    unset(${_old_var} CACHE)
endforeach()

option(${PROJECT_NAME}_ENABLE_IDA "Enable IDA for use in the computation" ON)
option(${PROJECT_NAME}_ENABLE_CVODE "Enable Cvode for use in the computation" ON)
option(${PROJECT_NAME}_ENABLE_ARKODE "Enable arkode for use in the computation" OFF)
option(${PROJECT_NAME}_ENABLE_KINSOL "Enable kinsol for use in the computation" ON)

set(BUILD_CVODES OFF CACHE INTERNAL "")
set(BUILD_IDAS OFF CACHE INTERNAL "")

if(${PROJECT_NAME}_ENABLE_IDA)
    set(BUILD_IDA ON CACHE INTERNAL "")
else()
    set(BUILD_IDA OFF CACHE INTERNAL "")
endif()

if(${PROJECT_NAME}_ENABLE_KINSOL)
    set(BUILD_KINSOL ON CACHE INTERNAL "")
else()
    set(BUILD_KINSOL OFF CACHE INTERNAL "")
endif()

if(${PROJECT_NAME}_ENABLE_CVODE)
    set(BUILD_CVODE ON CACHE INTERNAL "")
else()
    set(BUILD_CVODE OFF CACHE INTERNAL "")
endif()

if(${PROJECT_NAME}_ENABLE_ARKODE)
    set(BUILD_ARKODE ON CACHE INTERNAL "")
else()
    set(BUILD_ARKODE OFF CACHE INTERNAL "")
endif()

set(EXAMPLES_ENABLE_C OFF CACHE INTERNAL "")
set(EXAMPLES_ENABLE_CXX OFF CACHE INTERNAL "")
set(EXAMPLES_INSTALL OFF CACHE INTERNAL "")
set(SUNDIALS_INDEX_SIZE 32 CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")

if(${PROJECT_NAME}_ENABLE_OPENMP_SUNDIALS)
    set(ENABLE_OPENMP ON CACHE INTERNAL "")
endif()

if(${PROJECT_NAME}_ENABLE_KLU)
    set(ENABLE_KLU ON CACHE INTERNAL "")
    set(KLU_WORKS ON CACHE INTERNAL "")
else()
    set(ENABLE_KLU OFF CACHE INTERNAL "")
endif()

add_subdirectory("${sundials_SOURCE_DIR}" "${sundials_BINARY_DIR}")

add_library(sundials_all INTERFACE)
target_include_directories(sundials_all INTERFACE
    $<BUILD_INTERFACE:${sundials_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${sundials_BINARY_DIR}/include>
)
add_library(SUNDIALS::SUNDIALS ALIAS sundials_all)

set(SUNDIALS_LIBRARIES
    sundials_nvecserial_static
    sundials_sunlinsolband_static
    sundials_sunlinsoldense_static
    sundials_sunlinsolpcg_static
    sundials_sunlinsolspbcgs_static
    sundials_sunlinsolspfgmr_static
    sundials_sunlinsolspgmr_static
    sundials_sunlinsolsptfqmr_static
    sundials_sunmatrixband_static
    sundials_sunmatrixdense_static
    sundials_sunmatrixsparse_static
    sundials_sunnonlinsolfixedpoint_static
    sundials_sunnonlinsolnewton_static
    sundials_nvecmanyvector_static
)

if(${PROJECT_NAME}_ENABLE_IDA)
    list(APPEND SUNDIALS_LIBRARIES sundials_ida_static)
endif()

if(${PROJECT_NAME}_ENABLE_KINSOL)
    list(APPEND SUNDIALS_LIBRARIES sundials_kinsol_static)
endif()

if(${PROJECT_NAME}_ENABLE_CVODE)
    list(APPEND SUNDIALS_LIBRARIES sundials_cvode_static)
endif()

if(${PROJECT_NAME}_ENABLE_ARKODE)
    list(APPEND SUNDIALS_LIBRARIES sundials_arkode_static)
endif()

set(_griddyn_sundials_targets)
foreach(_sd_target IN LISTS SUNDIALS_LIBRARIES)
    if(TARGET ${_sd_target})
        list(APPEND _griddyn_sundials_targets ${_sd_target})
    endif()
endforeach()
if(TARGET sundials_generic_static_obj)
    list(APPEND _griddyn_sundials_targets sundials_generic_static_obj)
endif()
if(_griddyn_sundials_targets)
    set_target_properties(${_griddyn_sundials_targets} PROPERTIES FOLDER sundials)
endif()

target_link_libraries(sundials_all INTERFACE ${SUNDIALS_LIBRARIES})

if(TARGET sundials_nvecopenmp_static)
    set_target_properties(sundials_nvecopenmp_static PROPERTIES FOLDER sundials)
    target_link_libraries(sundials_all INTERFACE sundials_nvecopenmp_static)
endif()

if(TARGET sundials_sunlinsolklu_static)
    set_target_properties(sundials_sunlinsolklu_static PROPERTIES FOLDER sundials)
    target_link_libraries(sundials_all INTERFACE sundials_sunlinsolklu_static)
endif()

if(MSVC AND TARGET sundials_cvode_static)
    target_compile_options(sundials_cvode_static PRIVATE "/sdl-" "/W3")
endif()
