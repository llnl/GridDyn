# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

include(AddGoogletest)

set(testComponent_sources
    componentTests/ConditionTests.cpp
    componentTests/testComponents.cpp
    componentTests/testLoads.cpp
    componentTests/testRecorders.cpp
    componentTests/testExciters.cpp
    componentTests/testGovernors.cpp
    componentTests/testAdjustableTX.cpp
    componentTests/testGenModels.cpp
    componentTests/testdcLinks.cpp
    componentTests/testLinks.cpp
    componentTests/testRelays.cpp
    componentTests/testBlocks.cpp
    componentTests/testGenerators.cpp
    componentTests/testArea.cpp
    componentTests/testSource.cpp
    componentTests/simulationTests.cpp
    componentTests/testEvents.cpp
    componentTests/testExtraModels.cpp
    testHelperFunctions.cpp
    exeTestHelper.cpp
)

if(NOT GRIDDYN_ENABLE_MPI)
    list(APPEND testComponent_sources componentTests/testGridLab.cpp)
endif()

list(APPEND testComponent_sources ${optional_component_test_files})

set(testLibrary_sources
    testHelperFunctions.cpp
    exeTestHelper.cpp
    libraryTests/libraryTests.cpp
)

list(APPEND testLibrary_sources ${optional_library_test_files})

set(testSystem_sources
    systemTests/testSystem.cpp
    systemTests/testpFlow.cpp
    systemTests/testDyn1.cpp
    testHelperFunctions.cpp
    exeTestHelper.cpp
    systemTests/testDyn2.cpp
    systemTests/testInputs.cpp
    systemTests/testRoots.cpp
    systemTests/testConstraints.cpp
    systemTests/validationTests.cpp
    systemTests/testSolverModes.cpp
    systemTests/testMainExe.cpp
    systemTests/testOutputs.cpp
    systemTests/testCloning.cpp
    systemTests/faultTests.cpp
    systemTests/testContingency.cpp
    systemTests/testGridDynRunner.cpp
)

list(APPEND testSystem_sources ${optional_system_test_files})

set(testExtra_sources
    extraTests/testExtra.cpp
    extraTests/performanceTests.cpp
    extraTests/largeContingencyTests.cpp
    extraTests/largeInputTests.cpp
    extraTests/objectSizeReport.cpp
    testHelperFunctions.cpp
    exeTestHelper.cpp
)

if(GRIDDYN_ENABLE_FMI)
    list(APPEND testExtra_sources extraTests/extraFMUtests.cpp)
endif()

set(testMain_headers testHelper.h exeTestHelper.h)
set(
    library_gtest_sources
    gtestHelperFunctions.cpp
    libraryTests/gtestLibrarySmokeTests.cpp
    libraryTests/testCore.cpp
    libraryTests/testGridParameter.cpp
    libraryTests/testMatrixData.cpp
    libraryTests/testZipUtilities.cpp
    libraryTests/testOperatingBounds.cpp
    libraryTests/testValuePredictors.cpp
)

set(
    file_reader_gtest_sources
    gtestHelperFunctions.cpp
    libraryTests/testReaderInfo.cpp
    libraryTests/testXML.cpp
    libraryTests/testJsonReader.cpp
    libraryTests/testElementReaders.cpp
    libraryTests/testGridDynRunner.cpp
)

# add a baseline library for underlying dependencies and flags for test executables
add_library(griddyn_test_base INTERFACE)
target_link_libraries(griddyn_test_base INTERFACE Boostlibs::core)
target_compile_definitions(griddyn_test_base INTERFACE BOOST_TEST_NO_LIB)

target_link_libraries(griddyn_test_base INTERFACE griddyn_base)

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

target_compile_definitions(griddyn_test_base INTERFACE -DGRIDDYN_TEST_DIRECTORY="${test_dir}")
target_compile_definitions(griddyn_gtest_base INTERFACE -DGRIDDYN_TEST_DIRECTORY="${test_dir}")

target_compile_definitions(
    griddyn_test_base INTERFACE -DGRIDDYNMAIN_LOCATION="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
target_compile_definitions(
    griddyn_gtest_base INTERFACE -DGRIDDYNMAIN_LOCATION="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
target_compile_definitions(
    griddyn_test_base INTERFACE -DGRIDDYNSERVER_LOCATION="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
target_compile_definitions(
    griddyn_gtest_base INTERFACE -DGRIDDYNSERVER_LOCATION="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
target_compile_definitions(
    griddyn_test_base INTERFACE -DGRIDDYNINSTALL_LOCATION="${CMAKE_INSTALL_PREFIX}"
)
target_compile_definitions(
    griddyn_gtest_base INTERFACE -DGRIDDYNINSTALL_LOCATION="${CMAKE_INSTALL_PREFIX}"
)

add_executable(testLibrary ${testLibrary_sources} ${testMain_headers})
add_executable(testComponents ${testComponent_sources} ${testMain_headers})
add_executable(testSystem ${testSystem_sources} ${testMain_headers})
add_executable(testExtra ${testExtra_sources} ${testMain_headers})
add_executable(LibraryTests ${library_gtest_sources})
add_executable(FileReaderTests ${file_reader_gtest_sources})

set_target_properties(testLibrary testComponents testSystem testExtra PROPERTIES FOLDER
                                                                                 tests_boost
)

target_link_libraries(testLibrary fileInput griddyn_test_base)
target_link_libraries(
    testComponents
    griddyn_optional
    fileInput
    griddyn_test_base
    ${optional_component_test_linkages}
)
target_link_libraries(testSystem fileInput griddyn_test_base)
target_link_libraries(testExtra fileInput griddyn_test_base)
target_link_libraries(LibraryTests PRIVATE fileInput griddyn_gtest_base)
target_link_libraries(FileReaderTests PRIVATE fileInput griddyn_gtest_base)
add_gtest(LibraryTests)
add_gtest(FileReaderTests)
set_target_properties(LibraryTests FileReaderTests PROPERTIES FOLDER tests)
set_tests_properties(LibraryTests PROPERTIES LABELS "Quick;Continuous")
set_tests_properties(FileReaderTests PROPERTIES LABELS "Quick;Continuous")

# message(${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS testLibrary RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS testComponents RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS testSystem RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS testExtra RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS LibraryTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(TARGETS FileReaderTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
install(FILES ${KEY_LIBRARY_FILES} DESTINATION ${CMAKE_INSTALL_BINDIR}/test/)
