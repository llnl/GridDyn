# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#
# This file is used to add suitesparse to a project. GridDyn now expects the cmake-enabled
# SuiteSparse source to be available as the tracked submodule in ThirdParty/suitesparse-cmake rather
# than downloading a second copy at configure time.
#

if(${PROJECT_NAME}_USE_SUITESPARSE_STATIC_LIBRARY)
    set(suitesparse_static_build ON)
    set(suitesparse_shared_build OFF)
else()
    set(suitesparse_static_build OFF)
    set(suitesparse_shared_build ON)
endif()

set(suitesparse_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/suitesparse-cmake")
set(suitesparse_BINARY_DIR "${PROJECT_BINARY_DIR}/ThirdParty/suitesparse-cmake")

if(NOT EXISTS "${suitesparse_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The SuiteSparse submodule was not found at ${suitesparse_SOURCE_DIR}. "
                        "Initialize/update submodules before enabling the local SuiteSparse build."
    )
endif()

set(${PROJECT_NAME}_SUITESPARSE_LOCAL_BUILD ON CACHE INTERNAL "")
set(KLU_INCLUDE_DIR
    "${suitesparse_SOURCE_DIR}/SuiteSparse/KLU/Include"
    "${suitesparse_SOURCE_DIR}/SuiteSparse/AMD/Include"
    "${suitesparse_SOURCE_DIR}/SuiteSparse/SuiteSparse_config"
    "${suitesparse_SOURCE_DIR}/SuiteSparse/COLAMD/Include"
    "${suitesparse_SOURCE_DIR}/SuiteSparse/BTF/Include"
    CACHE INTERNAL ""
)

add_subdirectory("${suitesparse_SOURCE_DIR}" "${suitesparse_BINARY_DIR}" EXCLUDE_FROM_ALL)

set(SuiteSparse_FOUND ON CACHE INTERNAL "")

set(SuiteSparse_LIBRARIES klu btf amd colamd suitesparseconfig)

set_target_properties(${SuiteSparse_LIBRARIES} PROPERTIES FOLDER "klu")

if(${PROJECT_NAME}_USE_SUITESPARSE_STATIC_LIBRARY)
    set(klu_primary_target klu)
else()
    set(klu_primary_target klu)
endif()

if(${PROJECT_NAME}_BUILD_CXX_SHARED_LIB OR NOT ${PROJECT_NAME}_DISABLE_C_SHARED_LIB)

    if(NOT ${PROJECT_NAME}_USE_SUITESPARSE_STATIC_LIBRARY)
        set_target_properties(${klu_primary_target} PROPERTIES PUBLIC_HEADER "")
        #[[ if(NOT CMAKE_VERSION VERSION_LESS "3.13")
            install(
                TARGETS ${klu_primary_target}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                FRAMEWORK DESTINATION "Library/Frameworks"
            )
        elseif(WIN32)
            install(
                FILES $<TARGET_FILE:${klu_primary_target}>
                DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT libs
            )
        else()
            message(
                WARNING
                    "Update to CMake 3.13+ or enable the ${PROJECT_NAME}_USE_SUITESPARSE_STATIC_LIBRARY CMake option to install when using suitesparse as a subproject"
            )
        endif()
        if(MSVC
           AND NOT EMBEDDED_DEBUG_INFO
           AND NOT ${PROJECT_NAME}_BINARY_ONLY_INSTALL
        )
            install(
                FILES $<TARGET_PDB_FILE:${klu_primary_target}>
                DESTINATION ${CMAKE_INSTALL_BINDIR}
                OPTIONAL
                COMPONENT libs
            )
        endif()
        if(MSVC AND NOT ${PROJECT_NAME}_BINARY_ONLY_INSTALL)
            install(
                FILES $<TARGET_LINKER_FILE:${klu_primary_target}>
                DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT libs
            )
        endif()
]]
    endif()

endif()
