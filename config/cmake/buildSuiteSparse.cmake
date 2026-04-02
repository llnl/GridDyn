# This function is used to force a build on a dependent project at cmake configuration phase.
#

function (build_suitesparse install_path)
if (CMAKE_BUILD_TYPE)
    list(APPEND valid_btypes "Release" "Debug" "RelWithDebInfo" "MinSizeRel")
    if (${CMAKE_BUILD_TYPE} IN_LIST valid_btypes)
        set(LOCAL_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    else()
        set(LOCAL_BUILD_TYPE "RelWithDebInfo")
    endif()
else ()
    set(LOCAL_BUILD_TYPE "Release")
endif()
    include(escape_string)

    escape_string(cxx_compiler_string ${CMAKE_CXX_COMPILER})
    escape_string(c_compiler_string ${CMAKE_C_COMPILER})
    escape_string(linker_string ${CMAKE_LINKER})

    set(trigger_build_dir ${CMAKE_BINARY_DIR}/autobuild/force_suitesparse)

    #mktemp dir in build tree
    file(MAKE_DIRECTORY ${trigger_build_dir} ${trigger_build_dir}/build)

    #generate false dependency project
    set(CMAKE_LIST_CONTENT "
    cmake_minimum_required(VERSION 3.28)
    include(ExternalProject)
ExternalProject_Add(suitesparse
    SOURCE_DIR ${PROJECT_BINARY_DIR}/Download/suitesparse
    GIT_REPOSITORY  https://github.com/jlblancoc/suitesparse-metis-for-windows.git
    UPDATE_COMMAND " "
    BINARY_DIR ${PROJECT_BINARY_DIR}/ThirdParty/suitesparse
    INSTALL_DIR ${install_path}

    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${install_path}
        -DSUITESPARSE_INSTALL_PREFIX=${install_path}
        -DCMAKE_BUILD_TYPE=\$\{CMAKE_BUILD_TYPE\}
        -DCMAKE_CXX_COMPILER=${cxx_compiler_string}
        -DCMAKE_C_COMPILER=${c_compiler_string}
        -DLAPACK_DIR=${PROJECT_BINARY_DIR}/Download/suitesparse/lapack_windows/x64
        -DBUILD_SHARED_LIBS=${USE_KLU_SHARED}
        -DCMAKE_POSITION_INDEPENDENT_CODE=${CMAKE_POSITION_INDEPENDENT_CODE}
        \"-DCMAKE_C_FLAGS=${EXTRA_C_FLAGS}\"
        -DCMAKE_LINKER=${linker_string}
        -DBUILD_METIS=OFF
    )")


    file(WRITE ${trigger_build_dir}/CMakeLists.txt "${CMAKE_LIST_CONTENT}")
if (MSVC)
  if (NOT BUILD_DEBUG_ONLY)
  if (NOT MSVC_RELEASE_BUILD_TYPE)
        set(MSVC_RELEASE_BUILD_TYPE "Release")
    endif()
    message(STATUS "Configuring SuiteSparse Autobuild for ${MSVC_RELEASE_BUILD_TYPE} logging to ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_config_release.log")
    execute_process(COMMAND ${CMAKE_COMMAND}  -Wno-dev -D CMAKE_CXX_COMPILER=${cxx_compiler_string} -D CMAKE_C_COMPILER=${c_compiler_string}
        -D CMAKE_LINKER=${linker_string}
        -D CMAKE_BUILD_TYPE=${MSVC_RELEASE_BUILD_TYPE} -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_config_release.log
        )

    message(STATUS "Building SuiteSparse ${MSVC_RELEASE_BUILD_TYPE} build logging to ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_build_release.log")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${MSVC_RELEASE_BUILD_TYPE}
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_build_release.log
        )
endif()

if (NOT BUILD_RELEASE_ONLY)
    message(STATUS "Configuring SuiteSparse Autobuild for debug logging to ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_config_debug.log")
    execute_process(COMMAND ${CMAKE_COMMAND}  -Wno-dev -D CMAKE_CXX_COMPILER=${cxx_compiler_string} -D CMAKE_C_COMPILER=${c_compiler_string}
        -D CMAKE_LINKER=${linker_string}
        -D CMAKE_BUILD_TYPE=Debug -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_config_debug.log
        )

    message(STATUS "Building SuiteSparse debug build logging to ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_build_debug.log")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Debug
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_build_debug.log
        )
endif()

else(MSVC)
message(STATUS "Configuring SuiteSparse Autobuild for ${LOCAL_BUILD_TYPE} logging to ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_config.log")
    execute_process(COMMAND ${CMAKE_COMMAND}  -Wno-dev -D CMAKE_CXX_COMPILER=${cxx_compiler_string} -D CMAKE_C_COMPILER=${c_compiler_string}
        -D CMAKE_LINKER=${linker_string}
        -D CMAKE_BUILD_TYPE=${LOCAL_BUILD_TYPE} -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_config.log
        )

    message(STATUS "Building SuiteSparse ${LOCAL_BUILD_TYPE} build logging to ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_build.log")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${LOCAL_BUILD_TYPE}
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/suitesparse_autobuild_build.log
        )
endif(MSVC)
endfunction()
