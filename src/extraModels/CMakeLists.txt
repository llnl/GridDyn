# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(extra_sources extraModels.cpp txThermalModel.cpp txLifeSpan.cpp)

set(extra_headers extraModels.h txThermalModel.h txLifeSpan.h)

add_library(extraModelLibrary STATIC ${extra_sources} ${extra_headers})
set_target_properties(extraModelLibrary PROPERTIES FOLDER libraries)

target_link_libraries(extraModelLibrary PUBLIC griddyn)

list(APPEND optional_library_key_headers extraModels/extraModels.h)
list(APPEND optional_library_functions loadExtraModels)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)
