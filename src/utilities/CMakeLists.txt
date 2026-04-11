# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(utilities_sources
    gridRandom.cpp
    saturation.cpp
    ThreePhaseFunctions.cpp
    matrixOps.cpp
    functionInterpreter.cpp
    zipUtilities.cpp
    matrixCreation.cpp
    logger.cpp
    loggerCore.cpp
    matrixDataSparse.cpp
    OperatingBoundary.cpp
    GlobalWorkQueue.cpp
)

set(utilities_headers
    dataDictionary.hpp
    ThreePhaseFunctions.h
    gridRandom.h
    saturation.h
    vectData.hpp
    matrixData.hpp
    matrixOps.h
    matrixDataCompact.hpp
    matrixDataSparse.hpp
    matrixDataSparse_impl.hpp
    matrixDataBoost.hpp
    matrixDataSparseSM.hpp
    matrixDataTranslate.hpp
    matrixDataContainer.hpp
    matrixDataScale.hpp
    matrixDataOrdered.hpp
    matrixDataOrdering.hpp
    matrixDataFilter.hpp
    matrixDataCustomWriteOnly.hpp
    functionInterpreter.h
    zipUtilities.h
    valuePredictor.hpp
    matrixCreation.h
    indexTypes.hpp
    logger.h
    loggerCore.hpp
    OperatingBoundary.h
    GlobalWorkQueue.hpp
)

add_library(utilities STATIC ${utilities_sources} ${utilities_headers})
set(SKIP_INSTALL_LIBRARIES ON)
add_subdirectory(
    ${PROJECT_SOURCE_DIR}/ThirdParty/Zlib/zlib-1.3.2 ${PROJECT_BINARY_DIR}/Zlib EXCLUDE_FROM_ALL
)

add_subdirectory(
    ${PROJECT_SOURCE_DIR}/ThirdParty/Minizip ${PROJECT_BINARY_DIR}/Minizip EXCLUDE_FROM_ALL
)

if(TARGET ZLIB::ZLIB)
    set(GRIDDYN_ZLIB_TARGET ZLIB::ZLIB)
else()
    set(GRIDDYN_ZLIB_TARGET zlib::zlib)
endif()

target_link_libraries(
    utilities PUBLIC minizip::minizip ${GRIDDYN_ZLIB_TARGET} gmlc_utilities griddyn_base
)

set_target_properties(utilities PROPERTIES FOLDER libraries)
set_target_properties(minizip zlib PROPERTIES FOLDER Extern)
if(ENABLE_64_BIT_INDEXING)
    target_compile_definitions(utilities PUBLIC -DENABLE_64_BIT_INDEXING)
endif(ENABLE_64_BIT_INDEXING)
if(UNSIGNED_INDEXING)
    target_compile_definitions(utilities PUBLIC -DUNSIGNED_INDEXING)
endif(UNSIGNED_INDEXING)
