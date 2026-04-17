# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(shared_library_test_headers)

set(shared_library_test_sources shared_library_tests.cpp basic_tests.cpp)

add_executable(shared_library_tests ${shared_library_test_sources} ${shared_library_test_headers})

target_include_directories(shared_library_tests SYSTEM PUBLIC ${Boost_INCLUDE_DIR})
target_include_directories(shared_library_tests PUBLIC ${PROJECT_SOURCE_DIR}/src/griddyn_shared)

set_target_properties(shared_library_tests PROPERTIES FOLDER tests)

file(TO_NATIVE_PATH ${CMAKE_SOURCE_DIR}/test/test_files test_dir)

if(WIN32)
    # there are 4 slashes because cmake escapes "\" and regex escapes it so I need 4 this will
    # probably fail on macs yet

    string(REGEX REPLACE "\\\\" "\\\\\\\\" test_dir ${test_dir})
endif(WIN32)
# message(${test_dir})

target_compile_definitions(shared_library_tests PRIVATE -DGRIDDYN_TEST_DIRECTORY="${test_dir}")
target_link_libraries(shared_library_tests PUBLIC griddyn_shared_lib)

if(CXX_STANDARD_FLAG)
    if(MSVC)
        target_compile_options(shared_library_tests PUBLIC ${CXX_STANDARD_FLAG})
    else()
        target_compile_options(
            shared_library_tests PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${CXX_STANDARD_FLAG}>
        )
    endif(MSVC)
endif(CXX_STANDARD_FLAG)

target_link_libraries(shared_library_tests PUBLIC gtest gtest_main gmock)

add_test(NAME shared_library_tests COMMAND shared_library_tests)
set_target_properties(shared_library_tests PROPERTIES FOLDER tests)
