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

option(GRIDDYN_ENABLE_TCP "Enable TCP connection library" ON)

option(GRIDDYN_ENABLE_DIME "Enable connection with Dime" OFF)

# ------------------------------------------------------------
# Enable ZMQ interfacing
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_DIME)
    set(ZMQ_NEEDED ON CACHE BOOL "ZMQ is needed" FORCE)
endif(GRIDDYN_ENABLE_DIME)

hide_variable(ZMQ_NEEDED)

# If ZMQ library is enabled try to locate it and link against it
cmake_dependent_option(GRIDDYN_ENABLE_ZMQ "Enable ZMQ networking library" OFF "NOT ZMQ_NEEDED" OFF)

add_library(zmq INTERFACE)
if(GRIDDYN_ENABLE_ZMQ OR ZMQ_NEEDED)
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

if(GRIDDYN_ENABLE_ZMQ OR ZMQ_NEEDED)
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

if(GRIDDYN_ENABLE_DIME)
    set(dimeInterface_sources dimeCollector.cpp dimeCommunicator.cpp dimeClientInterface.cpp
                              dimeRunner.cpp dimeInterface.cpp
    )

    set(dimeInterface_headers dimeCollector.h dimeCommunicator.h dimeClientInterface.h dimeRunner.h
                              dimeInterface.h
    )
    list(APPEND networking_sources ${dimeInterface_sources})
    list(APPEND networking_headers ${dimeInterface_headers})
endif()

if(GRIDDYN_ENABLE_TCP)
    set(tcpInterface_sources AsioServiceManager.cpp tcpCommunicator.cpp tcpInterface.cpp
                             TcpHelperClasses.cpp tcpCollector.cpp
    )

    set(tcpInterface_headers AsioServiceManager.h tcpCommunicator.h tcpInterface.h
                             TcpHelperClasses.h tcpCollector.h
    )
    list(APPEND networking_sources ${tcpInterface_sources})
    list(APPEND networking_headers ${tcpInterface_headers})
endif()

add_library(networking STATIC ${networking_sources} ${networking_headers})
target_include_directories(
    networking
    # target_link_libraries(this, jsoncpp::jsoncpp) when available
    PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
)

# TODO might not need this
target_link_libraries(networking PRIVATE griddyn)
target_link_libraries(networking PUBLIC jsoncpp_lib)

set_target_properties(networking PROPERTIES FOLDER libraries)

if(GRIDDYN_ENABLE_ZMQ OR ZMQ_NEEDED)
    target_link_libraries(networking PRIVATE griddyn::zmq)

    source_group("ZMQLibrary" FILES ${zmqLibrary_sources} ${zmqLibrary_headers})
    source_group("zmqinterface" FILES ${zmqInterface_sources} ${zmqInterface_headers})
endif()

if(GRIDDYN_ENABLE_DIME)
    source_group("dime" FILES ${dimeInterface_sources} ${dimeInterface_headers})
endif()

if(GRIDDYN_ENABLE_TCP)
    source_group("tcp" FILES ${tcpInterface_sources} ${tcpInterface_headers})
endif()

list(APPEND optional_library_key_headers networking/networkingInterface.h)
list(APPEND optional_library_functions loadNetworkingLibrary)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)

if(GRIDDYN_ENABLE_ZMQ OR ZMQ_NEEDED)
    list(APPEND optional_component_test_files ${CMAKE_CURRENT_SOURCE_DIR}/test/zmqTest.cpp)
    set(optional_component_test_files ${optional_component_test_files} PARENT_SCOPE)

    list(APPEND optional_component_test_linkages griddyn::zmq)
    set(optional_component_test_linkages ${optional_component_test_linkages} PARENT_SCOPE)
endif()
