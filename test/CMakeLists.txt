# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

include(AddGoogletest)

set(gtestMain_headers gtestHelper.h)
set(extra_gtest_sources
    gtestHelperFunctions.cpp
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
    libraryTests/testCore.cpp
    libraryTests/testModelParameters.cpp
    libraryTests/testGridParameter.cpp
    libraryTests/testMatrixData.cpp
    libraryTests/testZipUtilities.cpp
    libraryTests/testOperatingBounds.cpp
    libraryTests/testSaturation.cpp
    libraryTests/testValuePredictors.cpp
)

set(file_reader_gtest_sources
    gtestHelperFunctions.cpp
    readerTests/testReaderInfo.cpp
    readerTests/testExampleReaders.cpp
    readerTests/testSavReader.cpp
    readerTests/testSolverReader.cpp
    readerTests/testXML.cpp
    readerTests/testJsonReader.cpp
    readerTests/testXmlElementReader.cpp
)

set(model_comparison_gtest_sources
    gtestHelperFunctions.cpp
    comparisonTests/testModelComparison.cpp
    comparisonTests/testPowerFlowComparison.cpp
)

set(network_component_gtest_sources
    gtestHelperFunctions.cpp
    componentTests/testLoads.cpp
    componentTests/testdcLinks.cpp
    componentTests/testLinks.cpp
    componentTests/testAdjustableTX.cpp
    componentTests/testExponentialLoadParameters.cpp
    componentTests/testComponentDescriptions.cpp
)

set(generator_component_gtest_sources
    gtestHelperFunctions.cpp
    componentTests/testBlocks.cpp
    componentTests/testGovernors.cpp
    componentTests/testStabilizers.cpp
    componentTests/testPSS2BESAC2A.cpp
    componentTests/testGenerators.cpp
    componentTests/testGenModels.cpp
    componentTests/testMachineDescription.cpp
    componentTests/testEPCGEN.cpp
    componentTests/testRenewableModels.cpp
    componentTests/testDistributedModels.cpp
    componentTests/testExciters.cpp
    componentTests/testSolverStateDescriptions.cpp
    componentTests/testExciterDescription.cpp
    componentTests/testVoltageCompensators.cpp
)

set(event_component_gtest_sources
    gtestHelperFunctions.cpp
    componentTests/testSource.cpp
    componentTests/testRelays.cpp
    componentTests/ConditionTests.cpp
    componentTests/testEvents.cpp
    componentTests/testRecorders.cpp
    componentTests/testMeasurements.cpp
    componentTests/testGrabberFunctions.cpp
)

set(system_component_gtest_sources gtestHelperFunctions.cpp componentTests/testArea.cpp
                                   componentTests/testBus.cpp componentTests/simulationTests.cpp
)

set(optional_component_gtest_sources gtestHelperFunctions.cpp componentTests/testExtraModels.cpp)
if(NOT GRIDDYN_ENABLE_MPI)
    list(APPEND optional_component_gtest_sources componentTests/testGridLab.cpp)
endif()
if(GRIDDYN_ENABLE_FMI)
    list(APPEND optional_component_gtest_sources componentTests/testFMI.cpp)
endif()
if(GRIDDYN_ENABLE_NETWORKING_LIBRARY AND GRIDDYN_ENABLE_ZMQ)
    list(APPEND optional_component_gtest_sources ../src/networking/test/zmqTest.cpp)
endif()
if(GRIDDYN_ENABLE_HELICS_EXECUTABLE)
    list(APPEND optional_component_gtest_sources ../src/helics/test/helicsTest.cpp)
    list(APPEND optional_component_gtest_sources exeTestHelper.cpp)
endif()
# The export test loads the generated FMU through the FMI import wrapper, so it requires both the
# exporter and the optional FMI import library.
if(GRIDDYN_ENABLE_FMI_EXPORT AND GRIDDYN_ENABLE_FMI)
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

set(executable_system_gtest_sources
    systemTests/testMainExe.cpp
    systemTests/testGridDynRunner.cpp
    systemTests/testRunnerPlayback.cpp
    exeTestHelper.cpp
)

set(dynamic_system_gtest_sources
    gtestHelperFunctions.cpp
    systemTests/testActivsG500.cpp
    systemTests/testSolverModes.cpp
    systemTests/testDyn1.cpp
    systemTests/testDyn2.cpp
    systemTests/testRoots.cpp
    systemTests/faultTests.cpp
)

if(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY)
    set(optimization_gtest_sources gtestHelperFunctions.cpp optimizationTests/testDcFormulation.cpp
                                   optimizationTests/testOptimizationDescription.cpp
    )
endif()

add_library(griddyn_gtest_base INTERFACE)
target_link_libraries(griddyn_gtest_base INTERFACE griddyn_base)
target_include_directories(
    griddyn_gtest_base SYSTEM
    INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
              $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty/utilities>
)
target_compile_options(
    griddyn_gtest_base
    INTERFACE $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-c++17-attribute-extensions>
)

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
add_executable(ModelComparisonTests ${model_comparison_gtest_sources} ${gtestMain_headers})
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
if(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY)
    add_executable(OptimizationTests ${optimization_gtest_sources} ${gtestMain_headers})
endif()

target_link_libraries(ExtraTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(LibraryTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(FileReaderTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(ModelComparisonTests PRIVATE fileInput griddyn_gtest_base)
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
target_link_libraries(ExecutableSystemTests PRIVATE runner griddyn_gtest_base)
add_dependencies(ExecutableSystemTests gridDynMain)
target_compile_definitions(
    ExecutableSystemTests PRIVATE GRIDDYN_TEST_MAIN_DIRECTORY="$<TARGET_FILE_DIR:gridDynMain>"
)
target_link_libraries(
    OptionalComponentTests PRIVATE griddyn_optional fileInput griddyn_gtest_base
                                   ${optional_component_test_linkages}
)
if(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY)
    target_link_libraries(OptimizationTests PRIVATE optimization fileInput griddyn_gtest_base)
    if(GRIDDYN_ENABLE_HIGHS)
        if(NOT TARGET GridDyn::highs)
            message(
                FATAL_ERROR
                    "GRIDDYN_ENABLE_HIGHS is ON, but the GridDyn HiGHS solver target was not created."
            )
        endif()
    endif()
endif()
set(continuous_test_targets
    LibraryTests
    FileReaderTests
    ModelComparisonTests
    NetworkComponentTests
    GeneratorComponentTests
    EventComponentTests
    SystemComponentTests
    PowerflowSystemTests
    WorkflowSystemTests
    DynamicSystemTests
    ExecutableSystemTests
    OptionalComponentTests
)
if(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY)
    list(APPEND continuous_test_targets OptimizationTests)
endif()
foreach(test_target IN LISTS continuous_test_targets)
    add_gtest(${test_target} "Quick;Continuous")
endforeach()
add_gtest(ExtraTests "Nightly")

install(DIRECTORY DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS ${continuous_test_targets} ExtraTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
