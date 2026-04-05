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

#project name
project(GridDyn)

#states that Cmake version > 3.28
cmake_minimum_required(VERSION 3.28)
cmake_policy(VERSION 3.28)

#version number
set(GridDyn_VERSION_MAJOR 0)
set(GridDyn_VERSION_MINOR 6)

SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/config)

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# -------------------------------------------------------------
# MACRO definitions
# -------------------------------------------------------------
# Macros to hide/show cached variables.
# These two macros can be used to "hide" or "show" in the
# list of cached variables various variables and/or options
# that depend on other options.
# Note that once a variable is modified, it will preserve its
# value (hiding it merely makes it internal)

MACRO(HIDE_VARIABLE var)
  IF(DEFINED ${var})
    SET(${var} "${${var}}" CACHE INTERNAL "")
  ENDIF(DEFINED ${var})
ENDMACRO(HIDE_VARIABLE)

MACRO(SHOW_VARIABLE var type doc default)
  IF(DEFINED ${var})
    SET(${var} "${${var}}" CACHE "${type}" "${doc}" FORCE)
  ELSE(DEFINED ${var})
    SET(${var} "${default}" CACHE "${type}" "${doc}")
  ENDIF(DEFINED ${var})
ENDMACRO(SHOW_VARIABLE)


# Prohibit in-source build
IF("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR "In-source build is not supported. Please, use an empty directory for building the project.")
ENDIF()

OPTION(ENABLE_EXTRA_COMPILER_WARNINGS "disable compiler warning for GridDyn build" ON)

SHOW_VARIABLE(COMPILER_OPTIMIZATION_LEVEL STRING
  "set the optimization level for the compiler (not implemented yet)" "normal")

  set(optimization_levels normal;high;machine;full)

set_property(CACHE COMPILER_OPTIMIZATION_LEVEL PROPERTY STRINGS ${optimization_levels})

#-------------------------------------------------------------
# Setup compiler options and configurations
#-------------------------------------------------------------
message(STATUS "setting up for ${CMAKE_CXX_COMPILER_ID}")
IF(UNIX)
  # Since default builds of boost library under Unix don't use
  # CMake, turn off using CMake build and find include/libs the
  # regular way.
  set(Boost_NO_BOOST_CMAKE ON)

  set(Boost_USE_MULTITHREADED      OFF)   # Needed if MT libraries not built
  if (ENABLE_EXTRA_COMPILER_WARNINGS)
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
ELSE(UNIX)
  IF(MINGW)
  if (ENABLE_EXTRA_COMPILER_WARNINGS)
 add_compile_options(-Wall -pedantic)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wshadow>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-aliasing=1>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wunreachable-code>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-overflow=5>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wredundant-decls>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>)
  #this options produces lots of warning but is useful for checking ever once in a while with Clang GCC warning notices with this aren't as useful
  #add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wpadded>)
  endif(ENABLE_EXTRA_COMPILER_WARNINGS)
  ENDIF(MINGW)

# -------------------------------------------------------------
# Extra definitions for visual studio
# -------------------------------------------------------------
IF(MSVC)
  set(Boost_USE_STATIC_LIBS ON)
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
  ADD_DEFINITIONS(-D_SCL_SECURE_NO_WARNINGS)
  add_compile_options(/EHsc)
  if (ENABLE_EXTRA_COMPILER_WARNINGS)
   add_compile_options(-W4 /wd4065 /wd4101 /wd4102 /wd4244 /wd4297 /wd4355 /wd4800 /wd4484)
  endif(ENABLE_EXTRA_COMPILER_WARNINGS)

ENDIF(MSVC)
ENDIF(UNIX)

#message(STATUS ${CMAKE_CXX_FLAGS})
include(configGenerator)

IF (VERSION_OPTION)
    add_compile_options(${VERSION_OPTION})
ELSE ()
set(CMAKE_CXX_STANDARD 14)
ENDIF ()

include(ExternalProject)

#########################################################################################

#include(mergestaticlibs)
# -------------------------------------------------------------
# Enable OpenMP support?
# -------------------------------------------------------------
OPTION(OPENMP_ENABLE "Enable openMP support" ON)



IF(OPENMP_ENABLE)
  #message(STATUS "****** finding OpenMP support")
  FIND_PACKAGE(OpenMP)
  IF(OPENMP_FOUND)
    ADD_DEFINITIONS(-DHAVE_OPENMP)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CDD_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
  ELSE(OPENMP_FOUND)
    message(STATUS "Disabling OpenMP support, could not determine compiler flags")
    set(OPENMP_ENABLE FALSE)
  ENDIF(OPENMP_FOUND)
ELSE(OPENMP_ENABLE)
  set(OPENMP_FOUND FALSE)
ENDIF(OPENMP_ENABLE)

IF (OPENMP_FOUND)
    OPTION(SUNDIALS_OPENMP "Enable SUNDIALS NVector openMP implementation" ON)
    OPTION(GRIDDYN_OPENMP "Enable openmp internal to GridDyn--not used yet" OFF)
ENDIF(OPENMP_FOUND)

OPTION(DISABLE_MULTITHREADING "disable multithreading in GridDyn libraries" OFF)


# -------------------------------------------------------------
# Find multithreading headers and includes
# -------------------------------------------------------------

IF (NOT DISABLE_MULTITHREADING)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
list(APPEND external_library_list Threads::Threads)
ENDIF(NOT DISABLE_MULTITHREADING)

# -------------------------------------------------------------
# Enable MPI support?
# -------------------------------------------------------------

OPTION(MPI_ENABLE "Enable MPI support" OFF)

IF(MPI_ENABLE)
  find_package(MPI)
  # SGS FIXME this should in configure.h somewhere perhaps?
  ADD_DEFINITIONS(-DHAVE_MPI)
ENDIF(MPI_ENABLE)


# -------------------------------------------------------------
# Find (and test) the KLU libraries
# -------------------------------------------------------------


OPTION(KLU_ENABLE "Enable KLU support" ON)

if(NOT DEFINED SuiteSparse_DIR)
  set(SuiteSparse_DIR ${PROJECT_BINARY_DIR}/libs CACHE PATH "path to SuiteSparse/KLU")
endif()

SHOW_VARIABLE(SuiteSparse_DIR PATH
  "KLU library directory" "${SuiteSparse_DIR}")

if(KLU_ENABLE)
IF(MSVC)
set(SuiteSparse_USE_LAPACK_BLAS ON)
ENDIF(MSVC)
set(SuiteSparse_VERBOSE OFF)
  find_package(SuiteSparse COMPONENTS KLU AMD COLAMD BTF SUITESPARSECONFIG CXSPARSE)
  if(SuiteSparse_FOUND)
    list(APPEND external_library_list ${SuiteSparse_LIBRARIES})
  #list(APPEND external_link_directories ${SuiteSparse_LIBRARY_DIR})
  IF (AUTOBUILD_KLU)
        OPTION(FORCE_KLU_REBUILD "force rebuild of KLU" OFF)
        IF(FORCE_KLU_REBUILD)
            include(buildSuiteSparse)
            build_suitesparse()
            set(FORCE_KLU_REBUILD OFF CACHE BOOL "force rebuild of KLU" FORCE)
        ENDIF(FORCE_KLU_REBUILD)
    ENDIF(AUTOBUILD_KLU)
  else(SuiteSparse_FOUND)
    OPTION(AUTOBUILD_KLU "enable Suitesparse to automatically download and build" OFF)
    IF (AUTOBUILD_KLU)
        include(buildSuiteSparse)
        build_suitesparse()
        set(SuiteSparse_DIR ${PROJECT_BINARY_DIR}/libs)
        find_package(SuiteSparse COMPONENTS KLU AMD COLAMD BTF SUITESPARSECONFIG CXSPARSE)
    ENDIF(AUTOBUILD_KLU)
    if (SuiteSparse_FOUND)
       list(APPEND external_library_list ${SuiteSparse_LIBRARIES})
    else()
        message( FATAL_ERROR "KLU not functional - support will not be provided")
        message( "Double check spelling specified libraries (search is case sensitive)")
    endif()
  endif(SuiteSparse_FOUND)

endif(KLU_ENABLE)

# -------------------------------------------------------------
# Sundials
# -------------------------------------------------------------

if(NOT DEFINED SUNDIALS_DIR)
  set(SUNDIALS_DIR ${PROJECT_BINARY_DIR}/libs CACHE PATH "path to SUNDIALS")
endif()

SHOW_VARIABLE(SUNDIALS_DIR PATH
  "SUNDIALS library directory" "${SUNDIALS_DIR}")

  set(SUNDIALS_FIND_QUIETLY ON)

  set(SUNDIALS_REQUIRED IDA KINSOL)
  if (OPENMP_FOUND)
  list(APPEND SUNDIALS_REQUIRED nvecopenmp)
  endif(OPENMP_FOUND)
find_package(SUNDIALS REQUIRED COMPONENTS ${SUNDIALS_REQUIRED} OPTIONAL_COMPONENTS CVODE ARKODE)

  if(SUNDIALS_FOUND)
    list(INSERT external_library_list 0 ${SUNDIALS_LIBRARIES})
  OPTION(FORCE_SUNDIALS_REBUILD "force rebuild of sundials" OFF)
    IF (AUTOBUILD_SUNDIALS)

        IF(FORCE_SUNDIALS_REBUILD)
            include(buildSundials)
            build_sundials()
            set(FORCE_SUNDIALS_REBUILD OFF CACHE BOOL "force rebuild of sundials" FORCE)
        ENDIF(FORCE_SUNDIALS_REBUILD)
    ELSE (AUTOBUILD_SUNDIALS)
       IF(FORCE_SUNDIALS_REBUILD)
            include(buildSundials)
            build_sundials()
            set(SUNDIALS_FOUND OFF CACHE BOOL "sundials not found" FORCE)
            set(SUNDIALS_LIBRARIES NOT_FOUND CACHE FORCE)
            set(FORCE_SUNDIALS_REBUILD OFF CACHE BOOL "force rebuild of sundials" FORCE)
            set(SUNDIALS_DIR ${PROJECT_BINARY_DIR}/libs)
            find_package(SUNDIALS REQUIRED COMPONENTS ${SUNDIALS_REQUIRED} OPTIONAL_COMPONENTS CVODE ARKODE)
        ENDIF(FORCE_SUNDIALS_REBUILD)
    ENDIF(AUTOBUILD_SUNDIALS)
  else(SUNDIALS_FOUND)
    OPTION(AUTOBUILD_SUNDIALS "enable Sundials to automatically download and build" OFF)
    message(STATUS "abs=${AUTOBUILD_SUNDIALS}")
    IF (AUTOBUILD_SUNDIALS)
      include(buildSundials)
      build_sundials()
      set(SUNDIALS_DIR ${PROJECT_BINARY_DIR}/libs)
      find_package(SUNDIALS REQUIRED COMPONENTS ${SUNDIALS_REQUIRED} OPTIONAL_COMPONENTS CVODE ARKODE)
    ENDIF(AUTOBUILD_SUNDIALS)
  if (SUNDIALS_FOUND)
    list(INSERT external_library_list 0 ${SUNDIALS_LIBRARIES})
  else (SUNDIALS_FOUND)
    message( FATAL_ERROR "sundials not found - unable to continue")
    message( "Double check spelling specified libraries (search is case sensitive)")
  endif(SUNDIALS_FOUND)
  endif(SUNDIALS_FOUND)

if (SUNDIALS_ARKODE_FOUND)
set(LOAD_ARKODE TRUE)
endif(SUNDIALS_ARKODE_FOUND)

if (SUNDIALS_CVODE_FOUND)
set(LOAD_CVODE TRUE)
endif(SUNDIALS_CVODE_FOUND)

set(optional_component_test_files)
set(optional_system_test_files)

set(griddyn_optional_libraries )




set (optional_library_key_headers )
set (optional_library_functions )

# -------------------------------------------------------------
# BOOST
# -------------------------------------------------------------

IF (MSVC)
find_path(BOOST_TEST_PATH
            NAMES             boost/version.hpp
            PATHS        C:/boost_1_64_0
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

        if (BOOST_TEST_PATH)
        set(BOOST_ROOT ${BOOST_TEST_PATH})
        endif(BOOST_TEST_PATH)
ENDIF(MSVC)

SHOW_VARIABLE(BOOST_ROOT PATH "Boost root directory" "${BOOST_ROOT}")

IF(MPI_ENABLE)
  find_package(Boost COMPONENTS program_options filesystem serialization mpi system date_time REQUIRED)
ELSE(MPI_ENABLE)
  find_package(Boost COMPONENTS program_options filesystem system date_time REQUIRED)
ENDIF(MPI_ENABLE)

mark_as_advanced(CLEAR BOOST_ROOT)

message("Using Boost include files : ${Boost_INCLUDE_DIR}")
message("Using Boost libraries ${Boost_LIBRARY_DIRS}")

list(APPEND external_library_list ${Boost_LIBRARIES})
  list(APPEND external_link_directories ${Boost_LIBRARY_DIRS})

# -------------------------------------------------------------
# global include directories
# -------------------------------------------------------------
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/libs/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/ThirdParty)
INCLUDE_DIRECTORIES(SYSTEM ${Boost_INCLUDE_DIR})
# -------------------------------------------------------------
# load the required subdirectories
# -------------------------------------------------------------

add_subdirectory(src/formatInterpreters)

add_subdirectory(src/core)
#required for utilities library

add_subdirectory(src/utilities)

add_subdirectory(src/griddyn)

# now go into the optional stuff


# -------------------------------------------------------------
# Enable support for Plugins
# -------------------------------------------------------------

OPTION(ENABLE_PLUGINS "Enable support for plugin modules" OFF)

IF (ENABLE_PLUGINS)
    IF (Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        set(ENABLE_PLUGINS OFF)
        message("Plugin support requires BOOST 1.61 or higher")
    ELSE(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        add_subdirectory(plugins)
    ENDIF(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
ENDIF(ENABLE_PLUGINS)

# -------------------------------------------------------------
# Enable compilation of extraModels?
# -------------------------------------------------------------

OPTION(LOAD_EXTRA_MODELS "Compile and load extraModels" ON)
# If extra models are used enabled try to locate the libraries
# link against them.

if (LOAD_EXTRA_MODELS)
  add_subdirectory(src/extraModels)
ENDIF(LOAD_EXTRA_MODELS)

# -------------------------------------------------------------
# Enable FMI support?
# -------------------------------------------------------------

OPTION(FMI_ENABLE "Enable FMI support" ON)
# If FMI is enabled try to locate the libraries
# link against them.

IF (FMI_ENABLE)
    IF (Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        set(FMI_ENABLE OFF CACHE BOOL "Enable FMI Support" FORCE)
        message("FMI support requires BOOST 1.61 or higher")
    ELSE(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
        add_subdirectory(src/fmi)
    ENDIF(Boost_MAJOR_VERSION==1 AND Boost_MINOR_VERSION<61)
ENDIF(FMI_ENABLE)


# -------------------------------------------------------------
# FSKIT
# -------------------------------------------------------------

OPTION(FSKIT_ENABLE "Enable FSKIT support" OFF)

IF(FSKIT_ENABLE)
  find_package(FSKIT)
  INCLUDE_DIRECTORIES(SYSTEM ${FSKIT_INCLUDE_DIR})
  set(GRIDDYN_HAVE_FSKIT 1)
  add_subdirectory(src/fskit)
ENDIF(FSKIT_ENABLE)

# -------------------------------------------------------------
# Enable 64 bit indexing (enable to allow for more than 2^31 objects)--that would be a very big model
# -------------------------------------------------------------

if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
  OPTION(ENABLE_64_BIT_INDEXING "set all indexing and count variables to 64 bit unsigned (Usually not required)" OFF)
endif("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")

#OPTION(BUILD_SERVERMODE "Build GridDyn Server" OFF)
IF (BUILD_SERVERMODE)
 add_subdirectory(src/gridDynServer)
ENDIF(BUILD_SERVERMODE)




# ------------------------------------------------------------
# Enable message logging
# -------------------------------------------------------------
OPTION(LOG_ENABLE "enable non-warning/error logging messages" ON)

IF(LOG_ENABLE)
OPTION(DEBUG_LOG_ENABLE "enable debug logging messages(Note: logging is still controlled via program options this is for program size and speed)" ON)
OPTION(TRACE_LOG_ENABLE "enable trace logging messages(Note: logging is still controlled via program options this is for program size and speed)" ON)
ENDIF(LOG_ENABLE)


option (GRIDDYN_GENERATE_DOXYGEN_DOC "Generate Doxygen doc target" OFF)

IF (GRIDDYN_GENERATE_DOXYGEN_DOC)
find_package(Doxygen)
if(DOXYGEN_FOUND)

    SHOW_VARIABLE(DOXYGEN_OUTPUT_DIR PATH "location to put Doxygen docs" "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs}
    COMMENT "Generating API documentation with Doxygen" VERBATIM
)
endif(DOXYGEN_FOUND)
endif (GRIDDYN_GENERATE_DOXYGEN_DOC)


# -------------------------------------------------------------
# Enable FNCS executable
# -------------------------------------------------------------

OPTION(FNCS_EXECUTABLE "Enable the FNCS executable to be built" OFF)
# If FNCS_EXECUTABLE is load the fncs directory
# link against them.

IF (FNCS_EXECUTABLE)
    SHOW_VARIABLE(USE_DUMMY_FNCS BOOL "use a dummy fncs implementation if none is available" OFF)
    IF(USE_DUMMY_FNCS)
        HIDE_VARIABLE(USE_FNCS_SHARED_LIBRARY)
        HIDE_VARIABLE(FNCS_INSTALL_PATH)

    ELSE(USE_DUMMY_FNCS)
        SHOW_VARIABLE(USE_FNCS_SHARED_LIBRARY BOOL "used a dynamic shared library for the FNCS interface" OFF)
        set(ZMQ_NEEDED ON)
        IF(USE_FNCS_SHARED_LIBRARY)
            SHOW_VARIABLE(FNCS_INSTALL_PATH PATH "path to the fncs installation" "${FNCS_LIBRARY_PATH}")
            find_package(FNCS)
        ELSE (USE_FNCS_SHARED_LIBRARY)
            HIDE_VARIABLE(FNCS_INSTALL_PATH)
            SHOW_VARIABLE(CZMQ_LIBRARY_PATH PATH  "path to the czmq libraries" "${CZMQ_LIBRARY_PATH}")
            SHOW_VARIABLE(CZMQ_INCLUDE_PATH PATH  "path to the czmq headers" "${CZMQ_INCLUDE_PATH}")
                find_package(CZMQ)
                list(APPEND griddyn_optional_libraries ${CZMQ_LIBRARY})
        ENDIF(USE_FNCS_SHARED_LIBRARY)

    ENDIF(USE_DUMMY_FNCS)
    add_subdirectory(src/fncs)
ELSE(FNCS_EXECUTABLE)
    HIDE_VARIABLE(FNCS_INSTALL_PATH)
    HIDE_VARIABLE(USE_DUMMY_FNCS)
    HIDE_VARIABLE(USE_FNCS_SHARED_LIBRARY)
ENDIF(FNCS_EXECUTABLE)


# -------------------------------------------------------------
# Enable HELICS executable
# -------------------------------------------------------------

OPTION(HELICS_EXECUTABLE "Enable the HELICS executable to be built" OFF)
# If HELICS_EXECUTABLE is load the fncs directory
# link against them.

IF (HELICS_EXECUTABLE)
    SHOW_VARIABLE(HELICS_INSTALL_PATH PATH "path to the helics installation" "${PROJECT_BINARY_DIR}/libs")
    set(ZMQ_NEEDED ON CACHE BOOL "ZMQ is needed" FORCE)
ELSE(HELICS_EXECUTABLE)
    HIDE_VARIABLE(HELICS_INSTALL_PATH)
ENDIF(HELICS_EXECUTABLE)

# -------------------------------------------------------------
# Enable ZMQ interfacing
# -------------------------------------------------------------

OPTION(DIME_ENABLE "Enable connection with Dime" OFF)

IF (DIME_ENABLE)
set(ZMQ_NEEDED ON CACHE BOOL "ZMQ is needed" FORCE)
ENDIF(DIME_ENABLE)
OPTION(ZMQ_ENABLE "Enable ZMQ networking library" ${ZMQ_NEEDED})
# If ZMQ library is enabled try to locate it and link against it

IF ((ZMQ_ENABLE) OR (ZMQ_NEEDED))

SHOW_VARIABLE(ZMQ_LIBRARY_PATH PATH
  "path to the zmq libraries" "${PROJECT_BINARY_DIR}/libs")

SHOW_VARIABLE(ZMQ_INCLUDE_PATH PATH
  "path to the zmq headers" "${PROJECT_BINARY_DIR}/libs")
  set(ZMQ_FIND_QUIETLY ON)
find_package(ZMQ)

if (NOT ZMQ_FOUND)
OPTION(AUTOBUILD_ZMQ "enable ZMQ to automatically download and build" OFF)
  IF (AUTOBUILD_ZMQ)
  include(buildlibZMQ)
  build_libzmq()
  set(ZMQ_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
  find_package(ZMQ)
  ENDIF(AUTOBUILD_ZMQ)

endif(NOT ZMQ_FOUND)


# ZMQ_INCLUDE_DIR, where to find zmq.h
# ZMQ_LIBRARY, the library needed to use ZMQ
# ZMQ_FOUND, if false, you cannot build anything that requires ZMQ.
    if (ZMQ_FOUND)
        add_subdirectory(src/zmqLib)
    else(ZMQ_FOUND)
        message(SEND_ERROR "unable to locate zmq library")
    endif(ZMQ_FOUND)
ENDIF((ZMQ_ENABLE) OR (ZMQ_NEEDED))


IF (HELICS_EXECUTABLE)
    SHOW_VARIABLE(HELICS_INSTALL_PATH PATH "path to the helics installation" "${PROJECT_BINARY_DIR}/libs")
    set(HELICS_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
    find_package(HELICS)
    IF(NOT HELICS_FOUND)
        OPTION(AUTOBUILD_HELICS "enable HELICS to automatically download and build" OFF)
        IF(AUTOBUILD_HELICS)
            include(buildHELICS)
            build_helics()
            set(HELICS_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
            find_package(HELICS)
        ENDIF(AUTOBUILD_HELICS)
    ELSE (NOT HELICS_FOUND)
        OPTION(FORCE_HELICS_REBUILD "force rebuild of helics" OFF)
        IF (AUTOBUILD_HELICS)
            IF(FORCE_HELICS_REBUILD)
                include(buildHELICS)
                build_helics()
                set(HELICS_INSTALL_PATH ${PROJECT_BINARY_DIR}/libs)
                set(FORCE_HELICS_REBUILD OFF CACHE BOOL "force rebuild of helics" FORCE)
            ENDIF(FORCE_HELICS_REBUILD)
        ENDIF(AUTOBUILD_HELICS)
    ENDIF(NOT HELICS_FOUND)

    add_subdirectory(src/helics)
ENDIF(HELICS_EXECUTABLE)
FILE(GLOB KEY_LIBRARY_FILES  ${PROJECT_BINARY_DIR}/libs/bin/*)
message(STATUS "key files ${KEY_LIBRARY_FILES}")
# -------------------------------------------------------------
# load the subdirectories
# -------------------------------------------------------------



OPTION(OPTIMIZATION_ENABLE "Enable Optimization libraries" OFF)
# If OPTIMIZATION is enabled try to locate the libraries
# link against them.
IF(OPTIMIZATION_ENABLE)
    add_subdirectory(src/optimization)
ENDIF(OPTIMIZATION_ENABLE)

add_subdirectory(src/fileInput)

add_subdirectory(src/coupling)

add_subdirectory(src/gridDynCombined)

OPTION(BUILD_SHARED_FMI_LIBRARY "Enable construction of a binary fmi shared library for GridDyn" ON)

IF(BUILD_SHARED_FMI_LIBRARY)
    add_subdirectory(src/fmi_export)
ENDIF(BUILD_SHARED_FMI_LIBRARY)

add_subdirectory(src/gridDynMain)
# -------------------------------------------------------------
# Enable clang analysis and formatting tools
# -------------------------------------------------------------

OPTION(ENABLE_CLANG_TOOLS "if clang is found enable some custom targets for clang formatting and tidy" OFF)

if (ENABLE_CLANG_TOOLS)
include(clang-cxx-dev-tools)
endif(ENABLE_CLANG_TOOLS)
# -------------------------------------------------------------
# Enable testCore construction?
# -------------------------------------------------------------

OPTION(TEST_ENABLE "Enable unit testing construction" ON)

IF (TEST_ENABLE)

#message("otcf:${optional_component_test_files}")
add_subdirectory(test)
#enable_testing()
#add_test(NAME gridDynTest COMMAND testCore)

ENDIF(TEST_ENABLE)

FILE(GLOB efiles "examples/*")
INSTALL(FILES ${efiles} DESTINATION examples)

#SET(binfiles bin/configure.griddyn bin/pgriddyn bin/pgriddyn_debug)
#INSTALL(PROGRAMS ${binfiles} DESTINATION bin)

INSTALL(FILES ${KEY_LIBRARY_FILES} DESTINATION bin)


# -------------------------------------------------------------
# Future Additions (by Jen)
# -------------------------------------------------------------

#adding dlls
# INSTALL(FILES ${LOCATION_OF_FILES} DESTINATION bin)
FILE(GLOB docs "docs/manuals/*")
INSTALL(FILES ${docs} DESTINATION docs)

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
#set(CPACK_PACKAGE_VERSION_PATCH ${GridDyn_VERSION_PATCH})

#THIS LINE MUST BE LAST
include(CPack)
