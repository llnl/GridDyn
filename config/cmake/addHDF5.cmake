# Copyright (c) 2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# Keep upstream's generic build options local to its directory. GridDyn owns the C++ storage
# interface and uses only HDF5's C library.
function(griddyn_add_hdf5)
    set(hdf5_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/hdf5")
    set(hdf5_BINARY_DIR "${PROJECT_BINARY_DIR}/ThirdParty/hdf5")

    if(NOT EXISTS "${hdf5_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "The HDF5 submodule was not found at ${hdf5_SOURCE_DIR}. "
                            "Initialize/update submodules before building GridDyn."
        )
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_TESTING OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(HDF5_BUILD_CPP_LIB OFF)
    set(HDF5_BUILD_HL_LIB OFF)
    set(HDF5_BUILD_FORTRAN OFF)
    set(HDF5_BUILD_JAVA OFF)
    set(HDF5_BUILD_TOOLS OFF)
    set(HDF5_BUILD_EXAMPLES OFF)
    set(HDF5_ENABLE_PARALLEL OFF)
    set(HDF5_ENABLE_THREADSAFE OFF)
    set(HDF5_ENABLE_CONCURRENCY OFF)
    set(HDF5_ENABLE_PLUGIN_SUPPORT OFF)
    set(HDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16 OFF)
    set(HDF5_ENABLE_SZIP_SUPPORT OFF)
    set(HDF5_ENABLE_ZLIB_SUPPORT ON)
    set(HDF5_ALLOW_EXTERNAL_SUPPORT NO)
    set(HDF5_NO_PACKAGES ON)
    # HDF5's supported parent-project hook reuses GridDyn's zlib without discovery, downloads or
    # another compression-library build.
    set(H5_ZLIB_HEADER "zlib.h")
    set(H5_ZLIB_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ThirdParty/Zlib/zlib-1.3.2"
                             "${PROJECT_BINARY_DIR}/Zlib"
    )
    get_target_property(griddyn_hdf5_zlib ${GRIDDYN_ZLIB_TARGET} ALIASED_TARGET)
    if(NOT griddyn_hdf5_zlib)
        set(griddyn_hdf5_zlib ${GRIDDYN_ZLIB_TARGET})
    endif()
    set(HDF5_LIB_DEPENDENCIES ${griddyn_hdf5_zlib})
    add_subdirectory("${hdf5_SOURCE_DIR}" "${hdf5_BINARY_DIR}" EXCLUDE_FROM_ALL)

    if(NOT TARGET hdf5-static)
        message(FATAL_ERROR "The HDF5 submodule did not create the expected hdf5-static target.")
    endif()
    add_library(GridDyn::hdf5 ALIAS hdf5-static)
    set_target_properties(hdf5-static PROPERTIES FOLDER Extern/HDF5)
    set_property(
        TARGET hdf5-static PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                                    $<TARGET_PROPERTY:hdf5-static,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()

griddyn_add_hdf5()
griddyn_hide_cache_variables_by_prefix(HDF5_ H5_)

install(
    FILES "${PROJECT_SOURCE_DIR}/ThirdParty/hdf5/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/griddyn/licenses"
    RENAME HDF5-LICENSE
    COMPONENT runtime
)
