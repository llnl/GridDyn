add_subdirectory(core)
add_subdirectory(coupling)
add_subdirectory(fileInput)
add_subdirectory(formatInterpreters)
add_subdirectory(griddyn)
add_subdirectory(gridDynMain)
add_subdirectory(runner)
add_subdirectory(utilities)

# ------------------------------------------------------------
# Enable support for Plugins
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_PLUGINS)
    if(BOOST_VERSION_LEVEL==0)
        set(ENABLE_PLUGINS OFF)
        message(WARNING "Plugin support requires BOOST 1.61 or higher")
    else()
        add_subdirectory(plugins)
        set(DL_REQUIRED ON)
    endif()
endif(GRIDDYN_ENABLE_PLUGINS)

# ------------------------------------------------------------
# Enable compilation of extraModels?
# ------------------------------------------------------------
# If extra models are used enabled try to locate the libraries link against them.
if(GRIDDYN_ENABLE_EXTRA_MODELS)
    add_subdirectory(extraModels)
    target_link_libraries(griddyn_optional INTERFACE extraModelLibrary)
endif(GRIDDYN_ENABLE_EXTRA_MODELS)

# ------------------------------------------------------------
# Enable compilation of extraSolvers?
# ------------------------------------------------------------
# If extra solvers are used enabled try to locate the libraries link against them.
if(GRIDDYN_ENABLE_EXTRA_SOLVERS)
    add_subdirectory(extraSolvers)
    target_link_libraries(griddyn_optional INTERFACE extraSolverLibrary)
endif(GRIDDYN_ENABLE_EXTRA_SOLVERS)

# ------------------------------------------------------------
# Enable compilation of FMI?
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_FMI)
    if(BOOST_VERSION_LEVEL==0)
        set(ENABLE_FMI OFF CACHE BOOL "Enable FMI Support" FORCE)
        message(WARNING "FMI support requires BOOST 1.61 or higher, disabling it")
    else()
        add_subdirectory(fmi)
        target_link_libraries(griddyn_optional INTERFACE fmiGDLibrary)
        set(DL_REQUIRED ON)
    endif()
endif(GRIDDYN_ENABLE_FMI)

# ------------------------------------------------------------
# Enable compilation of FMI_EXPORT?
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_FMI_EXPORT)
    add_subdirectory(fmi_export)
    target_link_libraries(griddyn_optional INTERFACE fmiBuilder)
endif(GRIDDYN_ENABLE_FMI_EXPORT)

# ------------------------------------------------------------
# Enable compilation of FSKIT?
# ------------------------------------------------------------
if(ENABLE_FSKIT)
    # TODO this needs to do a find_package(fskit) of some sort
    add_subdirectory(fskit)
    target_link_libraries(griddyn_optional INTERFACE fskitLibrary)
endif(ENABLE_FSKIT)

# ------------------------------------------------------------
# Enable compilation of networking?
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_NETWORKING_LIBRARY)
    add_subdirectory(networking)
    target_link_libraries(griddyn_optional INTERFACE networking)
endif()

# ------------------------------------------------------------
# Enable compilation of HELICS?
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_HELICS_EXECUTABLE)
    include(addHELICS)
    add_subdirectory(helics)
    target_link_libraries(griddyn_optional INTERFACE helicsLib)
endif(GRIDDYN_ENABLE_HELICS_EXECUTABLE)

# ------------------------------------------------------------
# Enable compilation of optimization library?
# ------------------------------------------------------------
if(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY)
    add_subdirectory(optimization)
    target_link_libraries(griddyn_optional INTERFACE optimization)
endif(GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY)

# ------------------------------------------------------------
# Enable compilation of the griddyn shared library?
# ------------------------------------------------------------
if(GRIDDYN_BUILD_C_SHARED_LIBRARY)
    add_subdirectory(griddyn_shared)
endif(GRIDDYN_BUILD_C_SHARED_LIBRARY)

# ------------------------------------------------------------
# Generate libraryLoader.cpp
#
# Do this last, it depends on parameters set in other folder CMakeLists
# ------------------------------------------------------------
add_subdirectory(gridDynLoader)

# Attach the generated loader target after it exists so the dependency propagates correctly through
# fileInput -> runner -> executables.
target_link_libraries(fileInput PUBLIC gridDynLoader)

set(optional_component_test_files ${optional_component_test_files} PARENT_SCOPE)
set(optional_component_test_linkages ${optional_component_test_linkages} PARENT_SCOPE)
set(optional_system_test_files ${optional_system_test_files} PARENT_SCOPE)
set(optional_library_key_headers ${optional_library_key_headers} PARENT_SCOPE)
set(optional_library_functions ${optional_library_functions} PARENT_SCOPE)
