# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(coreObject_headers
    coreDefinitions.hpp
    coreExceptions.h
    coreObject.h
    coreObjectList.h
    coreObjectTemplates.hpp
    coreOwningPtr.hpp
    coreProperties.hpp
    factoryTemplates.hpp
    helperObject.h
    nullObject.h
    objectFactory.hpp
    objectFactoryTemplates.hpp
    objectInterpreter.h
    objectOperatorInterface.hpp
    propertyBuffer.h
)

set(coreObject_sources
    coreExceptions.cpp
    coreObject.cpp
    coreObjectList.cpp
    helperObject.cpp
    nullObject.cpp
    objectFactory.cpp
    objectInterpreter.cpp
    propertyBuffer.cpp
)

add_library(coreObjects STATIC ${coreObject_headers} ${coreObject_sources})
set_target_properties(coreObjects PROPERTIES FOLDER libraries)
target_link_libraries(coreObjects PUBLIC utilities)
target_link_libraries(coreObjects PUBLIC units-static)

target_include_directories(
    coreObjects
    # target_link_libraries(this, extra_includes::variant) when available
    PUBLIC # TODO stop leaking the variant include, make this private
           $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
)
