# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#
# GridDyn now expects SUNDIALS to be available as the tracked submodule in ThirdParty/sundials
# rather than downloading a separate source tree during configure.
#

set(sundials_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/sundials")
set(sundials_BINARY_DIR "${PROJECT_BINARY_DIR}/ThirdParty/sundials-v6")

if(NOT EXISTS "${sundials_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The SUNDIALS submodule was not found at ${sundials_SOURCE_DIR}. "
                        "Initialize/update submodules before enabling the local SUNDIALS build."
    )
endif()

set(sundials_klu_module_dir "${sundials_SOURCE_DIR}/cmake/tpl")
set(sundials_klu_module "${sundials_klu_module_dir}/SundialsKLU.cmake")
set(sundials_klu_backup "${sundials_klu_module_dir}/SundialsKLUorig.cmake")
set(sundials_clang_tidy "${sundials_SOURCE_DIR}/.clang-tidy")
set(sundials_clang_tidy_backup "${sundials_SOURCE_DIR}/.clang-tidy.griddyn-orig")

if(EXISTS "${sundials_klu_module}")
    if(NOT EXISTS "${sundials_klu_backup}")
        file(RENAME "${sundials_klu_module}" "${sundials_klu_backup}")
    endif()
    file(COPY "${PROJECT_SOURCE_DIR}/config/cmake/SundialsKLU.cmake"
         DESTINATION "${sundials_klu_module_dir}"
    )
else()
    message(WARNING "Unable to find SUNDIALS KLU module at ${sundials_klu_module}; "
                    "GridDyn will not override the bundled KLU detection logic."
    )
endif()

# clang-tidy walks source directories for nested configuration files. The
# SUNDIALS submodule ships a .clang-tidy that uses options unsupported by the
# clang-tidy version in GridDyn CI, so hide it during configure in the same way
# we already override the KLU helper module above.
if(EXISTS "${sundials_clang_tidy}")
    if(NOT EXISTS "${sundials_clang_tidy_backup}")
        file(RENAME "${sundials_clang_tidy}" "${sundials_clang_tidy_backup}")
    endif()
else()
    message(STATUS "SUNDIALS local .clang-tidy already hidden at ${sundials_clang_tidy_backup}")
endif()

# Clear deprecated SUNDIALS cache keys from older build trees to avoid warning spam.
set(_sundials_deprecated_cache_vars
    SUNDIALS_BUILD_WITH_MONITORING
    SUNDIALS_BUILD_WITH_PROFILING
    F2003_INTERFACE_ENABLE
    BUILD_FORTRAN_MODULE_INTERFACE
    BUILD_ARKODE
    BUILD_CVODE
    BUILD_CVODES
    BUILD_IDA
    BUILD_IDAS
    BUILD_KINSOL
    MPI_ENABLE
    ENABLE_MPI
    OPENMP_ENABLE
    ENABLE_OPENMP
    OPENMP_DEVICE_ENABLE
    ENABLE_OPENMP_DEVICE
    SKIP_OPENMP_DEVICE_CHECK
    OPENMP_DEVICE_WORKS
    PTHREAD_ENABLE
    ENABLE_PTHREAD
    CUDA_ENABLE
    ENABLE_CUDA
    ENABLE_HIP
    ENABLE_SYCL
    LAPACK_ENABLE
    ENABLE_LAPACK
    SUPERLUDIST_ENABLE
    ENABLE_SUPERLUDIST
    SUPERLUMT_ENABLE
    ENABLE_SUPERLUMT
    KLU_ENABLE
    ENABLE_KLU
    KLU_WORKS
    HYPRE_ENABLE
    ENABLE_HYPRE
    PETSC_ENABLE
    ENABLE_PETSC
    Trilinos_ENABLE
    ENABLE_TRILINOS
    RAJA_ENABLE
    ENABLE_RAJA
    ENABLE_GINKGO
    ENABLE_MAGMA
    ENABLE_XBRAID
    ENABLE_ONEMKL
    ENABLE_KOKKOS
    ENABLE_KOKKOS_KERNELS
    EXAMPLES_ENABLE_C
    EXAMPLES_ENABLE_CXX
    EXAMPLES_ENABLE_CUDA
    EXAMPLES_ENABLE_F2003
    BUILD_NVECTOR_MANYVECTOR
    BUILD_BENCHMARKS
    SUNDIALS_ENABLE_NVECTOR_MANYVECTOR
    BUILD_SUNLINSOL_KLU
    SUNDIALS_ENABLE_SUNLINSOL_KLU
    ENABLE_ALL_WARNINGS
    ENABLE_WARNINGS_AS_ERRORS
    ENABLE_ADDRESS_SANITIZER
    SUNDIALS_TEST_DEVTESTS
    SUNDIALS_TEST_UNITTESTS
    SUNDIALS_TEST_NODIFF
    SUNDIALS_TEST_PROFILE
)
foreach(_sundials_cache_var IN LISTS _sundials_deprecated_cache_vars)
    unset(${_sundials_cache_var} CACHE)
endforeach()

option(${PROJECT_NAME}_ENABLE_IDA "Enable IDA for use in the computation" ON)
option(${PROJECT_NAME}_ENABLE_CVODE "Enable Cvode for use in the computation" ON)
option(${PROJECT_NAME}_ENABLE_ARKODE "Enable arkode for use in the computation" OFF)
option(${PROJECT_NAME}_ENABLE_KINSOL "Enable kinsol for use in the computation" ON)

set(SUNDIALS_ENABLE_CVODES OFF CACHE INTERNAL "")
set(SUNDIALS_ENABLE_IDAS OFF CACHE INTERNAL "")
set(SUNDIALS_ENABLE_IDA ${${PROJECT_NAME}_ENABLE_IDA} CACHE INTERNAL "")
set(SUNDIALS_ENABLE_KINSOL ${${PROJECT_NAME}_ENABLE_KINSOL} CACHE INTERNAL "")
set(SUNDIALS_ENABLE_CVODE ${${PROJECT_NAME}_ENABLE_CVODE} CACHE INTERNAL "")
set(SUNDIALS_ENABLE_ARKODE ${${PROJECT_NAME}_ENABLE_ARKODE} CACHE INTERNAL "")

set(SUNDIALS_ENABLE_C_EXAMPLES OFF CACHE INTERNAL "")
set(SUNDIALS_ENABLE_CXX_EXAMPLES OFF CACHE INTERNAL "")
set(EXAMPLES_INSTALL OFF CACHE INTERNAL "")
set(SUNDIALS_INDEX_SIZE 32 CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
set(SUNDIALS_ENABLE_NVECTOR_MANYVECTOR ON CACHE INTERNAL "")
set(SUNDIALS_ENABLE_SUNLINSOL_KLU ${${PROJECT_NAME}_ENABLE_KLU} CACHE INTERNAL "")
set(SUNDIALS_TEST_ENABLE_DEV_TESTS OFF CACHE INTERNAL "")
set(SUNDIALS_TEST_ENABLE_UNIT_TESTS OFF CACHE INTERNAL "")
set(SUNDIALS_TEST_ENABLE_DIFF_OUTPUT OFF CACHE INTERNAL "")
set(SUNDIALS_TEST_ANSWER_DIR "" CACHE INTERNAL "")

if(${PROJECT_NAME}_ENABLE_OPENMP_SUNDIALS)
    set(SUNDIALS_ENABLE_OPENMP ON CACHE INTERNAL "")
endif()

if(${PROJECT_NAME}_ENABLE_KLU)
    set(SUNDIALS_ENABLE_KLU ON CACHE INTERNAL "")
    set(SUNDIALS_ENABLE_KLU_CHECKS ON CACHE INTERNAL "")
else()
    set(SUNDIALS_ENABLE_KLU OFF CACHE INTERNAL "")
endif()

add_subdirectory("${sundials_SOURCE_DIR}" "${sundials_BINARY_DIR}")

function(griddyn_prefix_sundials_folders directory_path)
    get_property(_dir_targets DIRECTORY "${directory_path}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_dir_target IN LISTS _dir_targets)
        get_target_property(_current_folder "${_dir_target}" FOLDER)
        if(NOT _current_folder OR _current_folder STREQUAL "_current_folder-NOTFOUND")
            set(_new_folder "sundials")
        elseif(_current_folder MATCHES "^sundials($|/)")
            set(_new_folder "${_current_folder}")
        else()
            set(_new_folder "sundials/${_current_folder}")
        endif()
        set_target_properties("${_dir_target}" PROPERTIES FOLDER "${_new_folder}")
    endforeach()

    get_property(_subdirs DIRECTORY "${directory_path}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        griddyn_prefix_sundials_folders("${_subdir}")
    endforeach()
endfunction()

griddyn_prefix_sundials_folders("${sundials_BINARY_DIR}")

add_library(sundials_all INTERFACE)
target_include_directories(
    sundials_all INTERFACE $<BUILD_INTERFACE:${sundials_SOURCE_DIR}/include>
                           $<BUILD_INTERFACE:${sundials_BINARY_DIR}/include>
)
add_library(SUNDIALS::SUNDIALS ALIAS sundials_all)

set(SUNDIALS_LIBRARIES
    sundials_core_static
    sundials_core_obj_static
    sundials_nvecserial_obj_static
    sundials_nvecserial_static
    sundials_nvecmanyvector_obj_static
    sundials_nvecmanyvector_static
    sundials_sunmatrixband_obj_static
    sundials_sunlinsolband_static
    sundials_sunlinsolband_obj_static
    sundials_sunmatrixdense_obj_static
    sundials_sunlinsoldense_static
    sundials_sunlinsoldense_obj_static
    sundials_sunlinsolpcg_static
    sundials_sunlinsolpcg_obj_static
    sundials_sunlinsolspbcgs_static
    sundials_sunlinsolspbcgs_obj_static
    sundials_sunlinsolspfgmr_static
    sundials_sunlinsolspfgmr_obj_static
    sundials_sunlinsolspgmr_static
    sundials_sunlinsolspgmr_obj_static
    sundials_sunlinsolsptfqmr_static
    sundials_sunlinsolsptfqmr_obj_static
    sundials_sunlinsolklu_obj_static
    sundials_sunmatrixband_static
    sundials_sunmatrixdense_static
    sundials_sunmatrixsparse_obj_static
    sundials_sunmatrixsparse_static
    sundials_sunmemsys_obj_static
    sundials_sunnonlinsolfixedpoint_obj_static
    sundials_sunnonlinsolfixedpoint_static
    sundials_sunnonlinsolnewton_obj_static
    sundials_sunnonlinsolnewton_static
)

if(${PROJECT_NAME}_ENABLE_IDA)
    list(APPEND SUNDIALS_LIBRARIES sundials_ida_obj_static)
    list(APPEND SUNDIALS_LIBRARIES sundials_ida_static)
endif()

if(${PROJECT_NAME}_ENABLE_KINSOL)
    list(APPEND SUNDIALS_LIBRARIES sundials_kinsol_obj_static)
    list(APPEND SUNDIALS_LIBRARIES sundials_kinsol_static)
endif()

if(${PROJECT_NAME}_ENABLE_CVODE)
    list(APPEND SUNDIALS_LIBRARIES sundials_cvode_obj_static)
    list(APPEND SUNDIALS_LIBRARIES sundials_cvode_static)
endif()

if(${PROJECT_NAME}_ENABLE_ARKODE)
    list(APPEND SUNDIALS_LIBRARIES sundials_arkode_obj_static)
    list(APPEND SUNDIALS_LIBRARIES sundials_arkode_static)
endif()

set_target_properties(sundials_all PROPERTIES FOLDER sundials)

target_link_libraries(sundials_all INTERFACE ${SUNDIALS_LIBRARIES})

if(TARGET sundials_nvecopenmp_static)
    target_link_libraries(sundials_all INTERFACE sundials_nvecopenmp_static)
endif()

if(TARGET sundials_sunlinsolklu_static)
    target_link_libraries(sundials_all INTERFACE sundials_sunlinsolklu_static)
endif()

if(MSVC AND TARGET sundials_cvode_static)
    target_compile_options(sundials_cvode_static PRIVATE "/sdl-" "/W3")
endif()
