# LLNS Copyright Start
# Copyright (c) 2014, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

# -----------------------------------------------------------------------------
# * Find FSKIT library.
#
# This module finds if Fskit is installed and determines where the include files and libraries are.
# This code sets the following variables: FSKIT_FOUND         = Fskit was found FSKIT_LIBRARY_DIR =
# path to where libraries can be found FSKIT_INCLUDE_DIR   = path to where header files can be found
# FSKIT_LIBRARIES     = link libraries for Fskit
# -----------------------------------------------------------------------------

include(FindPackageHandleStandardArgs)

find_path(FSKIT_DIR include/fskit/logical-process.h HINTS ${FSKIT_INSTALL_DIR}
          DOC "FSKIT Directory"
)

if(FSKIT_DIR)
    set(FSKIT_FOUND YES)

    set(FSKIT_INCLUDE_DIR ${FSKIT_DIR}/include)
    set(FSKIT_LIBRARY_DIR ${FSKIT_DIR}/lib)

    set(FSKIT_LIBRARY_NAME fskit)

    find_library(
        FSKIT_LIBRARY
        NAMES fskit
        HINTS ${FSKIT_LIBRARY_DIR}
        NO_DEFAULT_PATH
    )

    set(FSKIT_LIBRARIES ${FSKIT_LIBRARY})

else(FSKIT_DIR)
    set(FSKIT_FOUND NO)
endif(FSKIT_DIR)

find_package_handle_standard_args(FSKIT DEFAULT_MSG FSKIT_LIBRARIES FSKIT_INCLUDE_DIR)
