# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(GRIDDYN_LIBRARY_LOADER_CPP ${CMAKE_CURRENT_BINARY_DIR}/libraryLoader.cpp)

set(GRIDDYN_LIBRARY_LOADER_CONTENT
    [=[/*
* Copyright (c) 2014-2026, Lawrence Livermore National Security
* See the top-level NOTICE for additional details. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause
*/

//automatically generated file
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/griddyn-config.h"
]=]
)

foreach(header ${optional_library_key_headers})
    string(APPEND GRIDDYN_LIBRARY_LOADER_CONTENT "\n#include \"${header}\"\n")
endforeach(header)

string(APPEND GRIDDYN_LIBRARY_LOADER_CONTENT "\nnamespace griddyn {\nvoid loadLibraries()\n{\n")

foreach(ns ${optional_library_functions})
    string(APPEND GRIDDYN_LIBRARY_LOADER_CONTENT "\n${ns}();\n")
endforeach(ns)

string(APPEND GRIDDYN_LIBRARY_LOADER_CONTENT "\n}\n}  // namespace griddyn\n")

file(WRITE ${GRIDDYN_LIBRARY_LOADER_CPP} "${GRIDDYN_LIBRARY_LOADER_CONTENT}")

add_library(gridDynLoader STATIC libraryLoader.h ${GRIDDYN_LIBRARY_LOADER_CPP})

set_target_properties(gridDynLoader PROPERTIES FOLDER libraries)

# message("optlibs: ${griddyn_optional_libraries}") message("optheaders:
# ${griddyn_optional_include_dirs}") message("external_libraries: ${external_library_list}")
# message("external_lib_dirs: ${external_link_directories}")

# fileInput needs to come first since it can depend on some of the optional libraries which in turn
# depend on the gridDyn Libraries and the external libraries
target_link_libraries(gridDynLoader griddyn_optional)

# TODO ENABLE_FMI should mark DL_REQUIRED
if(DL_REQUIRED OR ENABLE_FMI)
    target_link_libraries(gridDynLoader ${CMAKE_DL_LIBS})
endif(DL_REQUIRED OR ENABLE_FMI)
