# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(gridDynMain_sources gridDynMain.cpp)

set(gridDynMain_headers)

add_executable(gridDynMain ${gridDynMain_sources} ${gridDynMain_headers})

target_link_libraries(gridDynMain runner griddyn_base griddyn_optional)

if(APPLE)
    set_target_properties(gridDynMain PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(gridDynMain PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

set(_griddyn_runtime_dependency_options)
if(WIN32)
    # Bundle project DLLs such as SuiteSparse KLU, but leave Windows and the Visual C++ runtime to
    # the host system or its redistributable package.
    set(_griddyn_runtime_dependency_options
        RUNTIME_DEPENDENCIES
        DIRECTORIES
        $<TARGET_FILE_DIR:gridDynMain>
        PRE_EXCLUDE_REGEXES
        "[Aa][Pp][Ii]-[Mm][Ss]-[Ww][Ii][Nn]-.*"
        "[Ee][Xx][Tt]-[Mm][Ss]-[Ww][Ii][Nn]-.*"
        "[Kk][Ee][Rr][Nn][Ee][Ll]32[.]dll"
        "[Mm][Ss][Vv][Cc][Pp].*[.]dll"
        "[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee].*[.]dll"
        "[Uu][Cc][Rr][Tt][Bb][Aa][Ss][Ee].*[.]dll"
        POST_EXCLUDE_REGEXES
        "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]"
    )
endif()

install(TARGETS gridDynMain ${GRIDDYN_EXPORT_COMMAND} ${_griddyn_runtime_dependency_options}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications
)
