# LLNS Copyright Start
# Copyright (c) 2014-2018, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

set(griddynHELICS_sources
    helicsSource.cpp
    helicsCollector.cpp
    helicsGhostBus.cpp
    helicsLoad.cpp
    helicsLibrary.cpp
    helicsSupport.cpp
    helicsRunner.cpp
    helicsCoordinator.cpp
    helicsCommunicator.cpp
    helicsEvent.cpp
)

set(griddynHELICS_headers
    helicsLibrary.h
    helicsSource.h
    helicsCollector.h
    helicsGhostBus.h
    helicsLoad.h
    helicsRunner.h
    helicsSupport.h
    helicsCoordinator.h
    helicsCommunicator.h
    helicsEvent.h
)

add_library(helicsLib STATIC ${griddynHELICS_sources} ${griddynHELICS_headers})

set_target_properties(helicsLib PROPERTIES FOLDER libraries)

message(STATUS "helics include ${HELICS_INCLUDE_DIR}")

file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/test test_dir)

if(WIN32)
    # there are 4 slashes because cmake escapes "\" and regex escapes it so I need 4 to get the
    # regex to work and I need two for visual studio to actually print it. thus to it looks like
    # this to replace '\' with '\\' this will probably fail on macs yet

    string(REGEX REPLACE "\\\\" "\\\\\\\\" test_dir ${test_dir})
endif(WIN32)
# message(${test_dir})

target_compile_definitions(helicsLib PUBLIC -DHELICS_TEST_DIRECTORY="${test_dir}")

target_compile_definitions(helicsLib PUBLIC -DHELICS_BROKER_EXECUTABLE="${HELICS_BROKER}")

target_compile_definitions(helicsLib PUBLIC -DHELICS_PLAYER_EXECUTABLE="${HELICS_PLAYER}")
target_compile_definitions(helicsLib PUBLIC -DHELICS_RECORDER_EXECUTABLE="${HELICS_RECORDER}")

target_link_libraries(helicsLib PUBLIC HELICS::helics-apps-shared runner)

list(APPEND optional_library_key_headers helics/helicsLibrary.h)
list(APPEND optional_library_functions loadHELICSLibrary)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)

list(APPEND optional_component_test_files ${CMAKE_CURRENT_SOURCE_DIR}/test/helicsTest.cpp)
set(optional_component_test_files ${optional_component_test_files} PARENT_SCOPE)

if(GRIDDYN_ENABLE_HELICS_EXECUTABLE AND WIN32)

    add_custom_command(
        TARGET helicsLib
        POST_BUILD # Adds a post-build event to MyTest
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different # which executes "cmake - E copy_if_different..."
            $<TARGET_FILE:HELICS::helics-apps-shared> # <--this is in-file
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/"
    ) # <--this is out-file path
    add_custom_command(
        TARGET helicsLib
        POST_BUILD # Adds a post-build event to MyTest
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different # which executes "cmake - E copy_if_different..."
            $<TARGET_FILE:HELICS::helics-shared> # <--this is in-file
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/"
    ) # <--this is out-file path

endif()
