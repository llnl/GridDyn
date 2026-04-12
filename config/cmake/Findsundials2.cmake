#.rst:
# FindSundials
# ---------
#
# Find Sundials include dirs and libraries
#
# Use this module by invoking find_package with the form::
#
# find_package(sundials
# [version] [EXACT] # Minimum or EXACT version e.g. 1.36.0
# [REQUIRED] # Fail with error if Sundials is not found
# [COMPONENTS <libs>...] # Sundials libraries by their canonical name
# ) # e.g. "date_time" for "libsundials_date_time"
#
# This module finds headers and requested component libraries OR a CMake
# package configuration file provided by a "Sundials CMake" build. For the
# latter case skip to the "Sundials CMake" section below. For the former
# case results are reported in variables::
#
# Sundials_FOUND - True if headers and requested libraries were found
# Sundials_INCLUDE_DIRS - Sundials include directories
# Sundials_LIBRARY_DIRS - Link directories for Sundials libraries
# Sundials_LIBRARIES - Sundials component libraries to be linked
# Sundials_<C>_FOUND - True if component <C> was found (<C> is upper-case)
# Sundials_<C>_LIBRARY - Libraries to link for component <C> (may include
# target_link_libraries debug/optimized keywords)
# Sundials_VERSION - SUNDIALS_VERSION value from sundials/version.hpp
# Sundials_LIB_VERSION - Version string appended to library filenames
# Sundials_MAJOR_VERSION - Sundials major version number (X in X.y.z)
# Sundials_MINOR_VERSION - Sundials minor version number (Y in x.Y.z)
# Sundials_SUBMINOR_VERSION - Sundials subminor version number (Z in x.y.Z)
# Sundials_LIB_DIAGNOSTIC_DEFINITIONS (Windows)
# - Pass to add_definitions() to have diagnostic
# information about Sundials's automatic linking
# displayed during compilation
#
# This module reads hints about search locations from variables::
#
# SUNDIALS_ROOT - Preferred installation prefix
# (or SUNDIALSROOT)
# SUNDIALS_INCLUDEDIR - Preferred include directory e.g. <prefix>/include
# SUNDIALS_LIBRARYDIR - Preferred library directory e.g. <prefix>/lib
# Sundials_NO_SYSTEM_PATHS - Set to ON to disable searching in locations not
# specified by these hint variables. Default is OFF.
# Sundials_ADDITIONAL_VERSIONS
# - List of Sundials versions not known to this module
# (Sundials install locations may contain the version)
#
# and saves search results persistently in CMake cache entries::
#
# Sundials_INCLUDE_DIR - Directory containing Sundials headers
# Sundials_LIBRARY_DIR_RELEASE - Directory containing release Sundials libraries
# Sundials_LIBRARY_DIR_DEBUG - Directory containing debug Sundials libraries
# Sundials_<C>_LIBRARY_DEBUG - Component <C> library debug variant
# Sundials_<C>_LIBRARY_RELEASE - Component <C> library release variant
#
# Users may set these hints or results as cache entries. Projects
# should not read these entries directly but instead use the above
# result variables. Note that some hint names start in upper-case
# "SUNDIALS". One may specify these as environment variables if they are
# not specified as CMake variables or cache entries.
#
# This module first searches for the Sundials header files using the above
# hint variables (excluding SUNDIALS_LIBRARYDIR) and saves the result in
# Sundials_INCLUDE_DIR. Then it searches for requested component libraries
# using the above hints (excluding SUNDIALS_INCLUDEDIR and
# Sundials_ADDITIONAL_VERSIONS), "lib" directories near Sundials_INCLUDE_DIR,
# and the library name configuration settings below. It saves the
# library directories in Sundials_LIBRARY_DIR_DEBUG and
# Sundials_LIBRARY_DIR_RELEASE and individual library
# locations in Sundials_<C>_LIBRARY_DEBUG and Sundials_<C>_LIBRARY_RELEASE.
# When one changes settings used by previous searches in the same build
# tree (excluding environment variables) this module discards previous
# search results affected by the changes and searches again.
#
# Sundials libraries come in many variants encoded in their file name.
# Users or projects may tell this module which variant to find by
# setting variables::
#
# Sundials_USE_MULTITHREADED - Set to OFF to use the non-multithreaded
# libraries ('mt' tag). Default is ON.
# Sundials_USE_STATIC_LIBS - Set to ON to force the use of the static
# libraries. Default is OFF.
# Sundials_USE_STATIC_RUNTIME - Set to ON or OFF to specify whether to use
# libraries linked statically to the C++ runtime
# ('s' tag). Default is platform dependent.
# Sundials_USE_DEBUG_RUNTIME - Set to ON or OFF to specify whether to use
# libraries linked to the MS debug C++ runtime
# ('g' tag). Default is ON.
#
# Other variables one may set to control this module are::
#
# Sundials_DEBUG - Set to ON to enable debug output from FindSundials.
# Please enable this before filing any bug report.
# Sundials_DETAILED_FAILURE_MSG
# - Set to ON to add detailed information to the
# failure message even when the REQUIRED option
# is not given to the find_package call.
# Sundials_REALPATH - Set to ON to resolve symlinks for discovered
# libraries to assist with packaging. For example,
# the "system" component library may be resolved to
# "/usr/lib/libsundials_system.so.1.42.0" instead of
# "/usr/lib/libsundials_system.so". This does not
# affect linking and should not be enabled unless
# the user needs this information.
# Sundials_LIBRARY_DIR - Default value for Sundials_LIBRARY_DIR_RELEASE and
# Sundials_LIBRARY_DIR_DEBUG.
#
#
# Example to find Sundials headers only::
#
# find_package(Sundials 1.36.0)
# if(Sundials_FOUND)
# include_directories(${Sundials_INCLUDE_DIRS})
# add_executable(foo foo.cc)
# endif()
#
# Example to find Sundials headers and some *static* libraries::
#
# set(Sundials_USE_STATIC_LIBS ON) # only find static libs
# set(Sundials_USE_STATIC_RUNTIME OFF)
# find_package(Sundials 1.36.0 COMPONENTS date_time filesystem system ...)
# if(Sundials_FOUND)
# include_directories(${Sundials_INCLUDE_DIRS})
# add_executable(foo foo.cc)
# target_link_libraries(foo ${Sundials_LIBRARIES})
# endif()
#
# Sundials CMake
# ^^^^^^^^^^^
#
# If Sundials was built using the sundials-cmake project it provides a package
# configuration file for use with find_package's Config mode. This
# module looks for the package configuration file called
# SundialsConfig.cmake or sundials-config.cmake and stores the result in cache
# entry "Sundials_DIR". If found, the package configuration file is loaded
# and this module returns with no further action. See documentation of
# the Sundials CMake package configuration for details on what it provides.
#
# Set Sundials_NO_SUNDIALS_CMAKE to ON to disable the search for sundials-cmake.
#=============================================================================
# Copyright 2006-2012 Kitware, Inc.
# Copyright 2006-2008 Andreas Schneider <mail@cynapses.org>
# Copyright 2007 Wengo
# Copyright 2007 Mike Jackson
# Copyright 2008 Andreas Pakulat <apaku@gmx.de>
# Copyright 2008-2012 Philip Lowman <philip@yhbt.com>
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
# License text for the above reference.)
#-------------------------------------------------------------------------------
# Before we go searching, check whether sundials-cmake is available, unless the
# user specifically asked NOT to search for sundials-cmake.
#
# If Sundials_DIR is set, this behaves as any find_package call would. If not,
# it looks at SUNDIALS_ROOT and SUNDIALSROOT to find Sundials.
#
if(NOT Sundials_NO_SUNDIALS_CMAKE)
    # If Sundials_DIR is not set, look for SUNDIALSROOT and SUNDIALS_ROOT as alternatives, since
    # these are more conventional for Sundials.
    if("$ENV{Sundials_DIR}" STREQUAL "")
        if(NOT "$ENV{SUNDIALS_ROOT}" STREQUAL "")
            set(ENV{Sundials_DIR} $ENV{SUNDIALS_ROOT})
        elseif(NOT "$ENV{SUNDIALSROOT}" STREQUAL "")
            set(ENV{Sundials_DIR} $ENV{SUNDIALSROOT})
        endif()
    endif()
    # Do the same find_package call but look specifically for the CMake version. Note that args are
    # passed in the Sundials_FIND_xxxxx variables, so there is no need to delegate them to this
    # find_package call.
    find_package(Sundials QUIET NO_MODULE)
    mark_as_advanced(Sundials_DIR)
    # If we found sundials-cmake, then we're done. Print out what we found. Otherwise let the rest
    # of the module try to find it.
    if(Sundials_FOUND)
        message("Sundials ${Sundials_FIND_VERSION} found.")
        if(Sundials_FIND_COMPONENTS)
            message("Found Sundials components:")
            message(" ${Sundials_FIND_COMPONENTS}")
        endif()
        return()
    endif()
endif()
# -------------------------------------------------------------------------------
# FindSundials functions & macros
#
# ##################################################################################################
#
# Check the existence of the libraries.
#
# ##################################################################################################
# This macro was taken directly from the FindQt4.cmake file that is included with the CMake
# distribution. This is NOT my work. All work was done by the original authors of the FindQt4.cmake
# file. Only minor modifications were made to remove references to Qt and make this file more
# generally applicable And ELSE/ENDIF pairs were removed for readability.
# ##################################################################################################
macro(_Sundials_ADJUST_LIB_VARS basename)
    if(Sundials_INCLUDE_DIR)
        if(Sundials_${basename}_LIBRARY_DEBUG AND Sundials_${basename}_LIBRARY_RELEASE)
            # if the generator supports configuration types then set optimized and debug libraries,
            # or if the CMAKE_BUILD_TYPE has a value
            if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE)
                set(Sundials_${basename}_LIBRARY optimized ${Sundials_${basename}_LIBRARY_RELEASE}
                                                 debug ${Sundials_${basename}_LIBRARY_DEBUG}
                )
            else()
                # if there are no configuration types and CMAKE_BUILD_TYPE has no value then just
                # use the release libraries
                set(Sundials_${basename}_LIBRARY ${Sundials_${basename}_LIBRARY_RELEASE})
            endif()
            # FIXME: This probably should be set for both cases
            set(Sundials_${basename}_LIBRARIES optimized ${Sundials_${basename}_LIBRARY_RELEASE}
                                               debug ${Sundials_${basename}_LIBRARY_DEBUG}
            )
        endif()
        # if only the release version was found, set the debug variable also to the release version
        if(Sundials_${basename}_LIBRARY_RELEASE AND NOT Sundials_${basename}_LIBRARY_DEBUG)
            set(Sundials_${basename}_LIBRARY_DEBUG ${Sundials_${basename}_LIBRARY_RELEASE})
            set(Sundials_${basename}_LIBRARY ${Sundials_${basename}_LIBRARY_RELEASE})
            set(Sundials_${basename}_LIBRARIES ${Sundials_${basename}_LIBRARY_RELEASE})
        endif()
        # if only the debug version was found, set the release variable also to the debug version
        if(Sundials_${basename}_LIBRARY_DEBUG AND NOT Sundials_${basename}_LIBRARY_RELEASE)
            set(Sundials_${basename}_LIBRARY_RELEASE ${Sundials_${basename}_LIBRARY_DEBUG})
            set(Sundials_${basename}_LIBRARY ${Sundials_${basename}_LIBRARY_DEBUG})
            set(Sundials_${basename}_LIBRARIES ${Sundials_${basename}_LIBRARY_DEBUG})
        endif()
        # If the debug & release library ends up being the same, omit the keywords
        if(${Sundials_${basename}_LIBRARY_RELEASE} STREQUAL ${Sundials_${basename}_LIBRARY_DEBUG})
            set(Sundials_${basename}_LIBRARY ${Sundials_${basename}_LIBRARY_RELEASE})
            set(Sundials_${basename}_LIBRARIES ${Sundials_${basename}_LIBRARY_RELEASE})
        endif()
        if(Sundials_${basename}_LIBRARY)
            set(Sundials_${basename}_FOUND ON)
        endif()
    endif()
    # Make variables changeable to the advanced user
    mark_as_advanced(Sundials_${basename}_LIBRARY_RELEASE Sundials_${basename}_LIBRARY_DEBUG)
endmacro()
# Detect changes in used variables. Compares the current variable value with the last one. In short
# form: v != v_LAST -> CHANGED = 1 v is defined, v_LAST not -> CHANGED = 1 v is not defined, but
# v_LAST is -> CHANGED = 1 otherwise -> CHANGED = 0 CHANGED is returned in variable named
# ${changed_var}
macro(_Sundials_CHANGE_DETECT changed_var)
    set(${changed_var} 0)
    foreach(v ${ARGN})
        if(DEFINED _Sundials_COMPONENTS_SEARCHED)
            if(${v})
                if(_${v}_LAST)
                    string(COMPARE NOTEQUAL "${${v}}" "${_${v}_LAST}" _${v}_CHANGED)
                else()
                    set(_${v}_CHANGED 1)
                endif()
            elseif(_${v}_LAST)
                set(_${v}_CHANGED 1)
            endif()
            if(_${v}_CHANGED)
                set(${changed_var} 1)
            endif()
        else()
            set(_${v}_CHANGED 0)
        endif()
    endforeach()
endmacro()
#
# Find the given library (var). Use 'build_type' to support different lib paths for RELEASE or DEBUG
# builds
#
macro(_Sundials_FIND_LIBRARY var build_type)
    find_library(${var} ${ARGN})
    if(${var})
        # If this is the first library found then save Sundials_LIBRARY_DIR_[RELEASE,DEBUG].
        if(NOT Sundials_LIBRARY_DIR_${build_type})
            get_filename_component(_dir "${${var}}" PATH)
            set(Sundials_LIBRARY_DIR_${build_type} "${_dir}"
                CACHE PATH "Sundials library directory ${build_type}" FORCE
            )
        endif()
    elseif(_Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT)
        # Try component-specific hints but do not save Sundials_LIBRARY_DIR_[RELEASE,DEBUG].
        find_library(${var} HINTS ${_Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT} ${ARGN})
    endif()
    # If Sundials_LIBRARY_DIR_[RELEASE,DEBUG] is known then search only there.
    if(Sundials_LIBRARY_DIR_${build_type})
        set(_sundials_LIBRARY_SEARCH_DIRS_${build_type} ${Sundials_LIBRARY_DIR_${build_type}}
                                                        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
        )
        if(Sundials_DEBUG)
            message(
                STATUS
                    "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                    " Sundials_LIBRARY_DIR_${build_type} = ${Sundials_LIBRARY_DIR_${build_type}}"
                    " _sundials_LIBRARY_SEARCH_DIRS_${build_type} = ${_sundials_LIBRARY_SEARCH_DIRS_${build_type}}"
            )
        endif()
    endif()
endmacro()
# -------------------------------------------------------------------------------
#
# Runs compiler with "-dumpversion" and parses major/minor version with a regex.
#
function(_Sundials_COMPILER_DUMPVERSION _OUTPUT_VERSION)
    exec_program(
        ${CMAKE_CXX_COMPILER} ARGS ${CMAKE_CXX_COMPILER_ARG1} -dumpversion
        OUTPUT_VARIABLE _sundials_COMPILER_VERSION
    )
    string(REGEX REPLACE "([0-9])\\.([0-9])(\\.[0-9])?" "\\1\\2" _sundials_COMPILER_VERSION
                         ${_sundials_COMPILER_VERSION}
    )
    set(${_OUTPUT_VERSION} ${_sundials_COMPILER_VERSION} PARENT_SCOPE)
endfunction()
#
# Take a list of libraries with "thread" in it and prepend duplicates with
# "thread_${Sundials_THREADAPI}" at the front of the list
#
function(_Sundials_PREPEND_LIST_WITH_THREADAPI _output)
    set(_orig_libnames ${ARGN})
    string(REPLACE "thread" "thread_${Sundials_THREADAPI}" _threadapi_libnames "${_orig_libnames}")
    set(${_output} ${_threadapi_libnames} ${_orig_libnames} PARENT_SCOPE)
endfunction()
#
# If a library is found, replace its cache entry with its REALPATH
#
function(_Sundials_SWAP_WITH_REALPATH _library _docstring)
    if(${_library})
        get_filename_component(_sundials_filepathreal ${${_library}} REALPATH)
        unset(${_library} CACHE)
        set(${_library} ${_sundials_filepathreal} CACHE FILEPATH "${_docstring}")
    endif()
endfunction()
function(_Sundials_CHECK_SPELLING _var)
    if(${_var})
        string(TOUPPER ${_var} _var_UC)
        message(
            FATAL_ERROR
                "ERROR: ${_var} is not the correct spelling. The proper spelling is ${_var_UC}."
        )
    endif()
endfunction()
# Guesses Sundials's compiler prefix used in built library names Returns the guess by setting the
# variable pointed to by _ret
function(_Sundials_GUESS_COMPILER_PREFIX _ret)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Intel" OR CMAKE_CXX_COMPILER MATCHES "icl"
       OR CMAKE_CXX_COMPILER MATCHES "icpc"
    )
        if(WIN32)
            set(_sundials_COMPILER "-iw")
        else()
            set(_sundials_COMPILER "-il")
        endif()
    elseif(GHSMULTI)
        set(_sundials_COMPILER "-ghs")
    elseif(MSVC14)
        set(_sundials_COMPILER "-vc140")
    elseif(MSVC12)
        set(_sundials_COMPILER "-vc120")
    elseif(MSVC11)
        set(_sundials_COMPILER "-vc110")
    elseif(MSVC10)
        set(_sundials_COMPILER "-vc100")
    elseif(MSVC90)
        set(_sundials_COMPILER "-vc90")
    elseif(MSVC80)
        set(_sundials_COMPILER "-vc80")
    elseif(MSVC71)
        set(_sundials_COMPILER "-vc71")
    elseif(MSVC70) # Good luck!
        set(_sundials_COMPILER "-vc7") # yes, this is correct
    elseif(MSVC60) # Good luck!
        set(_sundials_COMPILER "-vc6") # yes, this is correct
    elseif(BORLAND)
        set(_sundials_COMPILER "-bcb")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "SunPro")
        set(_sundials_COMPILER "-sw")
    elseif(MINGW)
        if(${Sundials_MAJOR_VERSION}.${Sundials_MINOR_VERSION} VERSION_LESS 1.34)
            set(_sundials_COMPILER "-mgw") # no GCC version encoding prior to 1.34
        else()
            _sundials_compiler_dumpversion(_sundials_COMPILER_VERSION)
            set(_sundials_COMPILER "-mgw${_sundials_COMPILER_VERSION}")
        endif()
    elseif(UNIX)
        if(CMAKE_COMPILER_IS_GNUCXX)
            if(${Sundials_MAJOR_VERSION}.${Sundials_MINOR_VERSION} VERSION_LESS 1.34)
                set(_sundials_COMPILER "-gcc") # no GCC version encoding prior to 1.34
            else()
                _sundials_compiler_dumpversion(_sundials_COMPILER_VERSION)
                # Determine which version of GCC we have.
                if(APPLE)
                    if(Sundials_MINOR_VERSION)
                        if(${Sundials_MINOR_VERSION} GREATER 35)
                            # In Sundials 1.36.0 and newer, the mangled compiler name used on Mac OS
                            # X/Darwin is "xgcc".
                            set(_sundials_COMPILER "-xgcc${_sundials_COMPILER_VERSION}")
                        else()
                            # In Sundials <= 1.35.0, there is no mangled compiler name for the Mac
                            # OS X/Darwin version of GCC.
                            set(_sundials_COMPILER "")
                        endif()
                    else()
                        # We don't know the Sundials version, so assume it's pre-1.36.0.
                        set(_sundials_COMPILER "")
                    endif()
                else()
                    set(_sundials_COMPILER "-gcc${_sundials_COMPILER_VERSION}")
                endif()
            endif()
        endif()
    else()
        # TODO at least Sundials_DEBUG here?
        set(_sundials_COMPILER "")
    endif()
    set(${_ret} ${_sundials_COMPILER} PARENT_SCOPE)
endfunction()
#
# End functions/macros
#
# -------------------------------------------------------------------------------
# -------------------------------------------------------------------------------
# main.
# -------------------------------------------------------------------------------
# If the user sets Sundials_LIBRARY_DIR, use it as the default for both configurations.
if(NOT Sundials_LIBRARY_DIR_RELEASE AND Sundials_LIBRARY_DIR)
    set(Sundials_LIBRARY_DIR_RELEASE "${Sundials_LIBRARY_DIR}")
endif()
if(NOT Sundials_LIBRARY_DIR_DEBUG AND Sundials_LIBRARY_DIR)
    set(Sundials_LIBRARY_DIR_DEBUG "${Sundials_LIBRARY_DIR}")
endif()
if(NOT DEFINED Sundials_USE_MULTITHREADED)
    set(Sundials_USE_MULTITHREADED TRUE)
endif()
if(NOT DEFINED Sundials_USE_DEBUG_RUNTIME)
    set(Sundials_USE_DEBUG_RUNTIME TRUE)
endif()
# Check the version of Sundials against the requested version.
if(Sundials_FIND_VERSION AND NOT Sundials_FIND_VERSION_MINOR)
    message(
        SEND_ERROR
            "When requesting a specific version of Sundials, you must provide at least the major and minor version numbers, e.g., 1.34"
    )
endif()
if(Sundials_FIND_VERSION_EXACT)
    # The version may appear in a directory with or without the patch level, even when the patch
    # level is non-zero.
    set(_sundials_TEST_VERSIONS
        "${Sundials_FIND_VERSION_MAJOR}.${Sundials_FIND_VERSION_MINOR}.${Sundials_FIND_VERSION_PATCH}"
        "${Sundials_FIND_VERSION_MAJOR}.${Sundials_FIND_VERSION_MINOR}"
    )
else()
    # The user has not requested an exact version. Among known versions, find those that are
    # acceptable to the user request.
    set(_Sundials_KNOWN_VERSIONS
        ${Sundials_ADDITIONAL_VERSIONS}
        "1.58.0"
        "1.58"
        "1.57.0"
        "1.57"
        "1.56.0"
        "1.56"
        "1.55.0"
        "1.55"
        "1.54.0"
        "1.54"
        "1.53.0"
        "1.53"
        "1.52.0"
        "1.52"
        "1.51.0"
        "1.51"
        "1.50.0"
        "1.50"
        "1.49.0"
        "1.49"
        "1.48.0"
        "1.48"
        "1.47.0"
        "1.47"
        "1.46.1"
        "1.46.0"
        "1.46"
        "1.45.0"
        "1.45"
        "1.44.0"
        "1.44"
        "1.43.0"
        "1.43"
        "1.42.0"
        "1.42"
        "1.41.0"
        "1.41"
        "1.40.0"
        "1.40"
        "1.39.0"
        "1.39"
        "1.38.0"
        "1.38"
        "1.37.0"
        "1.37"
        "1.36.1"
        "1.36.0"
        "1.36"
        "1.35.1"
        "1.35.0"
        "1.35"
        "1.34.1"
        "1.34.0"
        "1.34"
        "1.33.1"
        "1.33.0"
        "1.33"
    )
    set(_sundials_TEST_VERSIONS)
    if(Sundials_FIND_VERSION)
        set(_Sundials_FIND_VERSION_SHORT
            "${Sundials_FIND_VERSION_MAJOR}.${Sundials_FIND_VERSION_MINOR}"
        )
        # Select acceptable versions.
        foreach(version ${_Sundials_KNOWN_VERSIONS})
            if(NOT "${version}" VERSION_LESS "${Sundials_FIND_VERSION}")
                # This version is high enough.
                list(APPEND _sundials_TEST_VERSIONS "${version}")
            elseif("${version}.99" VERSION_EQUAL "${_Sundials_FIND_VERSION_SHORT}.99")
                # This version is a short-form for the requested version with the patch level
                # dropped.
                list(APPEND _sundials_TEST_VERSIONS "${version}")
            endif()
        endforeach()
    else()
        # Any version is acceptable.
        set(_sundials_TEST_VERSIONS "${_Sundials_KNOWN_VERSIONS}")
    endif()
endif()
# The reason that we failed to find Sundials. This will be set to a user-friendly message when we
# fail to find some necessary piece of Sundials.
set(Sundials_ERROR_REASON)
if(Sundials_DEBUG)
    # Output some of their choices
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "_sundials_TEST_VERSIONS = ${_sundials_TEST_VERSIONS}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "Sundials_USE_MULTITHREADED = ${Sundials_USE_MULTITHREADED}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "Sundials_USE_STATIC_LIBS = ${Sundials_USE_STATIC_LIBS}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "Sundials_USE_STATIC_RUNTIME = ${Sundials_USE_STATIC_RUNTIME}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "Sundials_ADDITIONAL_VERSIONS = ${Sundials_ADDITIONAL_VERSIONS}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "Sundials_NO_SYSTEM_PATHS = ${Sundials_NO_SYSTEM_PATHS}"
    )
endif()
if(WIN32)
    # In windows, automatic linking is performed, so you do not have to specify the libraries. If
    # you are linking to a dynamic runtime, then you can choose to link to either a static or a
    # dynamic Sundials library, the default is to do a static link. You can alter this for a
    # specific library "whatever" by defining SUNDIALS_WHATEVER_DYN_LINK to force Sundials library
    # "whatever" to be linked dynamically. Alternatively you can force all Sundials libraries to
    # dynamic link by defining SUNDIALS_ALL_DYN_LINK. This feature can be disabled for Sundials
    # library "whatever" by defining SUNDIALS_WHATEVER_NO_LIB, or for all of Sundials by defining
    # SUNDIALS_ALL_NO_LIB. If you want to observe which libraries are being linked against then
    # defining SUNDIALS_LIB_DIAGNOSTIC will cause the auto-linking code to emit a #pragma message
    # each time a library is selected for linking.
    set(Sundials_LIB_DIAGNOSTIC_DEFINITIONS "-DSUNDIALS_LIB_DIAGNOSTIC")
endif()
_sundials_check_spelling(Sundials_ROOT)
_sundials_check_spelling(Sundials_LIBRARYDIR)
_sundials_check_spelling(Sundials_INCLUDEDIR)
# Collect environment variable inputs as hints. Do not consider changes.
foreach(v SUNDIALSROOT SUNDIALS_ROOT SUNDIALS_INCLUDEDIR SUNDIALS_LIBRARYDIR)
    set(_env $ENV{${v}})
    if(_env)
        file(TO_CMAKE_PATH "${_env}" _ENV_${v})
    else()
        set(_ENV_${v} "")
    endif()
endforeach()
if(NOT _ENV_SUNDIALS_ROOT AND _ENV_SUNDIALSROOT)
    set(_ENV_SUNDIALS_ROOT "${_ENV_SUNDIALSROOT}")
endif()
# Collect inputs and cached results. Detect changes since the last run.
if(NOT SUNDIALS_ROOT AND SUNDIALSROOT)
    set(SUNDIALS_ROOT "${SUNDIALSROOT}")
endif()
set(_Sundials_VARS_DIR SUNDIALS_ROOT Sundials_NO_SYSTEM_PATHS)
if(Sundials_DEBUG)
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "Declared as CMake or Environmental Variables:"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   " SUNDIALS_ROOT = ${SUNDIALS_ROOT}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   " SUNDIALS_INCLUDEDIR = ${SUNDIALS_INCLUDEDIR}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   " SUNDIALS_LIBRARYDIR = ${SUNDIALS_LIBRARYDIR}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "_sundials_TEST_VERSIONS = ${_sundials_TEST_VERSIONS}"
    )
endif()
# ------------------------------------------------------------------------
# Search for Sundials include DIR
# ------------------------------------------------------------------------
set(_Sundials_VARS_INC SUNDIALS_INCLUDEDIR Sundials_INCLUDE_DIR Sundials_ADDITIONAL_VERSIONS)
_sundials_change_detect(_Sundials_CHANGE_INCDIR ${_Sundials_VARS_DIR} ${_Sundials_VARS_INC})
# Clear Sundials_INCLUDE_DIR if it did not change but other input affecting the location did. We
# will find a new one based on the new inputs.
if(_Sundials_CHANGE_INCDIR AND NOT _Sundials_INCLUDE_DIR_CHANGED)
    unset(Sundials_INCLUDE_DIR CACHE)
endif()
if(NOT Sundials_INCLUDE_DIR)
    set(_sundials_INCLUDE_SEARCH_DIRS "")
    if(SUNDIALS_INCLUDEDIR)
        list(APPEND _sundials_INCLUDE_SEARCH_DIRS ${SUNDIALS_INCLUDEDIR})
    elseif(_ENV_SUNDIALS_INCLUDEDIR)
        list(APPEND _sundials_INCLUDE_SEARCH_DIRS ${_ENV_SUNDIALS_INCLUDEDIR})
    endif()
    if(SUNDIALS_ROOT)
        list(APPEND _sundials_INCLUDE_SEARCH_DIRS ${SUNDIALS_ROOT}/include ${SUNDIALS_ROOT})
    elseif(_ENV_SUNDIALS_ROOT)
        list(APPEND _sundials_INCLUDE_SEARCH_DIRS ${_ENV_SUNDIALS_ROOT}/include
             ${_ENV_SUNDIALS_ROOT}
        )
    endif()
    if(Sundials_NO_SYSTEM_PATHS)
        list(APPEND _sundials_INCLUDE_SEARCH_DIRS NO_CMAKE_SYSTEM_PATH)
    else()
        list(
            APPEND
            _sundials_INCLUDE_SEARCH_DIRS
            PATHS
            C:/sundials/include
            C:/sundials
            /sw/local/include
        )
    endif()
    # Try to find Sundials by stepping backwards through the Sundials versions we know about. Build
    # a list of path suffixes for each version.
    set(_sundials_PATH_SUFFIXES)
    foreach(_sundials_VER ${_sundials_TEST_VERSIONS})
        # Add in a path suffix, based on the required version, ideally we could read this from
        # version.hpp, but for that to work we'd need to know the include dir already
        set(_sundials_SUNDIALSIFIED_VERSION)
        # Transform 1.35 => 1_35 and 1.36.0 => 1_36_0
        if(_sundials_VER MATCHES "([0-9]+)\\.([0-9]+)\\.([0-9]+)")
            set(_sundials_SUNDIALSIFIED_VERSION
                "${CMAKE_MATCH_1}_${CMAKE_MATCH_2}_${CMAKE_MATCH_3}"
            )
        elseif(_sundials_VER MATCHES "([0-9]+)\\.([0-9]+)")
            set(_sundials_SUNDIALSIFIED_VERSION "${CMAKE_MATCH_1}_${CMAKE_MATCH_2}")
        endif()
        list(
            APPEND
            _sundials_PATH_SUFFIXES
            "sundials-${_sundials_SUNDIALSIFIED_VERSION}"
            "sundials_${_sundials_SUNDIALSIFIED_VERSION}"
            "sundials/sundials-${_sundials_SUNDIALSIFIED_VERSION}"
            "sundials/sundials_${_sundials_SUNDIALSIFIED_VERSION}"
        )
    endforeach()
    if(Sundials_DEBUG)
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       "Include debugging info:"
        )
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       " _sundials_INCLUDE_SEARCH_DIRS = ${_sundials_INCLUDE_SEARCH_DIRS}"
        )
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       " _sundials_PATH_SUFFIXES = ${_sundials_PATH_SUFFIXES}"
        )
    endif()
    # Look for a standard sundials header file.
    find_path(
        Sundials_INCLUDE_DIR
        NAMES sundials/config.hpp
        HINTS ${_sundials_INCLUDE_SEARCH_DIRS}
        PATH_SUFFIXES ${_sundials_PATH_SUFFIXES}
    )
endif()
# ------------------------------------------------------------------------
# Extract version information from version.hpp
# ------------------------------------------------------------------------
# Set Sundials_FOUND based only on header location and version. It will be updated below for
# component libraries.
if(Sundials_INCLUDE_DIR)
    if(Sundials_DEBUG)
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       "location of version.hpp: ${Sundials_INCLUDE_DIR}/sundials/version.hpp"
        )
    endif()
    # Extract Sundials_VERSION and Sundials_LIB_VERSION from version.hpp
    set(Sundials_VERSION 0)
    set(Sundials_LIB_VERSION "")
    file(STRINGS "${Sundials_INCLUDE_DIR}/sundials/version.hpp" _sundials_VERSION_HPP_CONTENTS
         REGEX "#define SUNDIALS_(LIB_)?VERSION "
    )
    set(_Sundials_VERSION_REGEX "([0-9]+)")
    set(_Sundials_LIB_VERSION_REGEX "\"([0-9_]+)\"")
    foreach(v VERSION LIB_VERSION)
        if("${_sundials_VERSION_HPP_CONTENTS}" MATCHES
           "#define SUNDIALS_${v} ${_Sundials_${v}_REGEX}"
        )
            set(Sundials_${v} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    unset(_sundials_VERSION_HPP_CONTENTS)
    math(EXPR Sundials_MAJOR_VERSION "${Sundials_VERSION} / 100000")
    math(EXPR Sundials_MINOR_VERSION "${Sundials_VERSION} / 100 % 1000")
    math(EXPR Sundials_SUBMINOR_VERSION "${Sundials_VERSION} % 100")
    set(Sundials_ERROR_REASON
        "${Sundials_ERROR_REASON}Sundials version: ${Sundials_MAJOR_VERSION}.${Sundials_MINOR_VERSION}.${Sundials_SUBMINOR_VERSION}\nSundials include path: ${Sundials_INCLUDE_DIR}"
    )
    if(Sundials_DEBUG)
        message(
            STATUS
                "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                "version.hpp reveals sundials "
                "${Sundials_MAJOR_VERSION}.${Sundials_MINOR_VERSION}.${Sundials_SUBMINOR_VERSION}"
        )
    endif()
    if(Sundials_FIND_VERSION)
        # Set Sundials_FOUND based on requested version.
        set(_Sundials_VERSION
            "${Sundials_MAJOR_VERSION}.${Sundials_MINOR_VERSION}.${Sundials_SUBMINOR_VERSION}"
        )
        if("${_Sundials_VERSION}" VERSION_LESS "${Sundials_FIND_VERSION}")
            set(Sundials_FOUND 0)
            set(_Sundials_VERSION_AGE "old")
        elseif(Sundials_FIND_VERSION_EXACT AND NOT "${_Sundials_VERSION}" VERSION_EQUAL
                                               "${Sundials_FIND_VERSION}"
        )
            set(Sundials_FOUND 0)
            set(_Sundials_VERSION_AGE "new")
        else()
            set(Sundials_FOUND 1)
        endif()
        if(NOT Sundials_FOUND)
            # State that we found a version of Sundials that is too new or too old.
            set(Sundials_ERROR_REASON
                "${Sundials_ERROR_REASON}\nDetected version of Sundials is too ${_Sundials_VERSION_AGE}. Requested version was ${Sundials_FIND_VERSION_MAJOR}.${Sundials_FIND_VERSION_MINOR}"
            )
            if(Sundials_FIND_VERSION_PATCH)
                set(Sundials_ERROR_REASON "${Sundials_ERROR_REASON}.${Sundials_FIND_VERSION_PATCH}")
            endif()
            if(NOT Sundials_FIND_VERSION_EXACT)
                set(Sundials_ERROR_REASON "${Sundials_ERROR_REASON} (or newer)")
            endif()
            set(Sundials_ERROR_REASON "${Sundials_ERROR_REASON}.")
        endif()
    else()
        # Caller will accept any Sundials version.
        set(Sundials_FOUND 1)
    endif()
else()
    set(Sundials_FOUND 0)
    set(Sundials_ERROR_REASON
        "${Sundials_ERROR_REASON}Unable to find the Sundials header files. Please set SUNDIALS_ROOT to the root directory containing Sundials or SUNDIALS_INCLUDEDIR to the directory containing Sundials's headers."
    )
endif()
# ------------------------------------------------------------------------
# Prefix initialization
# ------------------------------------------------------------------------
set(Sundials_LIB_PREFIX "")
if((GHSMULTI AND Sundials_USE_STATIC_LIBS) OR (WIN32 AND Sundials_USE_STATIC_LIBS AND NOT CYGWIN))
    set(Sundials_LIB_PREFIX "lib")
endif()
if(NOT Sundials_NAMESPACE)
    set(Sundials_NAMESPACE "sundials")
endif()
# ------------------------------------------------------------------------
# Suffix initialization and compiler suffix detection.
# ------------------------------------------------------------------------
set(_Sundials_VARS_NAME
    Sundials_NAMESPACE
    Sundials_COMPILER
    Sundials_THREADAPI
    Sundials_USE_DEBUG_PYTHON
    Sundials_USE_MULTITHREADED
    Sundials_USE_STATIC_LIBS
    Sundials_USE_STATIC_RUNTIME
    Sundials_USE_STLPORT
    Sundials_USE_STLPORT_DEPRECATED_NATIVE_IOSTREAMS
)
_sundials_change_detect(_Sundials_CHANGE_LIBNAME ${_Sundials_VARS_NAME})
# Setting some more suffixes for the library
if(Sundials_COMPILER)
    set(_sundials_COMPILER ${Sundials_COMPILER})
    if(Sundials_DEBUG)
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       "using user-specified Sundials_COMPILER = ${_sundials_COMPILER}"
        )
    endif()
else()
    # Attempt to guess the compiler suffix NOTE: this is not perfect yet, if you experience any
    # issues please report them and use the Sundials_COMPILER variable to work around the problems.
    _sundials_guess_compiler_prefix(_sundials_COMPILER)
    if(Sundials_DEBUG)
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       "guessed _sundials_COMPILER = ${_sundials_COMPILER}"
        )
    endif()
endif()
set(_sundials_MULTITHREADED "-mt")
if(NOT Sundials_USE_MULTITHREADED)
    set(_sundials_MULTITHREADED "")
endif()
if(Sundials_DEBUG)
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "_sundials_MULTITHREADED = ${_sundials_MULTITHREADED}"
    )
endif()
# ======================
# Systematically build up the Sundials ABI tag
# http://sundials.org/doc/libs/1_41_0/more/getting_started/windows.html#library-naming
set(_sundials_RELEASE_ABI_TAG "-")
set(_sundials_DEBUG_ABI_TAG "-")
# Key Use this library when: s linking statically to the C++ standard library and compiler runtime
# support libraries.
if(Sundials_USE_STATIC_RUNTIME)
    set(_sundials_RELEASE_ABI_TAG "${_sundials_RELEASE_ABI_TAG}s")
    set(_sundials_DEBUG_ABI_TAG "${_sundials_DEBUG_ABI_TAG}s")
endif()
# g using debug versions of the standard and runtime support libraries
if(WIN32 AND Sundials_USE_DEBUG_RUNTIME)
    if(MSVC OR "${CMAKE_CXX_COMPILER}" MATCHES "icl" OR "${CMAKE_CXX_COMPILER}" MATCHES "icpc")
        set(_sundials_DEBUG_ABI_TAG "${_sundials_DEBUG_ABI_TAG}g")
    endif()
endif()
# y using special debug build of python
if(Sundials_USE_DEBUG_PYTHON)
    set(_sundials_DEBUG_ABI_TAG "${_sundials_DEBUG_ABI_TAG}y")
endif()
# d using a debug version of your code
set(_sundials_DEBUG_ABI_TAG "${_sundials_DEBUG_ABI_TAG}d")
# p using the STLport standard library rather than the default one supplied with your compiler
if(Sundials_USE_STLPORT)
    set(_sundials_RELEASE_ABI_TAG "${_sundials_RELEASE_ABI_TAG}p")
    set(_sundials_DEBUG_ABI_TAG "${_sundials_DEBUG_ABI_TAG}p")
endif()
# n using the STLport deprecated "native iostreams" feature
if(Sundials_USE_STLPORT_DEPRECATED_NATIVE_IOSTREAMS)
    set(_sundials_RELEASE_ABI_TAG "${_sundials_RELEASE_ABI_TAG}n")
    set(_sundials_DEBUG_ABI_TAG "${_sundials_DEBUG_ABI_TAG}n")
endif()
if(Sundials_DEBUG)
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "_sundials_RELEASE_ABI_TAG = ${_sundials_RELEASE_ABI_TAG}"
    )
    message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                   "_sundials_DEBUG_ABI_TAG = ${_sundials_DEBUG_ABI_TAG}"
    )
endif()
# ------------------------------------------------------------------------
# Begin finding sundials libraries
# ------------------------------------------------------------------------
set(_Sundials_VARS_LIB "")
foreach(c DEBUG RELEASE)
    set(_Sundials_VARS_LIB_${c} SUNDIALS_LIBRARYDIR Sundials_LIBRARY_DIR_${c})
    list(APPEND _Sundials_VARS_LIB ${_Sundials_VARS_LIB_${c}})
    _sundials_change_detect(
        _Sundials_CHANGE_LIBDIR_${c} ${_Sundials_VARS_DIR} ${_Sundials_VARS_LIB_${c}}
        Sundials_INCLUDE_DIR
    )
    # Clear Sundials_LIBRARY_DIR_${c} if it did not change but other input affecting the location
    # did. We will find a new one based on the new inputs.
    if(_Sundials_CHANGE_LIBDIR_${c} AND NOT _Sundials_LIBRARY_DIR_${c}_CHANGED)
        unset(Sundials_LIBRARY_DIR_${c} CACHE)
    endif()
    # If Sundials_LIBRARY_DIR_[RELEASE,DEBUG] is set, prefer its value.
    if(Sundials_LIBRARY_DIR_${c})
        set(_sundials_LIBRARY_SEARCH_DIRS_${c} ${Sundials_LIBRARY_DIR_${c}} NO_DEFAULT_PATH
                                               NO_CMAKE_FIND_ROOT_PATH
        )
    else()
        set(_sundials_LIBRARY_SEARCH_DIRS_${c} "")
        if(SUNDIALS_LIBRARYDIR)
            list(APPEND _sundials_LIBRARY_SEARCH_DIRS_${c} ${SUNDIALS_LIBRARYDIR})
        elseif(_ENV_SUNDIALS_LIBRARYDIR)
            list(APPEND _sundials_LIBRARY_SEARCH_DIRS_${c} ${_ENV_SUNDIALS_LIBRARYDIR})
        endif()
        if(SUNDIALS_ROOT)
            list(APPEND _sundials_LIBRARY_SEARCH_DIRS_${c} ${SUNDIALS_ROOT}/lib
                 ${SUNDIALS_ROOT}/stage/lib
            )
        elseif(_ENV_SUNDIALS_ROOT)
            list(APPEND _sundials_LIBRARY_SEARCH_DIRS_${c} ${_ENV_SUNDIALS_ROOT}/lib
                 ${_ENV_SUNDIALS_ROOT}/stage/lib
            )
        endif()
        list(APPEND _sundials_LIBRARY_SEARCH_DIRS_${c} ${Sundials_INCLUDE_DIR}/lib
             ${Sundials_INCLUDE_DIR}/../lib ${Sundials_INCLUDE_DIR}/stage/lib
        )
        if(Sundials_NO_SYSTEM_PATHS)
            list(APPEND _sundials_LIBRARY_SEARCH_DIRS_${c} NO_CMAKE_SYSTEM_PATH)
        else()
            list(
                APPEND
                _sundials_LIBRARY_SEARCH_DIRS_${c}
                PATHS
                C:/sundials/lib
                C:/sundials
                /sw/local/lib
            )
        endif()
    endif()
endforeach()
if(Sundials_DEBUG)
    message(
        STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
               "_sundials_LIBRARY_SEARCH_DIRS_RELEASE = ${_sundials_LIBRARY_SEARCH_DIRS_RELEASE}"
               "_sundials_LIBRARY_SEARCH_DIRS_DEBUG = ${_sundials_LIBRARY_SEARCH_DIRS_DEBUG}"
    )
endif()
# Support preference of static libs by adjusting CMAKE_FIND_LIBRARY_SUFFIXES
if(Sundials_USE_STATIC_LIBS)
    set(_sundials_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
endif()
# We want to use the tag inline below without risking double dashes
if(_sundials_RELEASE_ABI_TAG)
    if(${_sundials_RELEASE_ABI_TAG} STREQUAL "-")
        set(_sundials_RELEASE_ABI_TAG "")
    endif()
endif()
if(_sundials_DEBUG_ABI_TAG)
    if(${_sundials_DEBUG_ABI_TAG} STREQUAL "-")
        set(_sundials_DEBUG_ABI_TAG "")
    endif()
endif()
# The previous behavior of FindSundials when Sundials_USE_STATIC_LIBS was enabled on WIN32 was to:
# 1. Search for static libs compiled against a SHARED C++ standard runtime library (use if found) 2.
# Search for static libs compiled against a STATIC C++ standard runtime library (use if found) We
# maintain this behavior since changing it could break people's builds. To disable the ambiguous
# behavior, the user need only set Sundials_USE_STATIC_RUNTIME either ON or OFF.
set(_sundials_STATIC_RUNTIME_WORKAROUND false)
if(WIN32 AND Sundials_USE_STATIC_LIBS)
    if(NOT DEFINED Sundials_USE_STATIC_RUNTIME)
        set(_sundials_STATIC_RUNTIME_WORKAROUND true)
    endif()
endif()
# On versions < 1.35, remove the System library from the considered list since it wasn't added until
# 1.35.
if(Sundials_VERSION AND Sundials_FIND_COMPONENTS)
    if(Sundials_VERSION LESS 103500)
        list(REMOVE_ITEM Sundials_FIND_COMPONENTS system)
    endif()
endif()
# If the user changed any of our control inputs flush previous results.
if(_Sundials_CHANGE_LIBDIR OR _Sundials_CHANGE_LIBNAME)
    foreach(COMPONENT ${_Sundials_COMPONENTS_SEARCHED})
        string(TOUPPER ${COMPONENT} UPPERCOMPONENT)
        foreach(c DEBUG RELEASE)
            set(_var Sundials_${UPPERCOMPONENT}_LIBRARY_${c})
            unset(${_var} CACHE)
            set(${_var} "${_var}-NOTFOUND")
        endforeach()
    endforeach()
    set(_Sundials_COMPONENTS_SEARCHED "")
endif()
foreach(COMPONENT ${Sundials_FIND_COMPONENTS})
    string(TOUPPER ${COMPONENT} UPPERCOMPONENT)
    set(_sundials_docstring_release "Sundials ${COMPONENT} library (release)")
    set(_sundials_docstring_debug "Sundials ${COMPONENT} library (debug)")
    # Compute component-specific hints.
    set(_Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT "")
    if(${COMPONENT} STREQUAL "mpi" OR ${COMPONENT} STREQUAL "mpi_python" OR ${COMPONENT} STREQUAL
                                                                            "graph_parallel"
    )
        foreach(lib ${MPI_CXX_LIBRARIES} ${MPI_C_LIBRARIES})
            if(IS_ABSOLUTE "${lib}")
                get_filename_component(libdir "${lib}" PATH)
                string(REPLACE "\\" "/" libdir "${libdir}")
                list(APPEND _Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT ${libdir})
            endif()
        endforeach()
    endif()
    # Consolidate and report component-specific hints.
    if(_Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT)
        list(REMOVE_DUPLICATES _Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT)
        if(Sundials_DEBUG)
            message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                           "Component-specific library search paths for ${COMPONENT}: "
                           "${_Sundials_FIND_LIBRARY_HINTS_FOR_COMPONENT}"
            )
        endif()
    endif()
    #
    # Find RELEASE libraries
    #
    set(_sundials_RELEASE_NAMES
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_RELEASE_ABI_TAG}-${Sundials_LIB_VERSION}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_RELEASE_ABI_TAG}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_RELEASE_ABI_TAG}-${Sundials_LIB_VERSION}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_RELEASE_ABI_TAG}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}
    )
    if(_sundials_STATIC_RUNTIME_WORKAROUND)
        set(_sundials_RELEASE_STATIC_ABI_TAG "-s${_sundials_RELEASE_ABI_TAG}")
        list(
            APPEND
            _sundials_RELEASE_NAMES
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_RELEASE_STATIC_ABI_TAG}-${Sundials_LIB_VERSION}
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_RELEASE_STATIC_ABI_TAG}
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_RELEASE_STATIC_ABI_TAG}-${Sundials_LIB_VERSION}
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_RELEASE_STATIC_ABI_TAG}
        )
    endif()
    if(Sundials_THREADAPI AND ${COMPONENT} STREQUAL "thread")
        _sundials_prepend_list_with_threadapi(_sundials_RELEASE_NAMES ${_sundials_RELEASE_NAMES})
    endif()
    if(Sundials_DEBUG)
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       "Searching for ${UPPERCOMPONENT}_LIBRARY_RELEASE: ${_sundials_RELEASE_NAMES}"
        )
    endif()
    # if Sundials_LIBRARY_DIR_RELEASE is not defined, but Sundials_LIBRARY_DIR_DEBUG is, look there
    # first for RELEASE libs
    if(NOT Sundials_LIBRARY_DIR_RELEASE AND Sundials_LIBRARY_DIR_DEBUG)
        list(INSERT _sundials_LIBRARY_SEARCH_DIRS_RELEASE 0 ${Sundials_LIBRARY_DIR_DEBUG})
    endif()
    # Avoid passing backslashes to _Sundials_FIND_LIBRARY due to macro re-parsing.
    string(REPLACE "\\" "/" _sundials_LIBRARY_SEARCH_DIRS_tmp
                   "${_sundials_LIBRARY_SEARCH_DIRS_RELEASE}"
    )
    _sundials_find_library(
        Sundials_${UPPERCOMPONENT}_LIBRARY_RELEASE
        RELEASE
        NAMES
        ${_sundials_RELEASE_NAMES}
        HINTS
        ${_sundials_LIBRARY_SEARCH_DIRS_tmp}
        NAMES_PER_DIR
        DOC
        "${_sundials_docstring_release}"
    )
    #
    # Find DEBUG libraries
    #
    set(_sundials_DEBUG_NAMES
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_DEBUG_ABI_TAG}-${Sundials_LIB_VERSION}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_DEBUG_ABI_TAG}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_DEBUG_ABI_TAG}-${Sundials_LIB_VERSION}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_DEBUG_ABI_TAG}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}
        ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}
    )
    if(_sundials_STATIC_RUNTIME_WORKAROUND)
        set(_sundials_DEBUG_STATIC_ABI_TAG "-s${_sundials_DEBUG_ABI_TAG}")
        list(
            APPEND
            _sundials_DEBUG_NAMES
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_DEBUG_STATIC_ABI_TAG}-${Sundials_LIB_VERSION}
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_COMPILER}${_sundials_MULTITHREADED}${_sundials_DEBUG_STATIC_ABI_TAG}
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_DEBUG_STATIC_ABI_TAG}-${Sundials_LIB_VERSION}
            ${Sundials_LIB_PREFIX}${Sundials_NAMESPACE}_${COMPONENT}${_sundials_MULTITHREADED}${_sundials_DEBUG_STATIC_ABI_TAG}
        )
    endif()
    if(Sundials_THREADAPI AND ${COMPONENT} STREQUAL "thread")
        _sundials_prepend_list_with_threadapi(_sundials_DEBUG_NAMES ${_sundials_DEBUG_NAMES})
    endif()
    if(Sundials_DEBUG)
        message(STATUS "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] "
                       "Searching for ${UPPERCOMPONENT}_LIBRARY_DEBUG: ${_sundials_DEBUG_NAMES}"
        )
    endif()
    # if Sundials_LIBRARY_DIR_DEBUG is not defined, but Sundials_LIBRARY_DIR_RELEASE is, look there
    # first for DEBUG libs
    if(NOT Sundials_LIBRARY_DIR_DEBUG AND Sundials_LIBRARY_DIR_RELEASE)
        list(INSERT _sundials_LIBRARY_SEARCH_DIRS_DEBUG 0 ${Sundials_LIBRARY_DIR_RELEASE})
    endif()
    # Avoid passing backslashes to _Sundials_FIND_LIBRARY due to macro re-parsing.
    string(REPLACE "\\" "/" _sundials_LIBRARY_SEARCH_DIRS_tmp
                   "${_sundials_LIBRARY_SEARCH_DIRS_DEBUG}"
    )
    _sundials_find_library(
        Sundials_${UPPERCOMPONENT}_LIBRARY_DEBUG
        DEBUG
        NAMES
        ${_sundials_DEBUG_NAMES}
        HINTS
        ${_sundials_LIBRARY_SEARCH_DIRS_tmp}
        NAMES_PER_DIR
        DOC
        "${_sundials_docstring_debug}"
    )
    if(Sundials_REALPATH)
        _sundials_swap_with_realpath(
            Sundials_${UPPERCOMPONENT}_LIBRARY_RELEASE "${_sundials_docstring_release}"
        )
        _sundials_swap_with_realpath(
            Sundials_${UPPERCOMPONENT}_LIBRARY_DEBUG "${_sundials_docstring_debug}"
        )
    endif()
    _sundials_adjust_lib_vars(${UPPERCOMPONENT})
endforeach()
# Restore the original find library ordering
if(Sundials_USE_STATIC_LIBS)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_sundials_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
endif()
# ------------------------------------------------------------------------
# End finding sundials libraries
# ------------------------------------------------------------------------
set(Sundials_INCLUDE_DIRS ${Sundials_INCLUDE_DIR})
set(Sundials_LIBRARY_DIRS)
if(Sundials_LIBRARY_DIR_RELEASE)
    list(APPEND Sundials_LIBRARY_DIRS ${Sundials_LIBRARY_DIR_RELEASE})
endif()
if(Sundials_LIBRARY_DIR_DEBUG)
    list(APPEND Sundials_LIBRARY_DIRS ${Sundials_LIBRARY_DIR_DEBUG})
endif()
if(Sundials_LIBRARY_DIRS)
    list(REMOVE_DUPLICATES Sundials_LIBRARY_DIRS)
endif()
# The above setting of Sundials_FOUND was based only on the header files. Update it for the
# requested component libraries.
if(Sundials_FOUND)
    # The headers were found. Check for requested component libs.
    set(_sundials_CHECKED_COMPONENT FALSE)
    set(_Sundials_MISSING_COMPONENTS "")
    foreach(COMPONENT ${Sundials_FIND_COMPONENTS})
        string(TOUPPER ${COMPONENT} COMPONENT)
        set(_sundials_CHECKED_COMPONENT TRUE)
        if(NOT Sundials_${COMPONENT}_FOUND)
            string(TOLOWER ${COMPONENT} COMPONENT)
            list(APPEND _Sundials_MISSING_COMPONENTS ${COMPONENT})
        endif()
    endforeach()
    if(Sundials_DEBUG)
        message(
            STATUS
                "[ ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE} ] Sundials_FOUND = ${Sundials_FOUND}"
        )
    endif()
    if(_Sundials_MISSING_COMPONENTS)
        set(Sundials_FOUND 0)
        # We were unable to find some libraries, so generate a sensible error message that lists the
        # libraries we were unable to find.
        set(Sundials_ERROR_REASON "${Sundials_ERROR_REASON}\nCould not find the following")
        if(Sundials_USE_STATIC_LIBS)
            set(Sundials_ERROR_REASON "${Sundials_ERROR_REASON} static")
        endif()
        set(Sundials_ERROR_REASON "${Sundials_ERROR_REASON} Sundials libraries:\n")
        foreach(COMPONENT ${_Sundials_MISSING_COMPONENTS})
            set(Sundials_ERROR_REASON
                "${Sundials_ERROR_REASON} ${Sundials_NAMESPACE}_${COMPONENT}\n"
            )
        endforeach()
        list(LENGTH Sundials_FIND_COMPONENTS Sundials_NUM_COMPONENTS_WANTED)
        list(LENGTH _Sundials_MISSING_COMPONENTS Sundials_NUM_MISSING_COMPONENTS)
        if(${Sundials_NUM_COMPONENTS_WANTED} EQUAL ${Sundials_NUM_MISSING_COMPONENTS})
            set(Sundials_ERROR_REASON
                "${Sundials_ERROR_REASON}No Sundials libraries were found. You may need to set SUNDIALS_LIBRARYDIR to the directory containing Sundials libraries or SUNDIALS_ROOT to the location of Sundials."
            )
        else()
            set(Sundials_ERROR_REASON
                "${Sundials_ERROR_REASON}Some (but not all) of the required Sundials libraries were found. You may need to install these additional Sundials libraries. Alternatively, set SUNDIALS_LIBRARYDIR to the directory containing Sundials libraries or SUNDIALS_ROOT to the location of Sundials."
            )
        endif()
    endif()
    if(NOT Sundials_LIBRARY_DIRS AND NOT _sundials_CHECKED_COMPONENT)
        # Compatibility Code for backwards compatibility with CMake 2.4's FindSundials module. Look
        # for the sundials library path. Note that the user may not have installed any libraries so
        # it is quite possible the Sundials_LIBRARY_DIRS may not exist.
        set(_sundials_LIB_DIR ${Sundials_INCLUDE_DIR})
        if("${_sundials_LIB_DIR}" MATCHES "sundials-[0-9]+")
            get_filename_component(_sundials_LIB_DIR ${_sundials_LIB_DIR} PATH)
        endif()
        if("${_sundials_LIB_DIR}" MATCHES "/include$")
            # Strip off the trailing "/include" in the path.
            get_filename_component(_sundials_LIB_DIR ${_sundials_LIB_DIR} PATH)
        endif()
        if(EXISTS "${_sundials_LIB_DIR}/lib")
            set(_sundials_LIB_DIR ${_sundials_LIB_DIR}/lib)
        else()
            if(EXISTS "${_sundials_LIB_DIR}/stage/lib")
                set(_sundials_LIB_DIR ${_sundials_LIB_DIR}/stage/lib)
            else()
                set(_sundials_LIB_DIR "")
            endif()
        endif()
        if(_sundials_LIB_DIR AND EXISTS "${_sundials_LIB_DIR}")
            set(Sundials_LIBRARY_DIRS ${_sundials_LIB_DIR})
        endif()
    endif()
else()
    # Sundials headers were not found so no components were found.
    foreach(COMPONENT ${Sundials_FIND_COMPONENTS})
        string(TOUPPER ${COMPONENT} UPPERCOMPONENT)
        set(Sundials_${UPPERCOMPONENT}_FOUND 0)
    endforeach()
endif()
# ------------------------------------------------------------------------
# Notification to end user about what was found
# ------------------------------------------------------------------------
set(Sundials_LIBRARIES "")
if(Sundials_FOUND)
    if(NOT Sundials_FIND_QUIETLY)
        message(
            STATUS
                "Sundials version: ${Sundials_MAJOR_VERSION}.${Sundials_MINOR_VERSION}.${Sundials_SUBMINOR_VERSION}"
        )
        if(Sundials_FIND_COMPONENTS)
            message(STATUS "Found the following Sundials libraries:")
        endif()
    endif()
    foreach(COMPONENT ${Sundials_FIND_COMPONENTS})
        string(TOUPPER ${COMPONENT} UPPERCOMPONENT)
        if(Sundials_${UPPERCOMPONENT}_FOUND)
            if(NOT Sundials_FIND_QUIETLY)
                message(STATUS " ${COMPONENT}")
            endif()
            list(APPEND Sundials_LIBRARIES ${Sundials_${UPPERCOMPONENT}_LIBRARY})
        endif()
    endforeach()
else()
    if(Sundials_FIND_REQUIRED)
        message(
            SEND_ERROR "Unable to find the requested Sundials libraries.\n${Sundials_ERROR_REASON}"
        )
    else()
        if(NOT Sundials_FIND_QUIETLY)
            # we opt not to automatically output Sundials_ERROR_REASON here as it could be quite
            # lengthy and somewhat imposing in its requests Since Sundials is not always a required
            # dependency we'll leave this up to the end-user.
            if(Sundials_DEBUG OR Sundials_DETAILED_FAILURE_MSG)
                message(STATUS "Could NOT find Sundials\n${Sundials_ERROR_REASON}")
            else()
                message(STATUS "Could NOT find Sundials")
            endif()
        endif()
    endif()
endif()
# Configure display of cache entries in GUI.
foreach(v SUNDIALSROOT SUNDIALS_ROOT ${_Sundials_VARS_INC} ${_Sundials_VARS_LIB})
    get_property(_type CACHE ${v} PROPERTY TYPE)
    if(_type)
        set_property(CACHE ${v} PROPERTY ADVANCED 1)
        if("x${_type}" STREQUAL "xUNINITIALIZED")
            if("x${v}" STREQUAL "xSundials_ADDITIONAL_VERSIONS")
                set_property(CACHE ${v} PROPERTY TYPE STRING)
            else()
                set_property(CACHE ${v} PROPERTY TYPE PATH)
            endif()
        endif()
    endif()
endforeach()
# Record last used values of input variables so we can detect on the next run if the user changed
# them.
foreach(v ${_Sundials_VARS_INC} ${_Sundials_VARS_LIB} ${_Sundials_VARS_DIR} ${_Sundials_VARS_NAME})
    if(DEFINED ${v})
        set(_${v}_LAST "${${v}}" CACHE INTERNAL "Last used ${v} value.")
    else()
        unset(_${v}_LAST CACHE)
    endif()
endforeach()
# Maintain a persistent list of components requested anywhere since the last flush.
set(_Sundials_COMPONENTS_SEARCHED "${_Sundials_COMPONENTS_SEARCHED}")
list(APPEND _Sundials_COMPONENTS_SEARCHED ${Sundials_FIND_COMPONENTS})
list(REMOVE_DUPLICATES _Sundials_COMPONENTS_SEARCHED)
list(SORT _Sundials_COMPONENTS_SEARCHED)
set(_Sundials_COMPONENTS_SEARCHED "${_Sundials_COMPONENTS_SEARCHED}"
    CACHE INTERNAL "Components requested for this build tree."
)
