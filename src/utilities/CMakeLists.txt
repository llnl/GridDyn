# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(utilities_sources
    GridRandom.cpp
    Saturation.cpp
    ThreePhaseFunctions.cpp
    matrixOps.cpp
    functionInterpreter.cpp
    zipUtilities.cpp
    matrixCreation.cpp
    logger.cpp
    MatrixDataSparse.cpp
    OperatingBoundary.cpp
    GlobalWorkQueue.cpp
)

set(utilities_headers
    dataDictionary.hpp
    ThreePhaseFunctions.h
    GridRandom.h
    Saturation.h
    vectData.hpp
    MatrixData.hpp
    matrixOps.h
    MatrixDataCompact.hpp
    MatrixDataSparse.hpp
    MatrixDataSparse_impl.hpp
    MatrixDataBoost.hpp
    MatrixDataSparseSM.hpp
    MatrixDataTranslate.hpp
    MatrixDataContainer.hpp
    MatrixDataScale.hpp
    MatrixDataOrdered.hpp
    MatrixDataOrdering.hpp
    MatrixDataFilter.hpp
    MatrixDataCustomWriteOnly.hpp
    functionInterpreter.h
    zipUtilities.h
    valuePredictor.hpp
    matrixCreation.h
    indexTypes.hpp
    logger.h
    OperatingBoundary.h
    GlobalWorkQueue.hpp
)

add_library(utilities STATIC ${utilities_sources} ${utilities_headers})
set(SKIP_INSTALL_LIBRARIES ON)
set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "Disable zlib contrib minizip in griddyn builds" FORCE)
add_subdirectory(
    ${PROJECT_SOURCE_DIR}/ThirdParty/Zlib/zlib-1.3.2 ${PROJECT_BINARY_DIR}/Zlib EXCLUDE_FROM_ALL
)

set(griddyn_minizip_use_bundled_zlib OFF)
if(WIN32 OR CMAKE_C_FLAGS MATCHES "(^| )-fsanitize=" OR CMAKE_CXX_FLAGS MATCHES "(^| )-fsanitize=")
    set(griddyn_minizip_use_bundled_zlib ON)
endif()

if(griddyn_minizip_use_bundled_zlib)
    set(GRIDDYN_MINIZIP_USE_EXISTING_ZLIB_TARGETS ON)
    set(MINIZIP_BUILD_SHARED OFF CACHE BOOL "Disable shared Minizip when building from bundled zlib"
                                    FORCE
    )
    set(MINIZIP_BUILD_STATIC ON CACHE BOOL "Enable static Minizip when building from bundled zlib"
                                    FORCE
    )
    set(MINIZIP_BUILD_TESTING OFF
        CACHE BOOL "Disable Minizip tests when building from bundled zlib" FORCE
    )
    set(MINIZIP_INSTALL OFF CACHE BOOL "Disable Minizip install rules for bundled builds" FORCE)
endif()

add_subdirectory(
    ${PROJECT_SOURCE_DIR}/ThirdParty/Minizip ${PROJECT_BINARY_DIR}/Minizip EXCLUDE_FROM_ALL
)

if(griddyn_minizip_use_bundled_zlib AND TARGET ZLIB::ZLIBSTATIC)
    set(GRIDDYN_ZLIB_TARGET ZLIB::ZLIBSTATIC)
elseif(TARGET ZLIB::ZLIB)
    set(GRIDDYN_ZLIB_TARGET ZLIB::ZLIB)
elseif(TARGET ZLIB::ZLIBSTATIC)
    set(GRIDDYN_ZLIB_TARGET ZLIB::ZLIBSTATIC)
else()
    set(GRIDDYN_ZLIB_TARGET zlib::zlib)
endif()

if(TARGET MINIZIP::minizip)
    set(GRIDDYN_MINIZIP_TARGET MINIZIP::minizip)
elseif(TARGET MINIZIP::minizipstatic)
    set(GRIDDYN_MINIZIP_TARGET MINIZIP::minizipstatic)
else()
    if(TARGET minizip::minizip)
        set(GRIDDYN_MINIZIP_TARGET minizip::minizip)
    else()
        set(GRIDDYN_MINIZIP_TARGET minizip::minizipstatic)
    endif()
endif()

target_link_libraries(
    utilities PUBLIC ${GRIDDYN_MINIZIP_TARGET} ${GRIDDYN_ZLIB_TARGET} gmlc_utilities griddyn_base
                     spdlog::spdlog
)

set_target_properties(utilities PROPERTIES FOLDER libraries)
foreach(griddyn_external_target IN ITEMS minizip libminizip libminizipstatic zlib zlibstatic)
    if(TARGET ${griddyn_external_target})
        set_target_properties(${griddyn_external_target} PROPERTIES FOLDER Extern)
    endif()
endforeach()
if(ENABLE_64_BIT_INDEXING)
    target_compile_definitions(utilities PUBLIC -DENABLE_64_BIT_INDEXING)
endif(ENABLE_64_BIT_INDEXING)
if(UNSIGNED_INDEXING)
    target_compile_definitions(utilities PUBLIC -DUNSIGNED_INDEXING)
endif(UNSIGNED_INDEXING)
