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

project(tinyxml2)
set(tinyxml2files tinyxml2.cpp)
set(tinyxml2_headers tinyxml2.h)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(tinyxml2 STATIC ${tinyxml2files} ${tinyxml2_headers})

include(CheckLatestCXXStandardOption)
IF (VERSION_OPTION)
	IF (MSVC)
		target_compile_options(tinyxml2 PUBLIC ${VERSION_OPTION})
	ELSE()
		target_compile_options(tinyxml2 PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${VERSION_OPTION}>)
	ENDIF (MSVC)
ELSE()
set(CMAKE_CXX_STANDARD 14)
ENDIF ()



if (NOT CMAKE_DEBUG_POSTFIX)
set(CMAKE_DEBUG_POSTFIX d)
endif()


IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(tinyxml2 PRIVATE -Wno-suggest-override)
ENDIF ()



INSTALL(TARGETS tinyxml2
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/  
	EXPORT tinyxml2-targets)
	
install (EXPORT tinyxml2-targets NAMESPACE tinyxml2:: DESTINATION cmake/)

INSTALL(FILES ${tinyxml2_headers} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/tinyxml2)