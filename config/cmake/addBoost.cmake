##############################################################################
# Copyright (c) 2017-2026,
# Battelle Memorial Institute; Lawrence Livermore National Security, LLC;
# Alliance for Sustainable Energy, LLC
# All rights reserved. See LICENSE file and DISCLAIMER for more details.
##############################################################################

show_variable(BOOST_INSTALL_PATH PATH "Boost root directory" "${BOOST_INSTALL_PATH}")
mark_as_advanced(BOOST_INSTALL_PATH)

if(UNIX)
    # Since many Unix installs of Boost don't provide or use a CMake package,
    # allow non-multithreaded library layouts and prefer shared libs by default.
    set(Boost_USE_MULTITHREADED OFF)
    option(USE_BOOST_STATIC_LIBS "Build using boost static Libraries" OFF)
else()
    if(MSYS)
        option(USE_BOOST_STATIC_LIBS "Build using boost static Libraries" OFF)
    else()
        option(USE_BOOST_STATIC_LIBS "Build using boost static Libraries" ON)
    endif()
endif()

mark_as_advanced(USE_BOOST_STATIC_LIBS)

set(Boost_USE_STATIC_LIBS ${USE_BOOST_STATIC_LIBS})
if(USE_BOOST_STATIC_LIBS)
    set(BOOST_STATIC ON)
endif()

if(WIN32 AND NOT UNIX_LIKE)
    if(MSVC_VERSION GREATER_EQUAL 1930)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(BOOST_MSVC_LIB_PATH lib32-msvc-14.3)
        else()
            set(BOOST_MSVC_LIB_PATH lib64-msvc-14.3)
        endif()
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(BOOST_MSVC_LIB_PATH lib32-msvc-14.2)
        else()
            set(BOOST_MSVC_LIB_PATH lib64-msvc-14.2)
        endif()
    endif()

    set(boost_versions
        boost_1_88_0
        boost_1_87_0
        boost_1_86_0
        boost_1_85_0
        boost_1_84_0
        boost_1_83_0
        boost_1_82_0
        boost_1_81_0
        boost_1_80_0
        boost_1_79_0
        boost_1_78_0
        boost_1_77_0
        boost_1_76_0
        boost_1_75_0
        boost_1_74_0
        boost_1_73_0
        boost_1_72_0
        boost_1_71_0
        boost_1_70_0
        boost_1_69_0
        boost_1_68_0
        boost_1_67_0
        boost_1_66_0
        boost_1_65_1
        boost_1_65_0
        boost_1_64_0
        boost_1_63_0
        boost_1_62_0
        boost_1_61_0
    )

    set(poss_prefixes
        C:
        C:/local
        C:/boost
        C:/local/boost
        C:/Libraries
        "C:/Program Files/boost"
        C:/ProgramData/chocolatey/lib
        D:
        D:/local
        D:/boost
        D:/local/boost
        D:/Libraries
    )

    list(APPEND boost_paths "")
    foreach(boostver ${boost_versions})
        foreach(dir ${poss_prefixes})
            if(IS_DIRECTORY ${dir}/${boostver} AND EXISTS ${dir}/${boostver}/boost/version.hpp)
                list(APPEND boost_paths ${dir}/${boostver})
            endif()
        endforeach()
    endforeach()

    find_path(
        BOOST_TEST_PATH
        NAMES boost/version.hpp
        HINTS ENV BOOST_INSTALL_PATH
        PATHS ${BOOST_INSTALL_PATH} ${boost_paths}
    )

    if(BOOST_TEST_PATH)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
            find_path(
                BOOST_CMAKE_PATH
                NAMES BoostConfig.cmake
                PATHS ${BOOST_TEST_PATH}/${BOOST_MSVC_LIB_PATH}/cmake
                PATH_SUFFIXES
                    Boost-1.88.0
                    Boost-1.87.0
                    Boost-1.86.0
                    Boost-1.85.0
                    Boost-1.84.0
                    Boost-1.83.0
                    Boost-1.82.0
                    Boost-1.81.0
                    Boost-1.80.0
                    Boost-1.79.0
                    Boost-1.78.0
                    Boost-1.77.0
                    Boost-1.76.0
                    Boost-1.75.0
                    Boost-1.74.0
                    Boost-1.73.0
            )
            if(BOOST_CMAKE_PATH)
                set(Boost_ROOT ${BOOST_CMAKE_PATH})
            else()
                set(Boost_ROOT ${BOOST_TEST_PATH})
            endif()
        else()
            set(Boost_ROOT ${BOOST_TEST_PATH})
        endif()
    endif()
endif()

if(NOT Boost_ROOT)
    if(BOOST_INSTALL_PATH)
        set(Boost_ROOT "${BOOST_INSTALL_PATH}")
    elseif($ENV{BOOST_INSTALL_PATH})
        set(Boost_ROOT "$ENV{BOOST_INSTALL_PATH}")
    else()
        set(Boost_ROOT "$ENV{BOOST_ROOT}")
    endif()
endif()

if(NOT BOOST_ROOT)
    set(BOOST_ROOT ${Boost_ROOT})
endif()

hide_variable(BOOST_TEST_PATH)
hide_variable(BOOST_CMAKE_PATH)

if(NOT BOOST_REQUIRED_LIBRARIES)
    set(BOOST_REQUIRED_LIBRARIES system date_time timer chrono)
endif()

# Minimum version of Boost required for building GridDyn.
set(BOOST_MINIMUM_VERSION 1.73)

find_package(Boost ${BOOST_MINIMUM_VERSION} QUIET COMPONENTS ${BOOST_REQUIRED_LIBRARIES})

if(NOT Boost_FOUND)
    message(STATUS "Boost package lookup failed, retrying via FindBoost module")
    set(Boost_NO_BOOST_CMAKE ON)
    find_package(Boost ${BOOST_MINIMUM_VERSION} QUIET COMPONENTS ${BOOST_REQUIRED_LIBRARIES})
endif()

if(NOT Boost_FOUND)
    message(FATAL_ERROR "Unable to find Boost ${BOOST_MINIMUM_VERSION} with components: ${BOOST_REQUIRED_LIBRARIES}")
endif()

if(NOT Boost_INCLUDE_DIR AND Boost_INCLUDE_DIRS)
    list(GET Boost_INCLUDE_DIRS 0 Boost_INCLUDE_DIR)
endif()

if(NOT Boost_INCLUDE_DIR AND TARGET Boost::headers)
    get_target_property(Boost_INCLUDE_DIR Boost::headers INTERFACE_INCLUDE_DIRECTORIES)
endif()

if(NOT Boost_INCLUDE_DIR AND TARGET Boost::boost)
    get_target_property(Boost_INCLUDE_DIR Boost::boost INTERFACE_INCLUDE_DIRECTORIES)
endif()

if(Boost_INCLUDE_DIR AND NOT Boost_INCLUDE_DIRS)
    set(Boost_INCLUDE_DIRS ${Boost_INCLUDE_DIR})
endif()

if((NOT TARGET Boost::headers) AND (NOT TARGET Boost::boost) AND Boost_INCLUDE_DIR)
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(
        Boost::headers PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIR}"
    )
    add_library(Boost::boost INTERFACE IMPORTED)
    set_target_properties(Boost::boost PROPERTIES INTERFACE_LINK_LIBRARIES Boost::headers)
endif()

if(NOT TARGET Boostlibs::core)
    add_library(Boostlibs::core INTERFACE IMPORTED)
endif()
set(Boost_LIBRARIES_core)
set(griddyn_boost_core_targets)

foreach(boost_component IN LISTS BOOST_REQUIRED_LIBRARIES)
    if(TARGET Boost::${boost_component})
        set(boost_component_link Boost::${boost_component})
    elseif(DEFINED Boost_${boost_component}_LIBRARY AND Boost_${boost_component}_LIBRARY)
        set(boost_component_link ${Boost_${boost_component}_LIBRARY})
    else()
        set(boost_component_link "")
    endif()

    if(boost_component_link)
        list(APPEND Boost_LIBRARIES_core ${boost_component_link})
        list(APPEND griddyn_boost_core_targets ${boost_component_link})
    endif()
endforeach()

if(TARGET Boost::boost)
    list(APPEND griddyn_boost_core_targets Boost::boost)
elseif(TARGET Boost::headers)
    list(APPEND griddyn_boost_core_targets Boost::headers)
endif()

list(REMOVE_DUPLICATES Boost_LIBRARIES_core)
list(REMOVE_DUPLICATES griddyn_boost_core_targets)

set_target_properties(
    Boostlibs::core PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIR}"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIR}"
)

set_target_properties(
    Boostlibs::core PROPERTIES INTERFACE_LINK_LIBRARIES "${griddyn_boost_core_targets}"
)

# Minimum version of Boost required for building test suite
if(Boost_VERSION LESS 106100)
    set(BOOST_VERSION_LEVEL 0)
elseif(Boost_VERSION GREATER 106599)
    # In 1.66 there were some changes to asio and inclusion of beast.
    set(BOOST_VERSION_LEVEL 2)
else()
    set(BOOST_VERSION_LEVEL 1)
endif()
