# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(runner_sources gridDynRunner.cpp)

set(runner_headers gridDynRunner.h)

add_library(runner STATIC ${runner_sources} ${runner_headers})
target_compile_definitions(runner PUBLIC -DCLI11_EXPERIMENTAL_OPTIONAL=0)

target_link_libraries(runner PUBLIC fileInput utilities)

set_target_properties(runner PROPERTIES FOLDER libraries)
