# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(fmiShared_headers fmiRunner.h fmiCoordinator.h fmiCollector.h fmiEvent.h loadFMIExportObjects.h)

set(fmiShared_sources
    fmiGridDynExport.cpp
    fmiRunner.cpp
    fmiCoordinator.cpp
    fmiCollector.cpp
    fmiEvent.cpp
    loadFMIExportObjects.cpp
)

set(fmiBuilder_headers
    fmiCoordinator.h
    fmiCollector.h
    fmiEvent.h
    loadFMIExportObjects.h
    fmuBuilder.h
    fmiRunner.h
)

set(fmiBuilder_sources
    fmiCoordinator.cpp
    fmiCollector.cpp
    fmiEvent.cpp
    loadFMIExportObjects.cpp
    fmuBuilder.cpp
    fmiRunner.cpp
    fmiGridDynExport.cpp
)

add_library(fmiGridDynSharedLib SHARED ${fmiShared_sources} ${fmiShared_headers})

set_target_properties(
    fmiGridDynSharedLib PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden
)

set_target_properties(fmiGridDynSharedLib PROPERTIES FOLDER fmi)

add_library(fmiBuilder STATIC ${fmiBuilder_sources} ${fmiBuilder_headers})

target_link_libraries(fmiBuilder PUBLIC runner pugixml::pugixml)

set_target_properties(fmiBuilder PROPERTIES FOLDER fmi)

target_link_libraries(fmiGridDynSharedLib PRIVATE runner)

install(
    TARGETS fmiGridDynSharedLib ${GRIDDYN_EXPORT_COMMAND}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set_target_properties(fmiGridDynSharedLib PROPERTIES NO_SONAME ON PREFIX "")
# Todo when minimum CMake version becomes 3.6: re-add EXCLUDE_FROM_ALL to these two install FILES
# commands for fmiGridDynSharedLib
install(FILES $<TARGET_FILE:fmiGridDynSharedLib> DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT RUNTIME
)
install(FILES $<TARGET_LINKER_FILE:fmiGridDynSharedLib> DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT libraries
)
if(WIN32)
    install(TARGETS fmiGridDynSharedLib RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/win64/
                                                COMPONENT RUNTIME
    )
    set(FMILIBRARY_TYPE win64)
elseif(APPLE)
    install(TARGETS fmiGridDynSharedLib RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/darwin64/
                                                COMPONENT RUNTIME
    )
    set(FMILIBRARY_TYPE darwin64)
elseif(UNIX)
    install(TARGETS fmiGridDynSharedLib LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/linux64/
                                                COMPONENT RUNTIME
    )
    set(FMILIBRARY_TYPE linux64)
endif()

target_compile_definitions(
    fmiBuilder PRIVATE -DGRIDDYNFMILIBRARY_LOC="$<TARGET_FILE_DIR:fmiGridDynSharedLib>"
)

target_compile_definitions(
    fmiBuilder PRIVATE -DGRIDDYNFMILIBRARY_NAME="$<TARGET_FILE_NAME:fmiGridDynSharedLib>"
)

target_compile_definitions(
    fmiBuilder PRIVATE -DGRIDDYNFMILIBRARY_BASE_NAME="$<TARGET_FILE_BASE_NAME:fmiGridDynSharedLib>"
)

target_compile_definitions(fmiBuilder PRIVATE -DFMILIBRARY_TYPE="${FMILIBRARY_TYPE}")

list(APPEND optional_component_test_files ${CMAKE_CURRENT_SOURCE_DIR}/testFMIExport.cpp)
set(optional_component_test_files ${optional_component_test_files} PARENT_SCOPE)

install(FILES ${fmiBuilder_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn/fmi_export
        COMPONENT headers
)
