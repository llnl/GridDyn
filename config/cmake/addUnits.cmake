# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Copyright (c) 2017-2019, Battelle Memorial Institute; Lawrence Livermore
# National Security, LLC; Alliance for Sustainable Energy, LLC.
# See the top-level NOTICE for additional details.
# All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# so units cpp exports to the correct target export
set(UNITS_INSTALL OFF CACHE INTERNAL "")

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()

set(UNITS_BUILD_OBJECT_LIBRARY OFF CACHE INTERNAL "")
set(UNITS_BUILD_STATIC_LIBRARY ON CACHE INTERNAL "")
set(UNITS_BUILD_SHARED_LIBRARY OFF CACHE INTERNAL "")

add_subdirectory("${PROJECT_SOURCE_DIR}/ThirdParty/units"
                 "${PROJECT_BINARY_DIR}/ThirdParty/units")

# Upstream units target naming changed (`units`/`units::units`), while GridDyn
# still expects `units-static`. Create a compatibility target as needed.
if(NOT TARGET units-static)
    if(TARGET units)
        add_library(units-static INTERFACE)
        target_link_libraries(units-static INTERFACE units)
    elseif(TARGET units::units)
        add_library(units-static INTERFACE)
        target_link_libraries(units-static INTERFACE units::units)
    endif()
endif()

set(_griddyn_units_targets)
foreach(_units_tgt IN ITEMS units-static units)
    if(TARGET ${_units_tgt})
        list(APPEND _griddyn_units_targets ${_units_tgt})
    endif()
endforeach()
if(_griddyn_units_targets)
    set_target_properties(${_griddyn_units_targets} PROPERTIES FOLDER Extern)
endif()

hide_variable(UNITS_HEADER_ONLY)
hide_variable(UNITS_BUILD_OBJECT_LIBRARY)
