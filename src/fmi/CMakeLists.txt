# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
set(fmiImport_sources
    fmi_import/fmiImport.cpp
    fmi_import/fmiInfo.cpp
    fmi_import/fmi2Object.cpp
    fmi_import/fmi2ModelExchangeObject.cpp
    fmi_import/fmi2CoSimObject.cpp
    fmi_import/fmiVariableSet.cpp
    fmi_import/fmiEnumDefinitions.cpp
    fmi_import/fmiLibraryManager.cpp
)

set(fmiGD_sources
    fmiGDinfo.cpp
    fmi_models/fmiMESubModel.cpp
    fmi_models/fmiCoSimSubModel.cpp
    fmi_models/fmiMELoad.cpp
    fmi_models/fmiCoSimLoad.cpp
    fmi_models/fmiMELoad3phase.cpp
    fmi_models/fmiCoSimLoad3phase.cpp
    fmi_models/fmiExciter.cpp
    fmi_models/outputEstimator.cpp
    fmi_models/fmiGovernor.cpp
    fmi_models/fmiGenModel.cpp
    fmi_models/CymeDistLoad.cpp
)

set(fmiGD_headers
    fmi_models/fmiMESubModel.h
    fmi_models/fmiCoSimSubModel.h
    fmi_models/fmiMELoad.h
    fmi_models/fmiCoSimLoad.h
    fmi_models/fmiMELoad3phase.h
    fmi_models/fmiCoSimLoad3phase.h
    fmi_models/fmiExciter.h
    fmi_models/fmiGovernor.h
    fmi_models/fmiGenModel.h
    fmi_models/outputEstimator.h
    fmi_models/fmiSupport.h
    fmi_models/fmiMEWrapper.hpp
    fmi_models/fmiCoSimWrapper.hpp
    fmi_models/fmiWrapper.hpp
    fmi_models/CymeDistLoad.h
    fmiGDinfo.h
)
set(fmiImport_headers fmi_import/fmiInfo.h fmi_import/fmiImport.h fmi_import/fmiObjects.h
                      fmi_import/fmiEnumDefinitions.h fmi_import/fmiLibraryManager.h
)

add_library(
    fmiGDLibrary STATIC ${fmiGD_sources} ${fmiImport_sources} ${fmiImport_headers} ${fmiGD_headers}
)

source_group("gridDynFMU" FILES ${fmiGD_sources} ${fmiGD_headers})
source_group("FMUImport" FILES ${fmiImport_sources} ${fmiImport_headers})

set_target_properties(fmiGDLibrary PROPERTIES FOLDER fmi)

target_link_libraries(fmiGDLibrary PRIVATE griddyn formatInterpreter)
target_compile_definitions(fmiGDLibrary PRIVATE BOOST_DLL_USE_STD_FS)
if(UNIX)
    target_link_libraries(fmiGDLibrary PRIVATE dl)
endif()

list(APPEND optional_library_key_headers fmi/fmiGDinfo.h)
list(APPEND optional_library_functions loadFmiLibrary)

set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)

list(APPEND optional_component_test_files ${PROJECT_SOURCE_DIR}/test/componentTests/testFMI.cpp)
set(optional_component_test_files ${optional_component_test_files} PARENT_SCOPE)
