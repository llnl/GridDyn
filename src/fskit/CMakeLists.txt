# LLNS Copyright Start
# Copyright (c) 2016, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

set(fskit_sources
    fskitCommunicator.cpp
    gridDynFederatedScheduler.cpp
    gridDynfskit.cpp
    zonalRelayLogicalProcess.cpp
    fskitRunner.cpp
    GridDynFederatedSimulator.cpp
)

set(fskit_headers
    fskitCommunicator.h
    gridDynFederatedScheduler.h
    gridDynfskit.h
    zonalRelayLogicalProcess.h
    fskitRunner.h
    GridDynFederatedSimulator.h
    griddyn-tracer.h
)

find_package(FSKIT)

add_library(fskitLibrary STATIC ${fskit_sources} ${fskit_headers})

find_package(Boost ${BOOST_MINIMUM_VERSION} COMPONENTS mpi serialization REQUIRED)

target_include_directories(fskitLibrary SYSTEM PRIVATE ${Boost_INCLUDE_DIR})
target_include_directories(fskitLibrary PUBLIC ${FSKIT_INCLUDE_DIR})
target_link_libraries(
    fskitLibrary runner ${FSKIT_LIBRARIES} ${Boost_SERIALIZATION_LIBRARY} ${Boost_MPI_LIBRARY}
)

install(
    TARGETS fskitLibrary
    EXPORT griddyn-targets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT libraries
)

install(FILES ${fskit_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn/fskit
        COMPONENT headers
)

list(APPEND optional_library_key_headers fskit/gridDynfskit.h)
list(APPEND optional_library_functions loadFskit)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)
