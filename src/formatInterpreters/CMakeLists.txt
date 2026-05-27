# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(elementReader_headers
    JsonreaderElement.h
    jsonElement.h
    XmlreaderElement.h
    readerElement.h
    tomlElement.h
    TomlreaderElement.h
    InireaderElement.h
    YamlreaderElement.h
    yamlElement.h
)

set(elementReader_files
    JsonReaderElement.cpp
    JsonElement.cpp
    XmlReaderElement.cpp
    ReaderElement.cpp
    TomlElement.cpp
    TomlReaderElement.cpp
    IniReaderElement.cpp
    YamlReaderElement.cpp
    YamlElement.cpp
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
target_include_directories(
    formatInterpreter SYSTEM PRIVATE $<TARGET_PROPERTY:yaml-cpp,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(formatInterpreter PUBLIC pugixml::pugixml utilities yaml-cpp)
set_target_properties(formatInterpreter PROPERTIES FOLDER libraries)

install(FILES ${elementReader_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn
        COMPONENT headers
)



