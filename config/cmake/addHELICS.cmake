# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if(MSVC)
    set(HELICS_PATH_HINTS C:/local/helics_2_5_0 C:/local/helics_2_5_1 C:/local/helics_2_5_2
                          C:/local/helics_2_6_0
    )
endif(MSVC)

show_variable(
    HELICS_INSTALL_PATH PATH "path to the helics installation" "${CMAKE_BINARY_OUTPUT_DIR}"
)

set(HELICS_CMAKE_SUFFIXES lib/cmake/HELICS/ cmake/HELICS/)

find_package(
    HELICS
    2.5
    HINTS
    ${HELICS_INSTALL_PATH}
    $ENV{HELICS_INSTALL_PATH}
    ${HELICS_PATH_HINTS}
    PATH_SUFFIXES
    ${HELICS_CMAKE_SUFFIXES}
)

if(NOT HELICS_FOUND)
    message(FATAL_ERROR "unable to locate HELICS for linking")
endif()
