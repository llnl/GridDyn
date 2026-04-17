# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# -----------------------------------------------------------------------------
# * Find Braid library.
#
# This module finds if Braid is installed and determines where the include files and libraries are.
# This code sets the following variables: BRAID_FOUND         = Braid was found BRAID_INCLUDE_DIR =
# path to where header files can be found BRAID_LIBRARY     = link library for Braid
# -----------------------------------------------------------------------------

find_path(BRAID_DIR braid.h HINTS ${BRAID_INSTALL_DIR} DOC "BRAID Directory")

if(BRAID_DIR)
    set(BRAID_INCLUDE_DIR ${BRAID_DIR})

    find_library(
        BRAID_LIBRARY
        NAMES braid
        HINTS ${BRAID_DIR}
        NO_DEFAULT_PATH
    )

    if(BRAID_LIBRARY AND NOT TARGET braid::braid)
        add_library(braid::braid STATIC IMPORTED)
        set_property(TARGET braid::braid PROPERTY IMPORTED_LOCATION ${BRAID_LIBRARY})
        set_property(
            TARGET braid::braid PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${BRAID_INCLUDE_DIR}
        )
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Braid FOUND_VAR BRAID_FOUND REQUIRED_VARS BRAID_LIBRARY BRAID_INCLUDE_DIR
)
