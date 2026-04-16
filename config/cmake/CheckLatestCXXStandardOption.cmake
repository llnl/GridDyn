# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Copyright (c) 2017-2019, Battelle Memorial Institute; Lawrence Livermore
# National Security, LLC; Alliance for Sustainable Energy, LLC.
# See the top-level NOTICE for additional details.
# All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

include(CheckCXXCompilerFlag)

if(NOT CMAKE_CXX_STANDARD)
    if(${PROJECT_NAME}_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD ${PROJECT_NAME}_CXX_STANDARD)
    else()
        set(CMAKE_CXX_STANDARD 23)
    endif()
endif()

if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "GridDyn requires C++23 or higher")
endif()

if(MSVC)
    if(CMAKE_CXX_STANDARD EQUAL 23)
        check_cxx_compiler_flag(/std:c++23 has_std_23_flag)
        if(has_std_23_flag)
            set(CXX_STANDARD_FLAG /std:c++23)
            set(has_std_2b_flag ON)
        else()
            set(CXX_STANDARD_FLAG /std:c++latest)
        endif()
    elseif(CMAKE_CXX_STANDARD GREATER_EQUAL 26)
        set(CXX_STANDARD_FLAG /std:c++latest)
    else()
        message(FATAL_ERROR "GridDyn requires C++23 or higher")
    endif()
else()

    if(CMAKE_CXX_STANDARD EQUAL 23)
        check_cxx_compiler_flag(-std=c++23 has_std_23_flag)
        if(has_std_23_flag)
            set(CXX_STANDARD_FLAG -std=c++23)
        else()
            check_cxx_compiler_flag(-std=c++2b has_std_2b_flag)
            if(has_std_2b_flag)
                set(CXX_STANDARD_FLAG -std=c++2b)
            endif()
        endif()
    elseif(CMAKE_CXX_STANDARD GREATER_EQUAL 26)
        check_cxx_compiler_flag(-std=c++26 has_std_26_flag)
        if(has_std_26_flag)
            set(CXX_STANDARD_FLAG -std=c++26)
        else()
            check_cxx_compiler_flag(-std=c++2c has_std_2c_flag)
            if(has_std_2c_flag)
                set(CXX_STANDARD_FLAG -std=c++2c)
            endif()
        endif()
    else()
        message(FATAL_ERROR "GridDyn requires C++23 or higher")
    endif()

endif()
# set(CMAKE_REQUIRED_FLAGS ${CXX_STANDARD_FLAG})

# boost libraries don't compile under /std:c++latest flag 1.66 might solve this issue
