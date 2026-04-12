# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(gridDynMain_sources gridDynMain.cpp)

set(gridDynMain_headers)

add_executable(gridDynMain ${gridDynMain_sources} ${gridDynMain_headers})

target_link_libraries(gridDynMain runner griddyn_base griddyn_optional)

install(TARGETS gridDynMain RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT RUNTIME)
