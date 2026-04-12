# LLNS Copyright Start
# Copyright (c) 2016, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

set(griddynServer_sources gridDynServerMain.cpp gridDynServer.cpp)

set(griddynServer_headers gridDynServer.h)

link_directories(${external_link_directories})

add_executable(griddynServer ${griddynServer_sources} ${griddynServer_headers})
target_include_directories(griddynServer SYSTEM PRIVATE ${Boost_INCLUDE_DIR})

include_directories(.)

include_directories(${PROJECT_SOURCE_DIR}/src/formatInterpreter)
include_directories(${PROJECT_SOURCE_DIR}/src/griddyn)
include_directories(${PROJECT_SOURCE_DIR}/src/gridDynFileInput)
include_directories(${PROJECT_SOURCE_DIR}/src/utilities)
include_directories(${PROJECT_SOURCE_DIR}/src/coupling)
include_directories(${PROJECT_SOURCE_DIR}/src/gridDynCombined)

target_link_libraries(griddynServer gridDynCombined)

install(TARGETS griddynServer RUNTIME DESTINATION bin)
