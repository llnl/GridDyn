# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(coreObject_headers
    coreDefinitions.hpp
    CoreExceptions.h
    CoreObject.h
    CoreObjectList.h
    coreObjectTemplates.hpp
    CoreOwningPtr.hpp
    CoreProperties.hpp
    FactoryTemplates.hpp
    HelperObject.h
    NullObject.h
    ObjectFactory.hpp
    ObjectFactoryTemplates.hpp
    ObjectInterpreter.h
    ObjectOperatorInterface.hpp
    PropertyBuffer.h
)

set(coreObject_sources
    CoreExceptions.cpp
    CoreObject.cpp
    CoreObjectList.cpp
    HelperObject.cpp
    NullObject.cpp
    ObjectFactory.cpp
    ObjectInterpreter.cpp
    PropertyBuffer.cpp
)

add_library(coreObjects STATIC ${coreObject_headers} ${coreObject_sources})
set_target_properties(coreObjects PROPERTIES FOLDER libraries)
target_link_libraries(coreObjects PUBLIC utilities)
target_link_libraries(coreObjects PUBLIC units-static)

target_include_directories(
    coreObjects
    # target_link_libraries(this, extra_includes::variant) when available
    SYSTEM PUBLIC # TODO stop leaking the variant include, make this private
                  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
)
