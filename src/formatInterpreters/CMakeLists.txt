# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(elementReader_headers
    jsonReaderElement.h
    jsonElement.h
    XmlReaderElement.h
    readerElement.h
    tomlElement.h
    tomlReaderElement.h
    iniReaderElement.h
    yamlReaderElement.h
    yamlElement.h
)

set(elementReader_files
    jsonReaderElement.cpp
    jsonElement.cpp
    XmlReaderElement.cpp
    readerElement.cpp
    tomlElement.cpp
    tomlReaderElement.cpp
    iniReaderElement.cpp
    yamlReaderElement.cpp
    yamlElement.cpp
)

list(APPEND optional_library_test_files
     ${PROJECT_SOURCE_DIR}/src/formatInterpreters/test/testYamlReader.cpp
)
set(optional_library_test_files ${optional_library_test_files} PARENT_SCOPE)

set(ZLIB_ROOT_DIR ${PROJECT_BINARY_DIR}/libs)

add_library(formatInterpreter STATIC ${elementReader_files} ${elementReader_headers})

target_include_directories(
    formatInterpreter PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
                              $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty/toml/include>
)

target_link_libraries(formatInterpreter PUBLIC pugixml::pugixml utilities yaml-cpp)
set_target_properties(formatInterpreter PROPERTIES FOLDER libraries)

install(FILES ${elementReader_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn
        COMPONENT headers
)
