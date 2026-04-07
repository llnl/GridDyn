# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

include(AddGoogletest)

set(gtestMain_headers gtestHelper.h)
set(extra_gtest_sources
    gtestHelperFunctions.cpp
    extraTests/testExtra.cpp
    extraTests/performanceTests.cpp
    extraTests/largeContingencyTests.cpp
    extraTests/largeInputTests.cpp
    extraTests/objectSizeReport.cpp
)

if(GRIDDYN_ENABLE_FMI)
    list(APPEND extra_gtest_sources extraTests/extraFMUtests.cpp)
endif()

set(library_gtest_sources
    gtestHelperFunctions.cpp
    libraryTests/gtestLibrarySmokeTests.cpp
    libraryTests/testCore.cpp
    libraryTests/testGridParameter.cpp
    libraryTests/testMatrixData.cpp
    libraryTests/testZipUtilities.cpp
    libraryTests/testOperatingBounds.cpp
    libraryTests/testValuePredictors.cpp
)

set(file_reader_gtest_sources
    gtestHelperFunctions.cpp
    libraryTests/testReaderInfo.cpp
    libraryTests/testXML.cpp
    libraryTests/testJsonReader.cpp
    libraryTests/testElementReaders.cpp
    libraryTests/testGridDynRunner.cpp
)

set(network_component_gtest_sources
    gtestHelperFunctions.cpp componentTests/testLoads.cpp componentTests/testdcLinks.cpp
    componentTests/testLinks.cpp componentTests/testAdjustableTX.cpp
)

set(generator_component_gtest_sources
    gtestHelperFunctions.cpp
    componentTests/testBlocks.cpp
    componentTests/testGovernors.cpp
    componentTests/testGenerators.cpp
    componentTests/testGenModels.cpp
    componentTests/testExciters.cpp
)

set(event_component_gtest_sources
    gtestHelperFunctions.cpp
    componentTests/testSource.cpp
    componentTests/testRelays.cpp
    componentTests/ConditionTests.cpp
    componentTests/testEvents.cpp
    componentTests/testRecorders.cpp
)

set(system_component_gtest_sources gtestHelperFunctions.cpp componentTests/testArea.cpp
                                   componentTests/simulationTests.cpp
)

set(optional_component_gtest_sources gtestHelperFunctions.cpp componentTests/testExtraModels.cpp)
if(NOT GRIDDYN_ENABLE_MPI)
    list(APPEND optional_component_gtest_sources componentTests/testGridLab.cpp)
endif()
if(GRIDDYN_ENABLE_FMI)
    list(APPEND optional_component_gtest_sources componentTests/testFMI.cpp)
endif()
if(GRIDDYN_ENABLE_ZMQ OR ZMQ_NEEDED)
    list(APPEND optional_component_gtest_sources ../src/networking/test/zmqTest.cpp)
endif()
if(GRIDDYN_ENABLE_HELICS_EXECUTABLE)
    list(APPEND optional_component_gtest_sources ../src/helics/test/helicsTest.cpp)
    list(APPEND optional_component_gtest_sources exeTestHelper.cpp)
endif()
if(GRIDDYN_ENABLE_FMI_EXPORT)
    list(APPEND optional_component_gtest_sources ../src/fmi_export/testFMIExport.cpp)
endif()

set(powerflow_system_gtest_sources gtestHelperFunctions.cpp systemTests/testpFlow.cpp
                                   systemTests/validationTests.cpp
)

set(workflow_system_gtest_sources
    gtestHelperFunctions.cpp
    systemTests/testConstraints.cpp
    systemTests/testOutputs.cpp
    systemTests/testContingency.cpp
    systemTests/testCloning.cpp
    systemTests/testInputs.cpp
)

set(executable_system_gtest_sources systemTests/testMainExe.cpp systemTests/testGridDynRunner.cpp
                                    exeTestHelper.cpp
)

set(dynamic_system_gtest_sources
    gtestHelperFunctions.cpp
    systemTests/testSolverModes.cpp
    systemTests/testDyn1.cpp
    systemTests/testDyn2.cpp
    systemTests/testRoots.cpp
    systemTests/faultTests.cpp
)

add_library(griddyn_gtest_base INTERFACE)
target_link_libraries(griddyn_gtest_base INTERFACE gtest gtest_main gmock)
target_link_libraries(griddyn_gtest_base INTERFACE griddyn_base)

file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/test_files test_dir)

if(WIN32)
    # there are 4 slashes because cmake escapes "\" and regex escapes it so I need 4 this will
    # probably fail on macs yet

    string(REGEX REPLACE "\\\\" "\\\\\\\\" test_dir ${test_dir})
endif(WIN32)
# message(${test_dir})

target_compile_definitions(griddyn_gtest_base INTERFACE -DGRIDDYN_TEST_DIRECTORY="${test_dir}")

target_compile_definitions(
    griddyn_gtest_base INTERFACE -DGRIDDYNMAIN_LOCATION="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
target_compile_definitions(
    griddyn_gtest_base INTERFACE -DGRIDDYNSERVER_LOCATION="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
target_compile_definitions(
    griddyn_gtest_base INTERFACE -DGRIDDYNINSTALL_LOCATION="${CMAKE_INSTALL_PREFIX}"
)

add_executable(ExtraTests ${extra_gtest_sources} ${gtestMain_headers})
add_executable(LibraryTests ${library_gtest_sources} ${gtestMain_headers})
add_executable(FileReaderTests ${file_reader_gtest_sources} ${gtestMain_headers})
add_executable(NetworkComponentTests ${network_component_gtest_sources} ${gtestMain_headers})
add_executable(GeneratorComponentTests ${generator_component_gtest_sources} ${gtestMain_headers})
add_executable(EventComponentTests ${event_component_gtest_sources} ${gtestMain_headers})
add_executable(SystemComponentTests ${system_component_gtest_sources} ${gtestMain_headers})
add_executable(PowerflowSystemTests ${powerflow_system_gtest_sources} ${gtestMain_headers})
add_executable(WorkflowSystemTests ${workflow_system_gtest_sources} ${gtestMain_headers})
add_executable(DynamicSystemTests ${dynamic_system_gtest_sources} ${gtestMain_headers})
add_executable(
    ExecutableSystemTests ${executable_system_gtest_sources} gtestHelper.h exeTestHelper.h
)
add_executable(OptionalComponentTests ${optional_component_gtest_sources} ${gtestMain_headers})

target_link_libraries(ExtraTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(LibraryTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(FileReaderTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(
    NetworkComponentTests PRIVATE griddyn_optional fileInput griddyn_gtest_base
                                  ${optional_component_test_linkages}
)
target_link_libraries(
    GeneratorComponentTests PRIVATE griddyn_optional fileInput griddyn_gtest_base
                                    ${optional_component_test_linkages}
)
target_link_libraries(
    EventComponentTests PRIVATE griddyn_optional fileInput griddyn_gtest_base
                                ${optional_component_test_linkages}
)
target_link_libraries(
    SystemComponentTests PRIVATE griddyn_optional fileInput griddyn_gtest_base
                                 ${optional_component_test_linkages}
)
target_link_libraries(PowerflowSystemTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(WorkflowSystemTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(DynamicSystemTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(ExecutableSystemTests PRIVATE griddyn_gtest_base)
target_link_libraries(
    OptionalComponentTests PRIVATE griddyn_optional fileInput griddyn_gtest_base
                                   ${optional_component_test_linkages}
)
add_gtest(LibraryTests)
add_gtest(ExtraTests)
add_gtest(FileReaderTests)
add_gtest(NetworkComponentTests)
add_gtest(GeneratorComponentTests)
add_gtest(EventComponentTests)
add_gtest(SystemComponentTests)
add_gtest(PowerflowSystemTests)
add_gtest(WorkflowSystemTests)
add_gtest(DynamicSystemTests)
add_gtest(ExecutableSystemTests)
add_gtest(OptionalComponentTests)
set_target_properties(
    LibraryTests
    ExtraTests
    FileReaderTests
    NetworkComponentTests
    GeneratorComponentTests
    EventComponentTests
    SystemComponentTests
    PowerflowSystemTests
    WorkflowSystemTests
    DynamicSystemTests
    ExecutableSystemTests
    OptionalComponentTests
    PROPERTIES FOLDER tests
)
set_tests_properties(LibraryTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(ExtraTests PROPERTIES LABELS "Nightly")
set_tests_properties(FileReaderTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(NetworkComponentTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(GeneratorComponentTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(EventComponentTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(SystemComponentTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(PowerflowSystemTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(WorkflowSystemTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(DynamicSystemTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(ExecutableSystemTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(OptionalComponentTests PROPERTIES LABELS "Quick;Continuous")

# message(${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS ExtraTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS LibraryTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS FileReaderTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS NetworkComponentTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS GeneratorComponentTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS EventComponentTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS SystemComponentTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS PowerflowSystemTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS WorkflowSystemTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS DynamicSystemTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS ExecutableSystemTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS OptionalComponentTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(FILES ${KEY_LIBRARY_FILES} DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
