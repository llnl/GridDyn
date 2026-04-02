# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Copyright (c) 2017-2019, Battelle Memorial Institute; Lawrence Livermore
# National Security, LLC; Alliance for Sustainable Energy, LLC.
# See the top-level NOTICE for additional details.
# All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(JSONCPP_WITH_TESTS OFF CACHE INTERNAL "")
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE INTERNAL "")
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE INTERNAL "")
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE INTERNAL "")
set(DEBUG_LIBNAME_SUFFIX ${CMAKE_DEBUG_POSTFIX} CACHE INTERNAL "")
# so json cpp exports to the correct target export

set(INSTALL_EXPORT "" CACHE INTERNAL "")

set(JSONCPP_DISABLE_CCACHE ON CACHE INTERNAL "")

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14) # Supported values are ``11``, ``14``, and ``17``.
endif()

if(BUILD_SHARED_LIBS)
    set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
endif()

# these are internal variables used in JSONCPP that we know to be true based on the
# requirements in HELICS for newer compilers than JSONCPP supports
set(HAVE_CLOCALE ON)
set(HAVE_LOCALECONV ON)
set(COMPILER_HAS_DEPRECATED ON)
set(HAVE_STDINT_H ON)
set(HAVE_DECIMAL_POINT ON)
add_subdirectory("${PROJECT_SOURCE_DIR}/ThirdParty/jsoncpp_new"
                 "${PROJECT_BINARY_DIR}/ThirdParty/jsoncpp_new" EXCLUDE_FROM_ALL)

# Upstream jsoncpp target names vary by build mode/version. GridDyn expects
# `jsoncpp_lib`, so create a compatibility target when only `jsoncpp_static`
# (or namespaced targets) are present.
if(NOT TARGET jsoncpp_lib)
    if(TARGET jsoncpp_static)
        add_library(jsoncpp_lib INTERFACE)
        target_link_libraries(jsoncpp_lib INTERFACE jsoncpp_static)
    elseif(TARGET JsonCpp::JsonCpp)
        add_library(jsoncpp_lib INTERFACE)
        target_link_libraries(jsoncpp_lib INTERFACE JsonCpp::JsonCpp)
    elseif(TARGET jsoncpp_object)
        add_library(jsoncpp_lib INTERFACE)
        target_link_libraries(jsoncpp_lib INTERFACE jsoncpp_object)
    endif()
endif()

set(_griddyn_jsoncpp_targets)
foreach(_json_tgt IN ITEMS jsoncpp_lib jsoncpp_static jsoncpp_object)
    if(TARGET ${_json_tgt})
        list(APPEND _griddyn_jsoncpp_targets ${_json_tgt})
    endif()
endforeach()
if(_griddyn_jsoncpp_targets)
    set_target_properties(${_griddyn_jsoncpp_targets} PROPERTIES FOLDER Extern)
endif()

if(OLD_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})
endif()

hide_variable(JSONCPP_WITH_STRICT_ISO)
hide_variable(JSONCPP_WITH_WARNING_AS_ERROR)
hide_variable(DEBUG_LIBNAME_SUFFIX)
hide_variable(JSONCPP_USE_SECURE_MEMORY)
