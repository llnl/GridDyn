# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# -----------------------------------------------------------------------------
# create the spdlog target
# -----------------------------------------------------------------------------

if(NOT TARGET spdlog::spdlog)
    option(${PROJECT_NAME}_USE_EXTERNAL_SPDLOG "Use external copy of spdlog" OFF)
    mark_as_advanced(${PROJECT_NAME}_USE_EXTERNAL_SPDLOG)

    if(${PROJECT_NAME}_USE_EXTERNAL_SPDLOG)
        find_package(spdlog REQUIRED)
    else()
        # GridDyn requires C++23, so prefer std::format over spdlog's bundled fmt copy.
        set(SPDLOG_USE_STD_FORMAT ON CACHE INTERNAL "")
        if(CMAKE_SYSTEM_NAME STREQUAL "CYGWIN")
            set(CMAKE_CXX_EXTENSIONS ON CACHE INTERNAL "")
        endif()

        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
            add_subdirectory(ThirdParty/spdlog EXCLUDE_FROM_ALL SYSTEM)
        else()
            add_subdirectory(ThirdParty/spdlog EXCLUDE_FROM_ALL)
        endif()

        set_target_properties(spdlog PROPERTIES FOLDER Extern)
        hide_variable(SPDLOG_BUILD_ALL)
        hide_variable(SPDLOG_BUILD_BENCH)
        hide_variable(SPDLOG_BUILD_EXAMPLE)
        hide_variable(SPDLOG_BUILD_EXAMPLE_HO)
        hide_variable(SPDLOG_BUILD_SHARED)
        hide_variable(SPDLOG_BUILD_TESTS)
        hide_variable(SPDLOG_BUILD_TESTS_HO)
        hide_variable(SPDLOG_BUILD_WARNINGS)
        hide_variable(SPDLOG_ENABLE_PCH)
        hide_variable(SPDLOG_NO_ATOMIC_LEVELS)
        hide_variable(SPDLOG_NO_EXCEPTIONS)
        hide_variable(SPDLOG_NO_THREAD_ID)
        hide_variable(SPDLOG_NO_TLS)
        hide_variable(SPDLOG_PREVENT_CHILD_FD)
        hide_variable(SPDLOG_SANITIZE_ADDRESS)
        hide_variable(SPDLOG_SANITIZE_THREAD)
        hide_variable(SPDLOG_TIDY)
        hide_variable(SPDLOG_WCHAR_FILENAMES)
        hide_variable(SPDLOG_WCHAR_SUPPORT)
        hide_variable(SPDLOG_DISABLE_DEFAULT_LOGGER)
        hide_variable(SPDLOG_FMT_EXTERNAL)
        hide_variable(SPDLOG_FMT_EXTERNAL_HO)
        hide_variable(SPDLOG_INSTALL)
        hide_variable(SPDLOG_CLOCK_COARSE)
        hide_variable(SPDLOG_USE_STD_FORMAT)
        hide_variable(SPDLOG_SYSTEM_INCLUDES)
        hide_variable(SPDLOG_BUILD_PIC)
        hide_variable(SPDLOG_FWRITE_UNLOCKED)
        hide_variable(SPDLOG_MSVC_UTF8)
        hide_variable(SPDLOG_WCHAR_CONSOLE)
        hide_variable(SPDLOG_NO_TZ_OFFSET)
    endif()
endif()
