# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(GRIDDYN_LIBRARY_LOADER_CPP ${CMAKE_CURRENT_BINARY_DIR}/libraryLoader.cpp)

file(
    WRITE ${GRIDDYN_LIBRARY_LOADER_CPP}
    "/*\n"
    "* Copyright (c) 2014-2020, Lawrence Livermore National Security\n"
    "* See the top-level NOTICE for additional details. All rights reserved.\n"
    "* SPDX-License-Identifier: BSD-3-Clause\n"
    "*/\n"
    "\n"
    "//automatically generated file\n"
    "\#include \"gridDynLoader/libraryLoader.h\"\n"
    "\#include \"griddyn/griddyn-config.h\"\n"
)

foreach(header ${optional_library_key_headers})
    file(APPEND ${GRIDDYN_LIBRARY_LOADER_CPP} "\n" "\#include \"${header}\"\n")
endforeach(header)

file(APPEND ${GRIDDYN_LIBRARY_LOADER_CPP} "namespace griddyn {\n"
                                                    "void loadLibraries()\n" "{\n"
)

foreach(ns ${optional_library_functions})
    file(APPEND ${GRIDDYN_LIBRARY_LOADER_CPP} "\n" "${ns}();\n" "\n")
endforeach(ns)

file(APPEND ${GRIDDYN_LIBRARY_LOADER_CPP} "\n" "}\n" "}//namespace griddyn")

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
