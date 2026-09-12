# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#
# GridDyn uses the tracked HiGHS submodule for the native optimization solver. Configure only the
# C++ library; HiGHS applications, examples, tests, language bindings, and GPU support are not part
# of the GridDyn build.
#

set(highs_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/highs")
set(highs_BINARY_DIR "${PROJECT_BINARY_DIR}/ThirdParty/highs")

if(NOT EXISTS "${highs_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The HiGHS submodule was not found at ${highs_SOURCE_DIR}. "
                        "Initialize/update submodules before enabling the HiGHS build."
    )
endif()

# HiGHS uses several generic option names. Keep its build self-contained and avoid changing
# GridDyn's top-level BUILD_TESTING setting while configuring it.
set(FAST_BUILD ON)
set(BUILD_CXX ON)
set(BUILD_CXX_EXE OFF)
set(BUILD_EXAMPLES OFF)
set(_griddyn_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(BUILD_EXTRA_UNIT_TESTS OFF)
set(BUILD_EXTRA_PROBLEM_SET OFF)
set(BUILD_STATIC_EXE OFF)
set(BUILD_SHARED_EXTRAS_LIB OFF)
set(FORTRAN OFF)
set(CSHARP OFF)
set(HIPO OFF)
set(HIPO_PYTHON OFF)
set(CUPDLP_GPU OFF)
set(CUPDLP_FIND_CUDA OFF)
set(HIGHS_GPU_LIB OFF)
set(BUILD_OPENBLAS OFF)
set(ZLIB OFF)
set(HIGHS_COVERAGE OFF)

add_subdirectory("${highs_SOURCE_DIR}" "${highs_BINARY_DIR}")

# HiGHS is configured in the parent directory scope, so restore the GridDyn testing option before
# the remaining project subdirectories are processed.
set(BUILD_TESTING "${_griddyn_build_testing}")
unset(_griddyn_build_testing)

if(NOT TARGET highs::highs)
    message(FATAL_ERROR "The HiGHS submodule did not create the expected highs::highs target.")
endif()

# HiGHS compiles its library with position-independent code, but its CMake
# integration also exports POSITION_INDEPENDENT_CODE=ON as an interface
# requirement. GridDyn intentionally mixes PIC and non-PIC targets, so
# propagating that requirement makes otherwise valid consumers fail during
# CMake generation. HiGHS itself retains POSITION_INDEPENDENT_CODE=ON; only
# remove the unnecessary consumer-side requirement.
set_property(TARGET highs PROPERTY INTERFACE_POSITION_INDEPENDENT_CODE "")

add_library(griddyn_highs INTERFACE)
target_link_libraries(griddyn_highs INTERFACE highs::highs highs_extras)
add_library(GridDyn::highs ALIAS griddyn_highs)

foreach(_highs_target IN ITEMS highs highs_extras)
    if(TARGET ${_highs_target})
        set_target_properties(${_highs_target} PROPERTIES FOLDER Extern/HiGHS)
        target_compile_options(
            ${_highs_target} PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-error>
        )
    endif()
endforeach()

unset(_highs_target)
