# LLNS Copyright Start
# Copyright (c) 2017, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

message(FATAL_ERROR "This file is significantly out-of-date with the root CMakeLists.txt")

# project name
project(GridDyn)

# states that Cmake version > 3.28
cmake_minimum_required(VERSION 3.28)
cmake_policy(VERSION 3.28)

# version number
set(GridDyn_VERSION_MAJOR 0)
set(GridDyn_VERSION_MINOR 6)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/config)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# -------------------------------------------------------------
# MACRO definitions
# -------------------------------------------------------------
# Macros to hide/show cached variables. These two macros can be used to "hide" or "show" in the list
# of cached variables various variables and/or options that depend on other options. Note that once
# a variable is modified, it will preserve its value (hiding it merely makes it internal)

macro(HIDE_VARIABLE var)
    if(DEFINED ${var})
        set(${var} "${${var}}" CACHE INTERNAL "")
    endif(DEFINED ${var})
endmacro(HIDE_VARIABLE)

macro(SHOW_VARIABLE var type doc default)
    if(DEFINED ${var})
        set(${var} "${${var}}" CACHE "${type}" "${doc}" FORCE)
    else(DEFINED ${var})
        set(${var} "${default}" CACHE "${type}" "${doc}")
    endif(DEFINED ${var})
endmacro(SHOW_VARIABLE)

# Prohibit in-source build
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(
        FATAL_ERROR
            "In-source build is not supported. Please, use an empty directory for building the project."
    )
endif()

option(ENABLE_EXTRA_COMPILER_WARNINGS "disable compiler warning for GridDyn build" ON)

show_variable(
    COMPILER_OPTIMIZATION_LEVEL STRING
    "set the optimization level for the compiler (not implemented yet)" "normal"
)

set(optimization_levels normal;high;machine;full)

set_property(CACHE COMPILER_OPTIMIZATION_LEVEL PROPERTY STRINGS ${optimization_levels})

# -------------------------------------------------------------
# Setup compiler options and configurations
# -------------------------------------------------------------
message(STATUS "setting up for ${CMAKE_CXX_COMPILER_ID}")
if(UNIX)
    # Since default builds of boost library under Unix don't use CMake, turn off using CMake build
    # and find include/libs the regular way.
    set(Boost_NO_BOOST_CMAKE ON)

    set(Boost_USE_MULTITHREADED OFF) # Needed if MT libraries not built
    if(ENABLE_EXTRA_COMPILER_WARNINGS)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-pedantic>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wshadow>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-aliasing=1>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wunreachable-code>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-overflow=5>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wredundant-decls>)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>)
    endif(ENABLE_EXTRA_COMPILER_WARNINGS)
else(UNIX)
    if(MINGW)
        if(ENABLE_EXTRA_COMPILER_WARNINGS)
            add_compile_options(-Wall -pedantic)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wshadow>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-aliasing=1>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wunreachable-code>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-overflow=5>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wredundant-decls>)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>)
            # this options produces lots of warning but is useful for checking ever once in a while
            # with Clang GCC warning notices with this aren't as useful
            # add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wpadded>)
        endif(ENABLE_EXTRA_COMPILER_WARNINGS)
    endif(MINGW)

    # -------------------------------------------------------------
    # Extra definitions for visual studio
    # -------------------------------------------------------------
    if(MSVC)
        set(Boost_USE_STATIC_LIBS ON)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
        add_definitions(-D_SCL_SECURE_NO_WARNINGS)
        add_compile_options(/EHsc)
        if(ENABLE_EXTRA_COMPILER_WARNINGS)
            add_compile_options(
                -W4
                /wd4065
                /wd4101
                /wd4102
                /wd4244
                /wd4297
                /wd4355
                /wd4800
                /wd4484
            )
        endif(ENABLE_EXTRA_COMPILER_WARNINGS)

    endif(MSVC)
endif(UNIX)

# message(STATUS ${CMAKE_CXX_FLAGS})
include(configGenerator)

if(VERSION_OPTION)
    add_compile_options(${VERSION_OPTION})
else()
    set(CMAKE_CXX_STANDARD 14)
endif()

include(ExternalProject)

# ##################################################################################################

# include(mergestaticlibs)
# -------------------------------------------------------------
# Enable OpenMP support?
# -------------------------------------------------------------
option(OPENMP_ENABLE "Enable openMP support" ON)

if(OPENMP_ENABLE)
    # message(STATUS "****** finding OpenMP support")
    find_package(OpenMP)
    if(OPENMP_FOUND)
        add_definitions(-DHAVE_OPENMP)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CDD_FLAGS} ${OpenMP_CXX_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    else(OPENMP_FOUND)
        message(STATUS "Disabling OpenMP support, could not determine compiler flags")
        set(OPENMP_ENABLE FALSE)
    endif(OPENMP_FOUND)
else(OPENMP_ENABLE)
    set(OPENMP_FOUND FALSE)
endif(OPENMP_ENABLE)

if(OPENMP_FOUND)
    option(SUNDIALS_OPENMP "Enable SUNDIALS NVector openMP implementation" ON)
    option(GRIDDYN_OPENMP "Enable openmp internal to GridDyn--not used yet" OFF)
endif(OPENMP_FOUND)

option(DISABLE_MULTITHREADING "disable multithreading in GridDyn libraries" OFF)

# -------------------------------------------------------------
# Find multithreading headers and includes
# -------------------------------------------------------------

if(NOT DISABLE_MULTITHREADING)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    list(APPEND external_library_list Threads::Threads)
endif(NOT DISABLE_MULTITHREADING)

# -------------------------------------------------------------
# Enable MPI support?
# -------------------------------------------------------------

option(MPI_ENABLE "Enable MPI support" OFF)

if(MPI_ENABLE)
    find_package(MPI)
    # SGS FIXME this should in configure.h somewhere perhaps?
    add_definitions(-DHAVE_MPI)
endif(MPI_ENABLE)

# -------------------------------------------------------------
# Find (and test) the KLU libraries
# -------------------------------------------------------------

option(KLU_ENABLE "Enable KLU support" ON)

if(NOT DEFINED SuiteSparse_DIR)
    set(SuiteSparse_DIR ${PROJECT_BINARY_DIR}/libs CACHE PATH "path to SuiteSparse/KLU")
endif()

show_variable(SuiteSparse_DIR PATH "KLU library directory" "${SuiteSparse_DIR}")

if(KLU_ENABLE)
    if(MSVC)
        set(SuiteSparse_USE_LAPACK_BLAS ON)
    endif(MSVC)
    set(SuiteSparse_VERBOSE OFF)
    find_package(
        SuiteSparse
        COMPONENTS KLU
                   AMD
                   COLAMD
                   BTF
                   SUITESPARSECONFIG
                   CXSPARSE
    )
    if(SuiteSparse_FOUND)
        list(APPEND external_library_list ${SuiteSparse_LIBRARIES})
        # list(APPEND external_link_directories ${SuiteSparse_LIBRARY_DIR})
        if(AUTOBUILD_KLU)
            option(FORCE_KLU_REBUILD "force rebuild of KLU" OFF)
            if(FORCE_KLU_REBUILD)
                include(buildSuiteSparse)
                build_suitesparse()
                set(FORCE_KLU_REBUILD OFF CACHE BOOL "force rebuild of KLU" FORCE)
            endif(FORCE_KLU_REBUILD)
        endif(AUTOBUILD_KLU)
    else(SuiteSparse_FOUND)
        option(AUTOBUILD_KLU "enable Suitesparse to automatically download and build" OFF)
        if(AUTOBUILD_KLU)
            include(buildSuiteSparse)
            build_suitesparse()
            set(SuiteSparse_DIR ${PROJECT_BINARY_DIR}/libs)
            find_package(
                SuiteSparse
                COMPONENTS KLU
                           AMD
                           COLAMD
                           BTF
                           SUITESPARSECONFIG
                           CXSPARSE
            )
        endif(AUTOBUILD_KLU)
        if(SuiteSparse_FOUND)
            list(APPEND external_library_list ${SuiteSparse_LIBRARIES})
        else()
            message(FATAL_ERROR "KLU not functional - support will not be provided")
            message("Double check spelling specified libraries (search is case sensitive)")
        endif()
    endif(SuiteSparse_FOUND)

endif(KLU_ENABLE)

# -------------------------------------------------------------
# Sundials
# -------------------------------------------------------------

if(NOT DEFINED SUNDIALS_DIR)
    set(SUNDIALS_DIR ${PROJECT_BINARY_DIR}/libs CACHE PATH "path to SUNDIALS")
endif()

show_variable(SUNDIALS_DIR PATH "SUNDIALS library directory" "${SUNDIALS_DIR}")

set(SUNDIALS_FIND_QUIETLY ON)

set(SUNDIALS_REQUIRED IDA KINSOL)
if(OPENMP_FOUND)
    list(APPEND SUNDIALS_REQUIRED nvecopenmp)
endif(OPENMP_FOUND)
find_package(SUNDIALS REQUIRED COMPONENTS ${SUNDIALS_REQUIRED} OPTIONAL_COMPONENTS CVODE ARKODE)

if(SUNDIALS_FOUND)
    list(INSERT external_library_list 0 ${SUNDIALS_LIBRARIES})
    option(FORCE_SUNDIALS_REBUILD "force rebuild of sundials" OFF)
    if(AUTOBUILD_SUNDIALS)

        if(FORCE_SUNDIALS_REBUILD)
            include(buildSundials)
            build_sundials()
            set(FORCE_SUNDIALS_REBUILD OFF CACHE BOOL "force rebuild of sundials" FORCE)
        endif(FORCE_SUNDIALS_REBUILD)
    else(AUTOBUILD_SUNDIALS)
        if(FORCE_SUNDIALS_REBUILD)
            include(buildSundials)
            build_sundials()
            set(SUNDIALS_FOUND OFF CACHE BOOL "sundials not found" FORCE)
            set(SUNDIALS_LIBRARIES NOT_FOUND CACHE FORCE)
            set(FORCE_SUNDIALS_REBUILD OFF CACHE BOOL "force rebuild of sundials" FORCE)
            set(SUNDIALS_DIR ${PROJECT_BINARY_DIR}/libs)
            find_package(
                SUNDIALS REQUIRED COMPONENTS ${SUNDIALS_REQUIRED} OPTIONAL_COMPONENTS CVODE ARKODE
            )
        endif(FORCE_SUNDIALS_REBUILD)
    endif(AUTOBUILD_SUNDIALS)
else(SUNDIALS_FOUND)
    option(AUTOBUILD_SUNDIALS "enable Sundials to automatically download and build" OFF)
    message(STATUS "abs=${AUTOBUILD_SUNDIALS}")
    if(AUTOBUILD_SUNDIALS)
        include(buildSundials)
        build_sundials()
        set(SUNDIALS_DIR ${PROJECT_BINARY_DIR}/libs)
        find_package(
            SUNDIALS REQUIRED COMPONENTS ${SUNDIALS_REQUIRED} OPTIONAL_COMPONENTS CVODE ARKODE
        )
    endif(AUTOBUILD_SUNDIALS)
    if(SUNDIALS_FOUND)
        list(INSERT external_library_list 0 ${SUNDIALS_LIBRARIES})
    else(SUNDIALS_FOUND)
        message(FATAL_ERROR "sundials not found - unable to continue")
        message("Double check spelling specified libraries (search is case sensitive)")
    endif(SUNDIALS_FOUND)
endif(SUNDIALS_FOUND)

if(SUNDIALS_ARKODE_FOUND)
    set(LOAD_ARKODE TRUE)
endif(SUNDIALS_ARKODE_FOUND)

if(SUNDIALS_CVODE_FOUND)
    set(LOAD_CVODE TRUE)
endif(SUNDIALS_CVODE_FOUND)

set(optional_component_test_files)
set(optional_system_test_files)

set(griddyn_optional_libraries)

set(optional_library_key_headers)
set(optional_library_functions)

# -------------------------------------------------------------
# BOOST
# -------------------------------------------------------------

if(MSVC)
    find_path(
        BOOST_TEST_PATH
        NAMES boost/version.hpp
        PATHS C:/boost_1_64_0
              C:/boost_1_63_0
              C:/boost_1_61_0
              D:/boost_1_64_0
              D:/boost_1_63_0
              D:/boost_1_61_0
              C:/boost/boost_1_64_0
              C:/boost/boost_1_63_0
              C:/boost/boost_1_61_0
              D:/boost/boost_1_64_0
              D:/boost/boost_1_63_0
              D:/boost/boost_1_61_0
              C:/local/boost_1_64_0
              C:/local/boost_1_63_0
              C:/local/boost_1_61_0
              D:/local/boost_1_64_0
              D:/local/boost_1_63_0
              D:/local/boost_1_61_0
    )

    if(BOOST_TEST_PATH)
        set(BOOST_ROOT ${BOOST_TEST_PATH})
    endif(BOOST_TEST_PATH)
endif(MSVC)

show_variable(BOOST_ROOT PATH "Boost root directory" "${BOOST_ROOT}")

if(MPI_ENABLE)
    find_package(
        Boost
        COMPONENTS program_options
                   filesystem
                   serialization
                   mpi
                   system
                   date_time
        REQUIRED
    )
else(MPI_ENABLE)
    find_package(Boost COMPONENTS program_options filesystem system date_time REQUIRED)
endif(MPI_ENABLE)

mark_as_advanced(CLEAR BOOST_ROOT)

message("Using Boost include files : ${Boost_INCLUDE_DIR}")
message("Using Boost libraries ${Boost_LIBRARY_DIRS}")

list(APPEND external_library_list ${Boost_LIBRARIES})
list(APPEND external_link_directories ${Boost_LIBRARY_DIRS})

# -------------------------------------------------------------
# global include directories
# -------------------------------------------------------------
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/libs/include)
include_directories(${PROJECT_SOURCE_DIR}/ThirdParty)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})
# -------------------------------------------------------------
# load the required subdirectories
# -------------------------------------------------------------

add_subdirectory(src/formatInterpreters)

add_subdirectory(src/core)
# required for utilities library

add_subdirectory(src/utilities)

add_subdirectory(src/griddyn)

# now go into the optional stuff

# -------------------------------------------------------------
# Enable support for Plugins
# -------------------------------------------------------------

option(ENABLE_PLUGINS "Enable support for plugin modules" OFF)

if(ENABLE_PLUGINS)
    if(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        set(ENABLE_PLUGINS OFF)
        message("Plugin support requires BOOST 1.61 or higher")
    else(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        add_subdirectory(plugins)
    endif(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
endif(ENABLE_PLUGINS)

# -------------------------------------------------------------
# Enable compilation of extraModels?
# -------------------------------------------------------------

option(LOAD_EXTRA_MODELS "Compile and load extraModels" ON)
# If extra models are used enabled try to locate the libraries link against them.

if(LOAD_EXTRA_MODELS)
    add_subdirectory(src/extraModels)
endif(LOAD_EXTRA_MODELS)

# -------------------------------------------------------------
# Enable FMI support?
# -------------------------------------------------------------

option(FMI_ENABLE "Enable FMI support" ON)
# If FMI is enabled try to locate the libraries link against them.

if(FMI_ENABLE)
    if(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        set(FMI_ENABLE OFF CACHE BOOL "Enable FMI Support" FORCE)
        message("FMI support requires BOOST 1.61 or higher")
    else(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        add_subdirectory(src/fmi)
    endif(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
endif(FMI_ENABLE)

# -------------------------------------------------------------
# FSKIT
# -------------------------------------------------------------

option(FSKIT_ENABLE "Enable FSKIT support" OFF)

if(FSKIT_ENABLE)
    find_package(FSKIT)
    include_directories(SYSTEM ${FSKIT_INCLUDE_DIR})
    set(GRIDDYN_HAVE_FSKIT 1)
    add_subdirectory(src/fskit)
endif(FSKIT_ENABLE)

# -------------------------------------------------------------
# Enable 64 bit indexing (enable to allow for more than 2^31 objects)--that would be a very big
# model
# -------------------------------------------------------------

if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    option(ENABLE_64_BIT_INDEXING
           "set all indexing and count variables to 64 bit unsigned (Usually not required)" OFF
    )
endif("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")

# OPTION(BUILD_SERVERMODE "Build GridDyn Server" OFF)
if(BUILD_SERVERMODE)
    add_subdirectory(src/gridDynServer)
endif(BUILD_SERVERMODE)

# ------------------------------------------------------------
# Enable message logging
# -------------------------------------------------------------
option(LOG_ENABLE "enable non-warning/error logging messages" ON)

if(LOG_ENABLE)
    option(
        DEBUG_LOG_ENABLE
        "enable debug logging messages(Note: logging is still controlled via program options this is for program size and speed)"
        ON
    )
    option(
        TRACE_LOG_ENABLE
        "enable trace logging messages(Note: logging is still controlled via program options this is for program size and speed)"
        ON
    )
endif(LOG_ENABLE)

option(GRIDDYN_GENERATE_DOXYGEN_DOC "Generate Doxygen doc target" OFF)

if(GRIDDYN_GENERATE_DOXYGEN_DOC)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)

        show_variable(
            DOXYGEN_OUTPUT_DIR PATH "location to put Doxygen docs"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        )
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY
        )
        add_custom_target(
            doc
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif(DOXYGEN_FOUND)
endif(GRIDDYN_GENERATE_DOXYGEN_DOC)

# -------------------------------------------------------------
# Enable FNCS executable
# -------------------------------------------------------------

option(FNCS_EXECUTABLE "Enable the FNCS executable to be built" OFF)
# If FNCS_EXECUTABLE is load the fncs directory link against them.

if(FNCS_EXECUTABLE)
    show_variable(USE_DUMMY_FNCS BOOL "use a dummy fncs implementation if none is available" OFF)
    if(USE_DUMMY_FNCS)
        hide_variable(USE_FNCS_SHARED_LIBRARY)
        hide_variable(FNCS_INSTALL_PATH)

    else(USE_DUMMY_FNCS)
        show_variable(
            USE_FNCS_SHARED_LIBRARY BOOL "used a dynamic shared library for the FNCS interface" OFF
        )
        set(ZMQ_NEEDED ON)
        if(USE_FNCS_SHARED_LIBRARY)
            show_variable(
                FNCS_INSTALL_PATH PATH "path to the fncs installation" "${FNCS_LIBRARY_PATH}"
            )
            find_package(FNCS)
        else(USE_FNCS_SHARED_LIBRARY)
            hide_variable(FNCS_INSTALL_PATH)
            show_variable(
                CZMQ_LIBRARY_PATH PATH "path to the czmq libraries" "${CZMQ_LIBRARY_PATH}"
            )
            show_variable(CZMQ_INCLUDE_PATH PATH "path to the czmq headers" "${CZMQ_INCLUDE_PATH}")
            find_package(CZMQ)
            list(APPEND griddyn_optional_libraries ${CZMQ_LIBRARY})
        endif(USE_FNCS_SHARED_LIBRARY)

    endif(USE_DUMMY_FNCS)
    add_subdirectory(src/fncs)
else(FNCS_EXECUTABLE)
    hide_variable(FNCS_INSTALL_PATH)
    hide_variable(USE_DUMMY_FNCS)
    hide_variable(USE_FNCS_SHARED_LIBRARY)
endif(FNCS_EXECUTABLE)

# -------------------------------------------------------------
# Enable HELICS executable
# -------------------------------------------------------------

option(HELICS_EXECUTABLE "Enable the HELICS executable to be built" OFF)
# If HELICS_EXECUTABLE is load the fncs directory link against them.

if(HELICS_EXECUTABLE)
    show_variable(
        HELICS_INSTALL_PATH PATH "path to the helics installation" "${PROJECT_BINARY_DIR}/libs"
    )
    set(ZMQ_NEEDED ON CACHE BOOL "ZMQ is needed" FORCE)
else(HELICS_EXECUTABLE)
    hide_variable(HELICS_INSTALL_PATH)
endif(HELICS_EXECUTABLE)

# -------------------------------------------------------------
# Enable ZMQ interfacing
# -------------------------------------------------------------

option(DIME_ENABLE "Enable connection with Dime" OFF)

if(DIME_ENABLE)
    set(ZMQ_NEEDED ON CACHE BOOL "ZMQ is needed" FORCE)
endif(DIME_ENABLE)
option(ZMQ_ENABLE "Enable ZMQ networking library" ${ZMQ_NEEDED})
# If ZMQ library is enabled try to locate it and link against it

if((ZMQ_ENABLE) OR (ZMQ_NEEDED))

    show_variable(ZMQ_LIBRARY_PATH PATH "path to the zmq libraries" "${PROJECT_BINARY_DIR}/libs")

    show_variable(ZMQ_INCLUDE_PATH PATH "path to the zmq headers" "${PROJECT_BINARY_DIR}/libs")
    set(ZMQ_FIND_QUIETLY ON)
    find_package(ZMQ)

    if(NOT ZMQ_FOUND)
        option(AUTOBUILD_ZMQ "enable ZMQ to automatically download and build" OFF)
        if(AUTOBUILD_ZMQ)
            include(buildlibZMQ)
            build_libzmq()
            set(ZMQ_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
            find_package(ZMQ)
        endif(AUTOBUILD_ZMQ)

    endif(NOT ZMQ_FOUND)

    # ZMQ_INCLUDE_DIR, where to find zmq.h ZMQ_LIBRARY, the library needed to use ZMQ ZMQ_FOUND, if
    # false, you cannot build anything that requires ZMQ.
    if(ZMQ_FOUND)
        add_subdirectory(src/zmqLib)
    else(ZMQ_FOUND)
        message(SEND_ERROR "unable to locate zmq library")
    endif(ZMQ_FOUND)
endif((ZMQ_ENABLE) OR (ZMQ_NEEDED))

if(HELICS_EXECUTABLE)
    show_variable(
        HELICS_INSTALL_PATH PATH "path to the helics installation" "${PROJECT_BINARY_DIR}/libs"
    )
    set(HELICS_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
    find_package(HELICS)
    if(NOT HELICS_FOUND)
        option(AUTOBUILD_HELICS "enable HELICS to automatically download and build" OFF)
        if(AUTOBUILD_HELICS)
            include(buildHELICS)
            build_helics()
            set(HELICS_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
            find_package(HELICS)
        endif(AUTOBUILD_HELICS)
    else(NOT HELICS_FOUND)
        option(FORCE_HELICS_REBUILD "force rebuild of helics" OFF)
        if(AUTOBUILD_HELICS)
            if(FORCE_HELICS_REBUILD)
                include(buildHELICS)
                build_helics()
                set(HELICS_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
                set(FORCE_HELICS_REBUILD OFF CACHE BOOL "force rebuild of helics" FORCE)
            endif(FORCE_HELICS_REBUILD)
        endif(AUTOBUILD_HELICS)
    endif(NOT HELICS_FOUND)

    add_subdirectory(src/helics)
endif(HELICS_EXECUTABLE)
file(GLOB KEY_LIBRARY_FILES ${PROJECT_BINARY_DIR}/libs/bin/*)
message(STATUS "key files ${KEY_LIBRARY_FILES}")
# -------------------------------------------------------------
# load the subdirectories
# -------------------------------------------------------------

option(OPTIMIZATION_ENABLE "Enable Optimization libraries" OFF)
# If OPTIMIZATION is enabled try to locate the libraries link against them.
if(OPTIMIZATION_ENABLE)
    add_subdirectory(src/optimization)
endif(OPTIMIZATION_ENABLE)

add_subdirectory(src/fileInput)

add_subdirectory(src/coupling)

add_subdirectory(src/gridDynCombined)

option(BUILD_SHARED_FMI_LIBRARY "Enable construction of a binary fmi shared library for GridDyn" ON)

if(BUILD_SHARED_FMI_LIBRARY)
    add_subdirectory(src/fmi_export)
endif(BUILD_SHARED_FMI_LIBRARY)

add_subdirectory(src/gridDynMain)
# -------------------------------------------------------------
# Enable clang analysis and formatting tools
# -------------------------------------------------------------

option(ENABLE_CLANG_TOOLS
       "if clang is found enable some custom targets for clang formatting and tidy" OFF
)

if(ENABLE_CLANG_TOOLS)
    include(clang-cxx-dev-tools)
endif(ENABLE_CLANG_TOOLS)
# -------------------------------------------------------------
# Enable testCore construction?
# -------------------------------------------------------------

option(TEST_ENABLE "Enable unit testing construction" ON)

if(TEST_ENABLE)

    # message("otcf:${optional_component_test_files}")
    add_subdirectory(test)
    # enable_testing() add_test(NAME gridDynTest COMMAND testCore)

endif(TEST_ENABLE)

file(GLOB efiles "examples/*")
install(FILES ${efiles} DESTINATION examples)

# SET(binfiles bin/configure.griddyn bin/pgriddyn bin/pgriddyn_debug) INSTALL(PROGRAMS ${binfiles}
# DESTINATION bin)

install(FILES ${KEY_LIBRARY_FILES} DESTINATION bin)

# -------------------------------------------------------------
# Future Additions (by Jen)
# -------------------------------------------------------------

# adding dlls INSTALL(FILES ${LOCATION_OF_FILES} DESTINATION bin)
file(GLOB docs "docs/manuals/*")
install(FILES ${docs} DESTINATION docs)

# -------------------------------------------------------------
# CTest
# -------------------------------------------------------------

enable_testing()

# -------------------------------------------------------------
# CPack for NSIS Installer
# -------------------------------------------------------------

set(CPACK_PACKAGE_NAME "GridDyn")
set(CPACK_PACKAGE_VENDOR "Lawrence Livermore National Security")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GridDyn Installer")
set(CPACK_PACKAGE_VERSION "${GridDyn_VERSION_MAJOR}.${GridDyn_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_MAJOR ${GridDyn_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${GridDyn_VERSION_MINOR})
# set(CPACK_PACKAGE_VERSION_PATCH ${GridDyn_VERSION_PATCH})

# THIS LINE MUST BE LAST
include(CPack)
