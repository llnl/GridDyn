# LLNS Copyright Start
# Copyright (c) 2014-2018, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

set(networking_sources networkingInterface.cpp)
set(networking_headers networkingInterface.h)

if(NOT TARGET gmlc::networking)
    add_subdirectory(
        ${PROJECT_SOURCE_DIR}/ThirdParty/networking ${PROJECT_BINARY_DIR}/ThirdParty/networking
        EXCLUDE_FROM_ALL
    )
    griddyn_hide_cache_variables_by_prefix(GMLC_)
endif()
if(TARGET gmlc_networking)
    set_target_properties(gmlc_networking PROPERTIES FOLDER Extern)
endif()

option(GRIDDYN_ENABLE_TCP "Enable TCP connection library" ON)

option(GRIDDYN_ENABLE_ZMQ "Enable ZMQ networking library" OFF)

add_library(zmq INTERFACE)
if(GRIDDYN_ENABLE_ZMQ)
    include(addZeroMQ)

    if(NOT ZeroMQ_FOUND)
        message(FATAL_ERROR "ZeroMQ not found, needed to enable the ZMQ Core")
    endif()
    if(GRIDDYN_USE_ZMQ_STATIC_LIBRARY)
        target_link_libraries(zmq INTERFACE libzmq-static)
        target_compile_definitions(zmq INTERFACE -DZMQ_STATIC)
    else()
        target_link_libraries(zmq INTERFACE libzmq)
    endif()
endif()
add_library(griddyn::zmq ALIAS zmq)

if(GRIDDYN_ENABLE_ZMQ)
    set(zmqLibrary_sources
        zmqLibrary/zmqContextManager.cpp zmqLibrary/zmqHelper.cpp zmqLibrary/zmqReactor.cpp
        zmqLibrary/zmqProxyHub.cpp zmqLibrary/zmqSocketDescriptor.cpp
    )

    set(zmqLibrary_headers
        zmqLibrary/zmqContextManager.h
        zmqLibrary/zmqHelper.h
        zmqLibrary/zmqReactor.h
        zmqLibrary/zmqProxyHub.h
        zmqLibrary/zmqSocketDescriptor.h
        ${GRIDDYN_SOURCE_DIR}/ThirdParty/cppzmq/zmq.hpp
        ${GRIDDYN_SOURCE_DIR}/ThirdParty/cppzmq/zmq_addon.hpp
    )

    set(zmqInterface_sources zmqCommunicator.cpp zmqInterface.cpp)

    set(zmqInterface_headers zmqCommunicator.h zmqInterface.h)

    list(APPEND networking_sources ${zmqLibrary_sources})
    list(APPEND networking_headers ${zmqLibrary_headers})
    list(APPEND networking_sources ${zmqInterface_sources})
    list(APPEND networking_headers ${zmqInterface_headers})

endif()

if(GRIDDYN_ENABLE_TCP)
    set(tcpInterface_sources tcpCommunicator.cpp tcpInterface.cpp tcpCollector.cpp)

    set(tcpInterface_headers tcpCommunicator.h tcpInterface.h tcpCollector.h)
    list(APPEND networking_sources ${tcpInterface_sources})
    list(APPEND networking_headers ${tcpInterface_headers})
endif()

add_library(networking STATIC ${networking_sources} ${networking_headers})
target_include_directories(networking PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>)

# TODO might not need this
target_link_libraries(networking PRIVATE griddyn)

set_target_properties(networking PROPERTIES FOLDER libraries)

if(GRIDDYN_ENABLE_ZMQ)
    target_link_libraries(networking PRIVATE griddyn::zmq)

    source_group("ZMQLibrary" FILES ${zmqLibrary_sources} ${zmqLibrary_headers})
    source_group("zmqinterface" FILES ${zmqInterface_sources} ${zmqInterface_headers})
endif()

if(GRIDDYN_ENABLE_TCP)
    source_group("tcp" FILES ${tcpInterface_sources} ${tcpInterface_headers})
    target_link_libraries(networking PRIVATE gmlc::networking)
endif()

list(APPEND optional_library_key_headers networking/networkingInterface.h)
list(APPEND optional_library_functions loadNetworkingLibrary)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)

if(GRIDDYN_ENABLE_ZMQ)
    list(APPEND optional_component_test_files ${CMAKE_CURRENT_SOURCE_DIR}/test/zmqTest.cpp)
    set(optional_component_test_files ${optional_component_test_files} PARENT_SCOPE)

    list(APPEND optional_component_test_linkages griddyn::zmq)
    set(optional_component_test_linkages ${optional_component_test_linkages} PARENT_SCOPE)
endif()
