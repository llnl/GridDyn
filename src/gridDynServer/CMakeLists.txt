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

add_executable(griddynServer ${griddynServer_sources} ${griddynServer_headers})
target_include_directories(
    griddynServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/griddyn
                          ${PROJECT_SOURCE_DIR}/src/gridDynLoader ${PROJECT_SOURCE_DIR}/src/runner
)
target_include_directories(griddynServer SYSTEM PRIVATE ${Boost_INCLUDE_DIR})

target_link_libraries(griddynServer PRIVATE runner griddyn gridDynLoader)

install(TARGETS griddynServer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT RUNTIME)
