# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Copyright (c) 2017-2019, Battelle Memorial Institute; Lawrence Livermore
# National Security, LLC; Alliance for Sustainable Energy, LLC.
# See the top-level NOTICE for additional details.
# All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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

# Hide dependency cache entries while leaving ownership of each dependency's option list with its
# loader. This keeps the shared helper generic and lets each loader decide which implementation
# details belong in the CMake GUI view.
function(griddyn_hide_cache_variables)
    foreach(_griddyn_cache_variable IN LISTS ARGN)
        if(DEFINED ${_griddyn_cache_variable})
            set("${_griddyn_cache_variable}" "${${_griddyn_cache_variable}}" CACHE INTERNAL ""
                                                                                   FORCE
            )
        endif()
    endforeach()
endfunction()

function(griddyn_hide_cache_variables_by_prefix)
    get_cmake_property(_griddyn_cache_variables CACHE_VARIABLES)
    foreach(_griddyn_cache_prefix IN LISTS ARGN)
        foreach(_griddyn_cache_variable IN LISTS _griddyn_cache_variables)
            if(_griddyn_cache_variable MATCHES "^${_griddyn_cache_prefix}")
                griddyn_hide_cache_variables("${_griddyn_cache_variable}")
            endif()
        endforeach()
    endforeach()
endfunction()

# ~~~
# the following code is derived from the cmake cmakeDependentOption macro
#
# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.
# ~~~

#[=======================================================================[.rst:
CMakeConditionalOption
--------------------

Macro to provide an option dependent on other options.

This macro presents an option to the user only if a set of other
conditions are true.  When the option is not presented a default value
is used, but any value set by the user is preserved for when the
option is presented again.  Example invocation:

::

  CMAKE_DEPENDENT_OPTION(USE_FOO "Use Foo"
                         "USE_BAR;NOT USE_ZOT")

If USE_BAR is true and USE_ZOT is false, this provides an option
called USE_FOO that defaults to ON.  Otherwise, it sets USE_FOO to
OFF.  If the status of USE_BAR or USE_ZOT ever changes, any value for
the USE_FOO option is saved so that when the option is re-enabled it
retains its old value.
#]=======================================================================]

macro(CMAKE_CONDITIONAL_OPTION option doc condition)
    set(${option}_ON 1)
    foreach(d ${depends})
        string(REGEX REPLACE " +" ";" CMAKE_DEPENDENT_OPTION_DEP "${d}")
        if(${CMAKE_DEPENDENT_OPTION_DEP})

        else()
            set(${option}_ON 0)
        endif()
    endforeach()
    if(${option}_ON)
        option(${option} "${doc}" ON)
    else()
        option(${option} "${doc}" OFF)
    endif()
endmacro()

#[=======================================================================[.rst:
CMake Dependent advacned option
--------------------

Macro to provide an advanced option dependent on other options.

This macro presents an advanced option to the user only if a set of other
conditions are true.  When the option is not presented a default value
is used, but any value set by the user is preserved for when the
option is presented again.  Example invocation:

::

  CMAKE_DEPENDENT_ADVANCED_OPTION(USE_FOO "Use Foo"
                         "USE_BAR;NOT USE_ZOT")

If USE_BAR is true and USE_ZOT is false, this provides an option
called USE_FOO that defaults to ON.  Otherwise, it sets USE_FOO to
OFF.  If the status of USE_BAR or USE_ZOT ever changes, any value for
the USE_FOO option is saved so that when the option is re-enabled it
retains its old value.
#]=======================================================================]

macro(
    CMAKE_DEPENDENT_ADVANCED_OPTION
    option
    doc
    default
    depends
    force
)
    if(${option}_ISSET MATCHES "^${option}_ISSET$")
        set(${option}_AVAILABLE 1)
        foreach(d ${depends})
            string(REGEX REPLACE " +" ";" CMAKE_DEPENDENT_OPTION_DEP "${d}")
            if(${CMAKE_DEPENDENT_OPTION_DEP})

            else()
                set(${option}_AVAILABLE 0)
            endif()
        endforeach()
        if(${option}_AVAILABLE)
            option(${option} "${doc}" "${default}")
            set(${option} "${${option}}" CACHE BOOL "${doc}" FORCE)
            mark_as_advanced(${option})
        else()
            if(${option} MATCHES "^${option}$")

            else()
                set(${option} "${${option}}" CACHE INTERNAL "${doc}")
            endif()
            set(${option} ${force})
        endif()
    else()
        set(${option} "${${option}_ISSET}")
    endif()
endmacro()

# Apply the optional launcher only to the targets created by a vendored dependency.
set(GRIDDYN_DEPENDENCY_COMPILER_LAUNCHER ""
    CACHE STRING "Compiler launcher for selected bundled dependencies"
)
mark_as_advanced(GRIDDYN_DEPENDENCY_COMPILER_LAUNCHER)

function(griddyn_apply_dependency_compiler_launcher directory)
    if(NOT GRIDDYN_DEPENDENCY_COMPILER_LAUNCHER)
        return()
    endif()

    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(target_type "${target}" TYPE)
        if(target_type MATCHES
           "^(EXECUTABLE|OBJECT_LIBRARY|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)$"
        )
            set_target_properties(
                "${target}"
                PROPERTIES C_COMPILER_LAUNCHER "${GRIDDYN_DEPENDENCY_COMPILER_LAUNCHER}"
                           CXX_COMPILER_LAUNCHER "${GRIDDYN_DEPENDENCY_COMPILER_LAUNCHER}"
            )
        endif()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        griddyn_apply_dependency_compiler_launcher("${subdirectory}")
    endforeach()
endfunction()
