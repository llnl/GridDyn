# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/tomlplusplus/CMakeLists.txt"
   OR NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/tomlplusplus/include/toml++/toml.hpp"
)
    message(FATAL_ERROR "The tomlplusplus submodule is incomplete. "
                        "Initialize/update ThirdParty/tomlplusplus before building GridDyn."
    )
endif()

# GridDyn uses the header-only parser and owns its install components. Keep the
# pinned submodule unchanged and avoid upstream executables and module targets.
foreach(_griddyn_toml_option IN ITEMS
        tomlplusplus_BUILD_LIB
        tomlplusplus_INSTALL
        tomlplusplus_REGENERATE
        tomlplusplus_BUILD_TESTS
        tomlplusplus_BUILD_TOML_TEST
        tomlplusplus_BUILD_BENCHMARKS
        tomlplusplus_UNRELEASED_FEATURES
        TOMLPLUSPLUS_BUILD_MODULES
)
    set(${_griddyn_toml_option} OFF CACHE BOOL "" FORCE)
endforeach()
unset(_griddyn_toml_option)
set(tomlplusplus_INCLUDES_WITH_SYSTEM ON CACHE BOOL "" FORCE)

add_subdirectory("${PROJECT_SOURCE_DIR}/ThirdParty/tomlplusplus"
                 "${PROJECT_BINARY_DIR}/ThirdParty/tomlplusplus" EXCLUDE_FROM_ALL
)
if(NOT TARGET tomlplusplus::tomlplusplus)
    message(FATAL_ERROR "The tomlplusplus submodule did not create its parser target.")
endif()
set_target_properties(tomlplusplus_tomlplusplus PROPERTIES FOLDER Extern)
griddyn_hide_cache_variables_by_prefix(tomlplusplus_ TOMLPLUSPLUS_)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/ThirdParty/tomlplusplus/include/toml++"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT headers
)
# Preserve the upstream copyright and permission notice for both consumers of
# the installed headers and distributions containing compiled GridDyn programs.
foreach(_griddyn_toml_component IN ITEMS headers applications)
    install(FILES "${PROJECT_SOURCE_DIR}/ThirdParty/tomlplusplus/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/griddyn/licenses"
            RENAME TOMLPLUSPLUS-LICENSE
            COMPONENT ${_griddyn_toml_component}
    )
endforeach()
unset(_griddyn_toml_component)
