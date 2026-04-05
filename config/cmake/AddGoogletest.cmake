# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Copyright (c) 2017-2026, Battelle Memorial Institute; Lawrence Livermore
# National Security, LLC; Alliance for Sustainable Energy, LLC.
# See the top-level NOTICE for additional details.
# All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#
# Build GoogleTest from the tracked submodule and provide a helper macro for registering GridDyn
# tests.
#

set(gtest_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/googletest")
set(gtest_BINARY_DIR "${PROJECT_BINARY_DIR}/ThirdParty/googletest")

if(NOT EXISTS "${gtest_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The googletest submodule was not found at ${gtest_SOURCE_DIR}. "
                        "Initialize/update submodules before enabling GoogleTest builds."
    )
endif()

set(gtest_force_shared_crt ON CACHE INTERNAL "")
set(BUILD_GMOCK ON CACHE INTERNAL "")
set(INSTALL_GTEST OFF CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(HAVE_STD_REGEX ON CACHE INTERNAL "")
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "")

add_subdirectory("${gtest_SOURCE_DIR}" "${gtest_BINARY_DIR}" EXCLUDE_FROM_ALL)

if(GOOGLE_TEST_INDIVIDUAL)
    include(GoogleTest)
endif()

# Target must already exist
macro(add_gtest TESTNAME)
    target_link_libraries(${TESTNAME} PUBLIC gtest gmock gtest_main)

    if(GOOGLE_TEST_INDIVIDUAL)
        gtest_discover_tests(${TESTNAME} TEST_PREFIX "${TESTNAME}." PROPERTIES FOLDER "Tests")
    else()
        add_test(${TESTNAME} ${TESTNAME})
        set_target_properties(${TESTNAME} PROPERTIES FOLDER "Tests")
    endif()
endmacro()

hide_variable(gmock_build_tests)
hide_variable(gtest_build_samples)
hide_variable(gtest_build_tests)
hide_variable(gtest_disable_pthreads)
hide_variable(gtest_hide_internal_symbols)
hide_variable(BUILD_GMOCK)
hide_variable(BUILD_GTEST)
hide_variable(INSTALL_GTEST)

set_target_properties(gtest gtest_main gmock gmock_main PROPERTIES FOLDER "Extern")

if(MSVC AND MSVC_VERSION GREATER_EQUAL 1900)
    target_compile_definitions(gtest PUBLIC _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
    target_compile_definitions(gtest_main PUBLIC _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
    target_compile_definitions(gmock PUBLIC _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
    target_compile_definitions(gmock_main PUBLIC _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
endif()
