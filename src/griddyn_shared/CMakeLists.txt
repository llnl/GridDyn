# LLNS Copyright Start
# Copyright (c) 2014-2018, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

set(griddyn_lib_public_headers griddyn.h griddyn_export.h griddyn_export_advanced.h)

set(griddyn_lib_internal_headers internal/griddyn_export_internal.h)

set(griddyn_lib_sources
    griddyn_exportObjects.cpp
    griddyn_exportSimulation.cpp
    griddyn_exportQueries.cpp
    griddyn_exportMathFunctions.cpp
    griddyn_exportObjectMathFunctions.cpp
    griddyn_exportEvents.cpp
)

include(GenerateExportHeader)

add_library(
    griddyn_shared_lib SHARED ${griddyn_lib_sources} ${griddyn_lib_public_headers}
                              ${griddyn_lib_internal_headers}
)

set_target_properties(
    griddyn_shared_lib PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden
)

generate_export_header(
    griddyn_shared_lib BASE_NAME griddyn EXPORT_FILE_NAME griddyn_shared_export.h
)

target_include_directories(griddyn_shared_lib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

set_target_properties(griddyn_shared_lib PROPERTIES FOLDER interfaces)

target_link_libraries(griddyn_shared_lib PRIVATE runner)
if(UNIX OR MINGW)
    if(NOT APPLE)
        target_link_libraries(
            griddyn_shared_lib PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.txt
        )
    endif(NOT APPLE)
endif()

install(
    TARGETS griddyn_shared_lib
    EXPORT griddyn-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_GREATER 3.6)
    install(
        FILES $<TARGET_FILE:griddyn_shared_lib>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT RUNTIME
        EXCLUDE_FROM_ALL
    )
    install(
        FILES $<TARGET_LINKER_FILE:griddyn_shared_lib>
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT libraries
        EXCLUDE_FROM_ALL
    )
else()
    install(FILES $<TARGET_FILE:griddyn_shared_lib> DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT RUNTIME
    )
    install(FILES $<TARGET_LINKER_FILE:griddyn_shared_lib> DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT libraries
    )
endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/griddyn_shared_export.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn/shared COMPONENT headers
)

install(FILES ${griddyn_lib_public_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn/shared
        COMPONENT headers
)
