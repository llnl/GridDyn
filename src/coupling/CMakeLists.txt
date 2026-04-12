# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# set variable shared_sources with the sources for the coupling library
set(coupling_sources GhostSwingBusManager.cpp)

set(coupling_headers GhostSwingBusManager.h GhostSwingBusMessageTypes.h)
if(GRIDDYN_ENABLE_MPI)
    list(APPEND coupling_sources MpiService.cpp)
    list(APPEND coupling_headers MpiService.h)
endif(GRIDDYN_ENABLE_MPI)
# require MPI for this project

add_library(coupling_static_lib STATIC ${coupling_sources} ${coupling_headers})

target_link_libraries(coupling_static_lib PUBLIC griddyn_base)
set_target_properties(coupling_static_lib PROPERTIES FOLDER libraries)
