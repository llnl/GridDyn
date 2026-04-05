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
# * Find KLU library.
#
# This module finds if KLU and other suitesparse libraries are installed and determines where the
# include files and libraries are.  This code sets the following variables: KLU_FOUND         = KLU
# was found KLU_LIBRARY_DIR   = path to where libraries can be found KLU_INCLUDE_DIR   = path to
# where header files can be found KLU_LIBRARIES     = link libraries for KLU
# -----------------------------------------------------------------------------

include(FindPackageHandleStandardArgs)

if(KLU_INCLUDE_DIR)

else(KLU_INCLUDE_DIR)
    find_path(KLU_DIR include/klu.h HINTS ${KLU_INSTALL_DIR} DOC "KLU Directory")

    message(STATUS ${KLU_DIR})
    if(KLU_DIR)
        set(KLU_FOUND YES)
        set(KLU_INCLUDE_DIR ${KLU_DIR}/include)
    else(KLU_DIR)
        find_path(KLU_DIR include/suitesparse/klu.h HINTS ${KLU_INSTALL_DIR} DOC "KLU Directory")
        message(STATUS ${KLU_DIR})
        if(KLU_DIR)
            set(KLU_FOUND YES)
            set(KLU_INCLUDE_DIR ${KLU_DIR}/include/suitesparse)
        endif(KLU_DIR)
    endif(KLU_DIR)
endif(KLU_INCLUDE_DIR)

message(STATUS ${KLU_DIR})
if(KLU_DIR)
    if(WIN32)
        if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
            message(STATUS "Target is 64 bits")
            set(KLU_LIBRARY_DIR ${KLU_DIR}/lib64)
        else("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
            message(STATUS "Target is 32 bits")
            set(KLU_LIBRARY_DIR ${KLU_DIR}/lib)
        endif("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    else(WIN32)
        set(KLU_LIBRARY_DIR ${KLU_DIR}/lib)
    endif(WIN32)

    # The set of required KLU libraries
    set(KLU_REQUIRED_LIBS klu amd colamd btf umfpack)
    set(KLU_REQUIRED_IFFOUND_LIBS cxsparse suitesparseconfig)

    foreach(TEST_LIB IN LISTS KLU_REQUIRED_LIBS)
        message(STATUS "Checking for TEST_LIB=${TEST_LIB}")
        # Need to make sure variable to search for isn't set
        unset(KLU_LIB CACHE)
        find_library(
            KLU_LIB
            NAMES ${TEST_LIB}
            HINTS ${KLU_LIBRARY_DIR}
            NO_DEFAULT_PATH
        )
        if(KLU_LIB)
            list(APPEND KLU_LIBRARIES ${KLU_LIB})
        else(KLU_LIB)
            find_library(
                KLU_LIB
                NAMES ${TEST_LIB}d lib${TEST_LIB} lib${TEST_LIB}d
                HINTS ${KLU_LIBRARY_DIR}
                NO_DEFAULT_PATH
            )
            if(KLU_LIB)
                list(APPEND KLU_LIBRARIES ${KLU_LIB})
            else(KLU_LIB)
                find_library(
                    KLU_LIB
                    NAMES ${TEST_LIB} ${TEST_LIB}d lib${TEST_LIB} lib${TEST_LIB}d
                    HINTS ${KLU_DIR}/lib
                    NO_DEFAULT_PATH
                )
                if(KLU_LIB)
                    list(APPEND KLU_LIBRARIES ${KLU_LIB})
                else(KLU_LIB)
                    find_library(KLU_LIB NAMES ${TEST_LIB} lib${TEST_LIB})
                    if(KLU_LIB)
                        list(APPEND KLU_LIBRARIES ${KLU_LIB})
                    else(KLU_LIB)
                        message(FATAL_ERROR "Could not find required KLU library : ${TEST_LIB}")
                    endif(KLU_LIB)
                endif(KLU_LIB)

            endif(KLU_LIB)
        endif(KLU_LIB)
        message(STATUS "KLU_LIB=${KLU_LIB}")
    endforeach(TEST_LIB)

    foreach(TEST_LIB IN LISTS KLU_REQUIRED_IFFOUND_LIBS)
        message(STATUS "Checking for TEST_LIB=${TEST_LIB}")

        # Need to make sure variable to search for isn't set
        unset(KLU_LIB CACHE)
        find_library(
            KLU_LIB
            NAMES ${TEST_LIB}
            HINTS ${KLU_LIBRARY_DIR}
            NO_DEFAULT_PATH
        )

        if(KLU_LIB)
            list(APPEND KLU_LIBRARIES ${KLU_LIB})
        else(KLU_LIB)
            find_library(
                KLU_LIB
                NAMES ${TEST_LIB}d lib${TEST_LIB} lib${TEST_LIB}d
                HINTS ${KLU_LIBRARY_DIR}
                NO_DEFAULT_PATH
            )
            if(KLU_LIB)
                list(APPEND KLU_LIBRARIES ${KLU_LIB})
            else(KLU_LIB)
                find_library(
                    KLU_LIB
                    NAMES ${TEST_LIB} ${TEST_LIB}d lib${TEST_LIB} lib${TEST_LIB}d
                    HINTS ${KLU_DIR}/lib
                    NO_DEFAULT_PATH
                )
                if(KLU_LIB)
                    list(APPEND KLU_LIBRARIES ${KLU_LIB})
                endif(KLU_LIB)
            endif(KLU_LIB)
        endif(KLU_LIB)
        message(STATUS "KLU_LIB=${KLU_LIB}")
    endforeach(TEST_LIB)

    message(STATUS "CMAKE_FIND_LIBRARY_SUFFIXES=${CMAKE_FIND_LIBRARY_SUFFIXES}")
    message(STATUS "KLU_FOUND=${KLU_FOUND}")
    message(STATUS "KLU_LIBRARY_DIR=${KLU_LIBRARY_DIR}")
    message(STATUS "KLU_INCLUDE_DIR=${KLU_INCLUDE_DIR}")
    message(STATUS "KLU_LIBRARIES=${KLU_LIBRARIES}")

else(KLU_DIR)
    set(KLU_FOUND NO)
endif(KLU_DIR)

find_package_handle_standard_args(KLU DEFAULT_MSG KLU_LIBRARIES KLU_INCLUDE_DIR)
