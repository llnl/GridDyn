# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# cmake >= 3.28 required, minimum_required sets policy
cmake_minimum_required(VERSION 3.28)
cmake_policy(VERSION 3.28)

project(GRIDDYN VERSION 0.10.0)

# Require C++20 (or newer) across the project.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
if(CMAKE_CXX_STANDARD LESS 20)
    message(FATAL_ERROR "GridDyn requires CMAKE_CXX_STANDARD to be at least 20")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Minimum supported compiler versions for C++20-era GridDyn.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR "GridDyn requires GCC 13 or newer")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "GridDyn requires Clang 18 or newer")
endif()

# version number
set(GRIDDYN_VERSION_BUILD)
set(GRIDDYN_DATE "06-16-2020")

set(GRIDDYN_VERSION_UNDERSCORE
    "${GRIDDYN_VERSION_MAJOR}_${GRIDDYN_VERSION_MINOR}_${GRIDDYN_VERSION_PATCH}"
)
if(GRIDDYN_VERSION_BUILD)
    set(GRIDDYN_VERSION ${GRIDDYN_VERSION}-${GRIDDYN_VERSION_BUILD})
    set(GRIDDYN_VERSION_UNDERSCORE "${GRIDDYN_VERSION_UNDERSCORE}-${GRIDDYN_VERSION_BUILD}")
endif()

set(GRIDDYN_COMPILER_VERSION
    "${CMAKE_GENERATOR} ${CMAKE_GENERATOR_PLATFORM} ${CMAKE_SYSTEM}:${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
)
message(STATUS "SYSTEM INFO -> ${GRIDDYN_COMPILER_VERSION}")
# ------------------------------------------------------------
# set the module path and include some common macros
# ------------------------------------------------------------

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/config/cmake/"
                          "${PROJECT_SOURCE_DIR}/ThirdParty/cmake/"
    )
else()
    set(ORIGINAL_MODULE_PATH ${CMAKE_MODULE_PATH})
    set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/config/cmake/"
                          "${PROJECT_SOURCE_DIR}/ThirdParty/cmake/" ${CMAKE_MODULE_PATH}
    )
    get_property(griddyn-use-folders GLOBAL PROPERTY USE_FOLDERS)
endif()

if(NOT GRIDDYN_DISABLE_GIT_OPERATIONS)
    include(version_describe)
    git_version_describe(${PROJECT_SOURCE_DIR} GRIDDYN_VERSION_DESCRIPTION)
endif()

if(GRIDDYN_VERSION_DESCRIPTION)
    message(STATUS "griddyn version tag description is ${GRIDDYN_VERSION_DESCRIPTION}")
    string(TIMESTAMP current_date "%Y-%m-%d")
    set(GRIDDYN_VERSION_STRING
        "${GRIDDYN_VERSION}-${GRIDDYN_VERSION_DESCRIPTION} (${current_date})"
    )
else()
    set(GRIDDYN_VERSION_STRING "${GRIDDYN_VERSION} (${GRIDDYN_DATE})")
endif()

include(extraMacros)
include(CMakeDependentOption)
include(copy_key_files)
include(CTest)
include(ucm)
include(GNUInstallDirs)

# Set the build output paths
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Archive output dir.")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library output dir.")
    set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        CACHE PATH "PDB (MSVC debug symbol)output dir."
    )
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH
                                                                       "Executable/dll output dir."
    )
endif()
# ------------------------------------------------------------
# set the install path to a local directory
# ------------------------------------------------------------
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    if(WIN32)
        if(MSVC)
            set(CMAKE_INSTALL_PREFIX "C:/local/griddyn_${GRIDDYN_VERSION_UNDERSCORE}/"
                CACHE PATH "default install path" FORCE
            )
        elseif(MINGW AND NOT MSYS)
            set(CMAKE_INSTALL_PREFIX "C:/local/griddyn_${GRIDDYN_VERSION_UNDERSCORE}/"
                CACHE PATH "default install path" FORCE
            )
        elseif(MSYS)
            # use CMAKE_OBJCOPY here since it is somewhat less likely to be overridden by users
            # rather than the compiler
            get_filename_component(path_bin ${CMAKE_OBJCOPY} DIRECTORY)
            get_filename_component(path_install ${path_bin} DIRECTORY)
            set(CMAKE_INSTALL_PREFIX ${path_install} CACHE PATH "default install path" FORCE)
        endif(MSVC)
    endif(WIN32)
endif()

if(MSYS
   OR CYGWIN
   OR UNIX
   OR APPLE
)
    set(UNIX_LIKE TRUE)
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/logs)

# Prohibit in-source build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(
        FATAL_ERROR
            "In-source build is not supported. Please, use an empty directory for building the project."
    )
endif()

# -------
# options
# -------

cmake_dependent_option(
    GRIDDYN_BUILD_TESTS "Enable the test executables to be built" ON
    "CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME" OFF
)

cmake_dependent_option(
    GRIDDYN_BUILD_BENCHMARKS "Build GridDyn benchmarks" OFF
    "CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME" OFF
)

cmake_dependent_advanced_option(
    GRIDDYN_WITH_CMAKE_PACKAGE "Generate and install cmake package files" ON
    "CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME" OFF
)

# Install instructions for this target
if(GRIDDYN_WITH_CMAKE_PACKAGE)
    set(GRIDDYN_EXPORT_COMMAND EXPORT griddyn-targets)
else(GRIDDYN_WITH_CMAKE_PACKAGE)
    set(GRIDDYN_EXPORT_COMMAND)
endif(GRIDDYN_WITH_CMAKE_PACKAGE)

option(GRIDDYN_ENABLE_FMI_EXPORT "Enable construction of a binary fmi shared library for GridDyn"
       OFF
)

option(GRIDDYN_BUILD_PYTHON_INTERFACE "Build Python extension" OFF)

option(GRIDDYN_BUILD_PYTHON2_INTERFACE
       "Build Python2.7 extension (conflicts with \"PYTHON_INTERFACE\", requires swig)" OFF
)

option(GRIDDYN_BUILD_MATLAB_INTERFACE "Build Matlab Extension" OFF)

option(GRIDDYN_BUILD_OCTAVE_INTERFACE "Build Octave extension (very experimental)" OFF)

option(GRIDDYN_BUILD_JAVA_INTERFACE "Build Java extension" OFF)

option(GRIDDYN_ENABLE_CODE_COVERAGE_TEST "Build a target for testing code coverage" OFF)

set(GRIDDYN_ENABLE_OPENMP AUTO CACHE STRING "Enable OpenMP support")
set_property(CACHE GRIDDYN_ENABLE_OPENMP PROPERTY STRINGS AUTO ON OFF)

option(# TODO this needs to be accounted for in the source code
       GRIDDYN_ENABLE_MULTITHREADING "enable multithreading in GridDyn libraries" ON
)

option(GRIDDYN_ENABLE_MPI "Enable MPI networking library" OFF)

option(
    GRIDDYN_ENABLE_KLU
    "Option to disable KLU (not recommended [slow]; prefer to turn on GRIDDYN_SUITESPARSE_FORCE_SUBPROJECT)"
    ON
)

option(GRIDDYN_ENABLE_PLUGINS "Enable support for plugin modules" OFF)
mark_as_advanced(GRIDDYN_ENABLE_PLUGINS)

option(GRIDDYN_ENABLE_EXTRA_MODELS "Compile and load extraModels" ON)

option(GRIDDYN_ENABLE_EXTRA_SOLVERS "Compile and load extraSolvers" OFF)

option(GRIDDYN_ENABLE_FMI "Enable FMI support" ON)

option(GRIDDYN_ENABLE_LOGGING "enable all normal, debug, and trace logging in GridDyn" ON)

option(GRIDDYN_ENABLE_DOXYGEN "Generate Doxygen doc target" OFF)

option(GRIDDYN_ENABLE_HELICS_EXECUTABLE "Enable the HELICS executable to be built" OFF)

option(GRIDDYN_ENABLE_NETWORKING_LIBRARY "Enable network based communication components" OFF)

option(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY "Enable Optimization libraries" OFF)

option(GRIDDYN_ENABLE_CLANG_TOOLS
       "if clang is found enable some custom targets for clang formatting and tidy" OFF
)

option(GRIDDYN_ENABLE_PACKAGE_BUILD "Add projects for making packages and installers for GridDyn"
       OFF
)

# -----------------
# dependent options
# -----------------
set(EXTENDED_INDEXING_ALLOWED "${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
# TODO make sure this is adequately tested and consistently used
cmake_dependent_advanced_option(
    GRIDDYN_ENABLE_64_BIT_INDEXING
    "set all indexing and count variables to 64 bit unsigned (Usually not required)" OFF
    "EXTENDED_INDEXING_ALLOWED" OFF
)

cmake_dependent_advanced_option(
    GRIDDYN_ENABLE_TRACE_LOGGING "enable trace logging" ON "GRIDDYN_ENABLE_LOGGING" OFF
)

cmake_dependent_advanced_option(
    GRIDDYN_ENABLE_DEBUG_LOGGING "enable debug logging" ON "GRIDDYN_ENABLE_LOGGING" OFF
)

# add a baseline library for underlying dependencies and flags
add_library(griddyn_base INTERFACE)
add_library(griddyn_optional INTERFACE)

# -----------------------------------------------------------------------------
# General project wide configuration for debug postfix
# -----------------------------------------------------------------------------
if(NOT NO_DEBUG_POSFIX)
    if(NOT CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX d)
    endif()
endif()

# we want to acknowledge this flag since it is standard C++ but it can cause issues in submodules so
# we need to set other variables
if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
endif()

if(BUILD_SHARED_LIBS)
    set(GRIDDYN_BUILD_C_SHARED_LIBRARY ON)
    set(GRIDDYN_BUILD_CXX_SHARED_LIBRARY ON)
    set(old_build_shared ON)
    # we need this to prevent submodules from building shared libs
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
else()
    option(GRIDDYN_BUILD_C_SHARED_LIBRARY OFF
           "Enable construction of GridDyn binary C based shared library"
    )
    option(GRIDDYN_BUILD_CXX_SHARED_LIBRARY OFF "Build the GridDyn C++ shared library")
endif()

if(GRIDDYN_BUILD_PYTHON_INTERFACE
   OR GRIDDYN_BUILD_PYTHON2_INTERFACE
   OR GRIDDYN_BUILD_MATLAB_INTERFACE
   OR GRIDDYN_BUILD_JAVA_INTERFACE
   OR GRIDDYN_BUILD_OCTAVE_INTERFACE
)
    set(INTERFACE_BUILD ON)
    hide_variable(BUILD_GRIDDYN_C_SHARED_LIBRARY)
    show_variable(
        DISABLE_SWIG BOOL "Disable the use of swig to generate interface code and use repo code"
        OFF
    )
else()
    set(INTERFACE_BUILD OFF)
    hide_variable(DISABLE_SWIG)
endif()

if(INTERFACE_BUILD
   OR GRIDDYN_BUILD_CXX_SHARED_LIBRARY
   OR GRIDDYN_BUILD_C_SHARED_LIBRARY
   OR GRIDDYN_ENABLE_FMI_EXPORT
)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT CMAKE_POSITION_INDEPENDENT_CODE)
    show_variable(
        CMAKE_POSITION_INDEPENDENT_CODE BOOL
        "Compile the static libraries so that they're relocatable" OFF
    )
endif()

if(NOT TARGET compile_flags_target)
    add_library(compile_flags_target INTERFACE)
endif()

if(NOT TARGET build_flags_target)
    add_library(build_flags_target INTERFACE)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    mark_as_advanced(BUILD_TESTING)
    include(compiler_flags)

endif()

add_library(GRIDDYN::compile_flags_target ALIAS compile_flags_target)
add_library(GRIDDYN::build_flags_target ALIAS build_flags_target)

get_target_property(EXTRA_BUILD_FLAGS build_flags_target INTERFACE_COMPILE_OPTIONS)
set(GRIDDYN_BUILD_FLAGS_OUTPUT
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_STATIC_LINKER_FLAGS} ${EXTRA_BUILD_FLAGS} "
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    if(NOT USE_LIBCXX)
        show_variable(STATIC_STANDARD_LIB STRING "Link against a static standard lib" default)
        set_property(CACHE STATIC_STANDARD_LIB PROPERTY STRINGS default static dynamic)
    else()
        hide_variable(STATIC_STANDARD_LIB)
    endif()
    if(MSVC)
        show_variable(GRIDDYN_EMBEDDED_DEBUG_INFO STRING "embed debug info into lib files" default)
        set_property(CACHE GRIDDYN_EMBEDDED_DEBUG_INFO PROPERTY STRINGS default embedded external)
    else()
        hide_variable(GRIDDYN_EMBEDDED_DEBUG_INFO)
    endif()
endif()

if(STATIC_STANDARD_LIB STREQUAL "default")

elseif(STATIC_STANDARD_LIB STREQUAL "static")
    ucm_set_runtime(STATIC)
else()
    ucm_set_runtime(DYNAMIC)
endif()

if(GRIDDYN_EMBEDDED_DEBUG_INFO STREQUAL "default")

elseif(GRIDDYN_EMBEDDED_DEBUG_INFO STREQUAL "external")
    ucm_set_embedded_debug(EXTERNAL)
else()
    ucm_set_embedded_debug(EMBEDDED)
endif()

install(
    TARGETS compile_flags_target build_flags_target ${GRIDDYN_EXPORT_COMMAND}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# ------------------------------------------------------------
# BOOST  find the boost libraries
# ------------------------------------------------------------
# TODO check if boost works (e.g. libstdc++ vs libc++)
set(BOOST_REQUIRED_LIBRARIES)

include(addBoost)

target_link_libraries(griddyn_base INTERFACE Boost::boost)
target_link_libraries(griddyn_base INTERFACE compile_flags_target)
target_compile_definitions(griddyn_base INTERFACE BOOST_SYSTEM_NO_LIB BOOST_ERROR_CODE_HEADER_ONLY)

set(GRIDDYN_BOOST_VERSION_LEVEL ${BOOST_VERSION_LEVEL})

# ------------------------------------------------------------
# add coverage target
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_CODE_COVERAGE_TEST)
    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        include(CodeCoverage)

        set(COVERAGE_EXCLUDES
            'usr/*'
            'dependencies/*'
            'ThirdParty/*'
            'test/*'
            'tests/*'
            'swig/*'
            'examples/*'
        )
        setup_target_for_coverage(
            NAME
            griddyn_coverage # New target name
            EXECUTABLE
            ctest
            -R
            testShared # Executable in PROJECT_BINARY_DIR
        )
    else()
        message(FATAL_ERROR "CMAKE_BUILD_TYPE must be set to Coverage for testing code coverage")
    endif()
endif(GRIDDYN_ENABLE_CODE_COVERAGE_TEST)

# ------------------------------------------------------------
# Get some configuration for modern C++ features as available
# ------------------------------------------------------------
# message(STATUS ${CMAKE_CXX_FLAGS})
set(CONFIGURE_TARGET_LOCATION ${GRIDDYN_BINARY_DIR}/griddyn_generated_includes/griddyn/)
include(configGenerator)
target_include_directories(
    griddyn_base INTERFACE $<BUILD_INTERFACE:${GRIDDYN_BINARY_DIR}/griddyn_generated_includes/>
)

# ------------------------------------------------------------
# Enable OpenMP support?
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_OPENMP)
    find_package(OpenMP QUIET)
    if(OPENMP_FOUND)
        link_libraries(OpenMP::OpenMP_C OpenMP::OpenMP_CXX)
    else(OPENMP_FOUND)
        if(NOT GRIDDYN_ENABLE_OPENMP STREQUAL "AUTO")
            message(FATAL_ERROR "Disabling OpenMP support, could not determine compiler flags")
        endif()
    endif(OPENMP_FOUND)
endif(GRIDDYN_ENABLE_OPENMP)

cmake_dependent_option(
    ENABLE_OPENMP_SUNDIALS "Enable SUNDIALS NVector openMP implementation" ON
    "GRIDDYN_ENABLE_OPENMP;OPENMP_FOUND" OFF
)

cmake_dependent_advanced_option(
    # TODO look at this
    GRIDDYN_ENABLE_OPENMP_INTERNAL "Enable openmp internal to GridDyn--not used yet" OFF
    "GRIDDYN_ENABLE_OPENMP;OPENMP_FOUND" OFF
)

# ------------------------------------------------------------
# Find multithreading headers and includes
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_MULTITHREADING)
    if(NOT WIN32 OR MSYS)
        set(THREADS_PREFER_PTHREAD_FLAG ON)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(griddyn_base INTERFACE Threads::Threads)
endif(GRIDDYN_ENABLE_MULTITHREADING)

# -------------------------------------------------------------
# Update git submodules
# -------------------------------------------------------------
include(updateGitSubmodules)
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/jsoncpp_new/CMakeLists.txt")
    submod_update(ThirdParty/jsoncpp_new)
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/containers/gmlc/containers/BlockingQueue.hpp")
    submod_update(ThirdParty/containers)
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/concurrency/gmlc/concurrency/TriggerVariable.hpp")
    submod_update(ThirdParty/concurrency)
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/utilities/gmlc/utilities/demangle.hpp")
    submod_update(ThirdParty/utilities)
endif()

if(GRIDDYN_BUILD_TESTS AND BUILD_TESTING)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/googletest/CMakeLists.txt")
        submod_update(ThirdParty/googletest)
    endif()
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/units/units/units.hpp")
    submod_update(ThirdParty/units)
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/sundials/CMakeLists.txt")
    submod_update(ThirdParty/sundials)
endif()

if(GRIDDYN_ENABLE_KLU)
    if(NOT DEFINED GRIDDYN_USE_SYSTEM_SUITESPARSE_ONLY OR NOT GRIDDYN_USE_SYSTEM_SUITESPARSE_ONLY)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/suitesparse-cmake/CMakeLists.txt")
            submod_update(ThirdParty/suitesparse-cmake)
        endif()
    endif()
endif()

# ------------------------------------------------------------
# finding MPI
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_MPI)
    include(addMPI)
    if(TARGET MPI::MPI_C)
        target_link_libraries(griddyn_base INTERFACE MPI::MPI_C)
        if(MPI_CXX_FOUND)
            target_link_libraries(griddyn_base INTERFACE MPI::MPI_CXX)
        endif()
    else()
        message(FATAL_ERROR "unable to include MPI")
    endif()
endif(GRIDDYN_ENABLE_MPI)

# ------------------------------------------------------------
# Find (and test) the KLU libraries
# ------------------------------------------------------------

if(GRIDDYN_ENABLE_KLU)
    include(addKLU)
endif()

# ------------------------------------------------------------
# Sundials
# ------------------------------------------------------------
include(addSundials)

# ------------------------------------------------------------
# Enable HELICS executable
# ------------------------------------------------------------
# The order needs to be this -> ZMQ -> addHELICS due to knowledge dependencies
if(GRIDDYN_ENABLE_HELICS_EXECUTABLE)
    show_variable(
        HELICS_INSTALL_PATH PATH "path to the helics installation" "${PROJECT_BINARY_DIR}/libs"
    )
else(GRIDDYN_ENABLE_HELICS_EXECUTABLE)
    hide_variable(HELICS_INSTALL_PATH)
endif(GRIDDYN_ENABLE_HELICS_EXECUTABLE)

# ------------------------------------------------------------
# setting the RPATH
# ------------------------------------------------------------
# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH already (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

set(CMAKE_MACOSX_RPATH 1)

set(CMAKE_BUILD_RPATH
    "${AUTOBUILD_INSTALL_PATH}/bin;${AUTOBUILD_INSTALL_PATH}/lib;${AUTOBUILD_INSTALL_PATH}/lib64"
)

# add the automatically determined parts of the RPATH which point to directories outside the build
# tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if(NOT APPLE)
    set(CMAKE_INSTALL_RPATH $ORIGIN)
endif()

# the RPATH to be used when installing, but only if it's not a system directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES
     "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}" isSystemDir
)

if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
endif("${isSystemDir}" STREQUAL "-1")

list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES
     "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir
)
if("${isSystemDir}" STREQUAL "-1")
    list(APPEND CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif("${isSystemDir}" STREQUAL "-1")

if(APPLE)
    list(APPEND CMAKE_INSTALL_RPATH "@loader_path/.")
    list(APPEND CMAKE_INSTALL_RPATH "@executable_path/.")
else()
    list(APPEND CMAKE_INSTALL_RPATH "./")
endif()

if(NOT Boost_USE_STATIC_LIBS)
    list(APPEND CMAKE_INSTALL_RPATH ${Boost_LIBRARY_DIRS})
    list(APPEND CMAKE_BUILD_RPATH ${Boost_LIBRARY_DIRS})
endif()

# ------------------------------------------------------------
# global include directories
# ------------------------------------------------------------
target_include_directories(
    griddyn_base
    INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
              $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_INCLUDEDIR}>
              $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty/containers>
              $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty/concurrency>
              $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty/utilities>
)

target_include_directories(
    griddyn_base SYSTEM INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
)

# --------------------------------------------------------------
# Create the target for jsoncpp
# -----------------------------------------------------------
include(addJsoncpp)
add_library(GRIDDYN::jsoncpp_lib ALIAS jsoncpp_lib)

# --------------------------------------------------------------
# Create the target for unitslib
# -----------------------------------------------------------
include(addUnits)
add_library(GRIDDYN::units ALIAS units-static)

# -----------------------------------------------------------------------------
# create utilities target
# -----------------------------------------------------------------------------
set(GMLC_UTILITIES_INSTALL OFF CACHE INTERNAL "")
add_subdirectory(ThirdParty/utilities)

hide_variable(GMLC_UTILITIES_GENERATE_DOXYGEN_DOC)
hide_variable(GMLC_UTILITIES_INCLUDE_BOOST)
hide_variable(GMLC_UTILITIES_USE_BOOST_SPIRIT)
hide_variable(GMLC_UTILITIES_WITH_CMAKE_PACKAGE)

hide_variable(GMLC_UTILITIES_OBJECT_LIB)
hide_variable(GMLC_UTILITIES_STATIC_LIB)

add_library(GridDyn::utilities ALIAS gmlc_utilities)
set_target_properties(gmlc_utilities PROPERTIES FOLDER Extern)
target_include_directories(gmlc_utilities SYSTEM PRIVATE $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>)

# ------------------------------------------------------------
# load the required subdirectories
# ------------------------------------------------------------
add_subdirectory(src)

# ------------------------------------------------------------
# Enable Doxygen
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_DOXYGEN)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)

        show_variable(
            DOXYGEN_OUTPUT_DIR PATH "location to put Doxygen docs"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        )
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/config/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            @ONLY
        )
        add_custom_target(
            doc
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif(DOXYGEN_FOUND)
endif(GRIDDYN_ENABLE_DOXYGEN)

file(GLOB KEY_LIBRARY_FILES ${PROJECT_BINARY_DIR}/libs/bin/*)
message(STATUS "key files ${KEY_LIBRARY_FILES}")

# TODO this should probably be in a different file
# ------------------------------------------------------------
# Enable clang analysis and formatting tools
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_CLANG_TOOLS)
    include(clang-cxx-dev-tools)
endif(GRIDDYN_ENABLE_CLANG_TOOLS)

# TODO this should be under tests/
# ------------------------------------------------------------
# Enable testCore construction?
# ------------------------------------------------------------
if(GRIDDYN_BUILD_TESTS AND BUILD_TESTING)
    # message(STATUS "otcf:${optional_component_test_files}")
    add_subdirectory(test)
    if(GRIDDYN_BUILD_C_SHARED_LIBRARY)
        add_subdirectory(test/testSharedLibrary)
    endif()
    enable_testing()
    # add_test(NAME gridDynTest COMMAND testCore)
endif()

set(GRIDDYN_CMAKECONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    CACHE STRING "install path for GriddynConfig.cmake"
)

# TODO group the installs better
install(DIRECTORY examples/ DESTINATION examples COMPONENT examples)
# file(GLOB ebraidfiles "examples/braid_examples/*.*") install(FILES ${ebraidfiles} DESTINATION
# examples/braid_examples COMPONENT examples)

set(binfiles bin/configure.griddyn bin/pgriddyn bin/pgriddyn_debug)
install(PROGRAMS ${binfiles} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT binaries)

install(FILES ${KEY_LIBRARY_FILES} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT binaries)

install(
    EXPORT griddyn-targets
    NAMESPACE Griddyn::
    DESTINATION ${GRIDDYN_CMAKECONFIG_INSTALL_DIR}
    COMPONENT libs
)

# ------------------------------------------------------------
# Future Additions
# ------------------------------------------------------------
# adding dlls install(FILES ${LOCATION_OF_FILES} DESTINATION bin) file(GLOB docs "docs/manuals/*")
# install(FILES ${docs} DESTINATION docs)

# TODO this should be under tests/ too
# ------------------------------------------------------------
# CTest
# ------------------------------------------------------------
if(GRIDDYN_BUILD_TESTS AND BUILD_TESTING)

    find_program(CTEST_MEMORYCHECK_COMMAND valgrind)

    add_test(NAME Example179BusDynamicTest
             COMMAND gridDynMain ${PROJECT_SOURCE_DIR}/test/test_files/IEEE_test_cases/ieee300.cdf
    )
    set_property(TEST Example179BusDynamicTest PROPERTY LABELS Quick Continuous Nightly)

    if(GRIDDYN_BUILD_C_SHARED_LIBRARY)
        add_test(NAME testSharedLibrary COMMAND shared_library_tests)
        set_property(TEST testSharedLibrary PROPERTY LABELS Nightly Release)

        add_test(NAME testSharedLibraryQuick COMMAND shared_library_tests --run_test=@quick)
        set_property(TEST testSharedLibraryQuick PROPERTY LABELS Quick Continuous Valgrind)
    endif()
endif()

# ------------------------------------------------------------
# swig interface builds
# ------------------------------------------------------------
if(INTERFACE_BUILD)
    add_subdirectory(interfaces)
endif()

# TODO make a cpack file, put this there
# ------------------------------------------------------------
# CPack
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_PACKAGE_BUILD)
    set(CPACK_PACKAGE_NAME "GridDyn")
    set(CPACK_PACKAGE_VENDOR "Lawrence Livermore National Security LLC")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GridDyn Installer")
    set(CPACK_PACKAGE_VERSION "${GRIDDYN_VERSION}")
    set(CPACK_PACKAGE_VERSION_MAJOR ${GRIDDYN_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${GRIDDYN_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${GRIDDYN_VERSION_PATCH})

    set(CPACK_COMPONENTS_ALL
        applications
        headers
        libs
        binaries
        matlab
        python
        java
        octave
        examples
    )

    if(WIN32)
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}\\\\LICENSE")
    else(WIN32)
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
    endif(WIN32)

    set(CPACK_COMPONENT_APPLICATIONS_DISPLAY_NAME "Application")
    set(CPACK_COMPONENT_LIBS_DISPLAY_NAME "Libraries")
    set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "Headers")
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Runtime Libraries")
    set(CPACK_COMPONENT_EXAMPLES_DISPLAY_NAME "Examples")

    set(CPACK_COMPONENT_MATLAB_GROUP interfaces)
    set(CPACK_COMPONENT_JAVA_GROUP interfaces)
    set(CPACK_COMPONENT_OCTAVE_GROUP interfaces)
    set(CPACK_COMPONENT_PYTHON_GROUP interfaces)

    set(CPACK_COMPONENT_APPLICATION_DESCRIPTION "Executables and helper applications for GridDyn")
    set(CPACK_COMPONENT_LIBS_DESCRIPTION "Libraries for compiling and linking with GridDyn")
    set(CPACK_COMPONENT_HEADERS_DESCRIPTION "Headers for linking and compiling with GridDyn")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Runtime libraries for GrdDyn")
    set(CPACK_COMPONENT_EXAMPLES_DESCRIPTION "Example files for GridDyn")

    set(CPACK_COMPONENT_GROUP_INTERFACES_DESCRIPTION "additional language interfaces for GridDyn")

    set(CPACK_COMPONENT_LIBS_DEPENDS headers)
    set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)
    if(WIN32)
        set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}\\\\docgen\\\\images\\\\griddyn.ico")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/docgen/images/griddyn.ico")

        set(CPACK_NSIS_INSTALL_ROOT "C:\\\\local\\\\")
        set(CPACK_NSIS_URL_INFO_ABOUT "https://www.github.com/LLNL/GridDyn")
        set(CPACK_NSIS_MENU_LINKS "https://www.github.com/LLNL/GridDyn" "GridDyn source code"
                                  "https://www.griddyn.org" "GridDyn Web Page"
        )

        set(CPACK_NSIS_INSTALL_ROOT "C:\\\\local\\\\")
    else(WIN32)
        set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/docgen/images/griddyn.ico")
    endif(WIN32)
    set(CPACK_SOURCE_IGNORE_FILES "/Build*/;/build*/;/.git/")

    # THIS LINE MUST BE LAST
    include(CPack)
endif(GRIDDYN_ENABLE_PACKAGE_BUILD)

if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # restore the original module path
    set(CMAKE_MODULE_PATH ${ORIGINAL_MODULE_PATH})
    if(old_build_shared)
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    endif()
endif()
