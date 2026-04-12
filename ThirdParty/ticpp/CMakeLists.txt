# LLNS Copyright Start
# Copyright (c) 2017, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department 
# of Energy by Lawrence Livermore National Laboratory in part under 
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

#project name

cmake_minimum_required(VERSION 3.5)
cmake_policy(VERSION 3.5)

project(ticpp)
set(ticppfiles ticpp.cpp tinystr.cpp tinyxml.cpp tinyxmlerror.cpp tinyxmlparser.cpp)
set(ticpp_headers ticppapi.h ticpp.h ticpprc.h tinystr.h tinyxml.h)

add_library(ticpp STATIC ${ticppfiles} ${ticpp_headers})

include(CheckLatestCXXStandardOption)
IF (VERSION_OPTION)
	IF (MSVC)
		target_compile_options(ticpp PUBLIC ${VERSION_OPTION})
	ELSE()
		target_compile_options(ticpp PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${VERSION_OPTION}>)
	ENDIF (MSVC)
ELSE()
set(CMAKE_CXX_STANDARD 14)
ENDIF ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_DEBUG_POSTFIX)
set(CMAKE_DEBUG_POSTFIX d)
endif()

target_compile_options(ticpp PUBLIC -DTIXML_USE_TICPP)
IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(ticpp PRIVATE -Wno-suggest-override)
ENDIF ()


INSTALL(TARGETS ticpp EXPORT ticpp-targets
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/ )

install (EXPORT ticpp-targets NAMESPACE ticpp:: DESTINATION cmake/)

INSTALL(FILES ${ticpp_headers} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/ticpp)
 
 
