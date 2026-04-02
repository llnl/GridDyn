# This function is used to force a build on a dependent project at cmake configuration phase.
#

function (build_yaml)

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

    set(trigger_build_dir ${CMAKE_BINARY_DIR}/autobuild/force_yaml)

    #mktemp dir in build tree
    file(MAKE_DIRECTORY ${trigger_build_dir} ${trigger_build_dir}/build)

    #generate false dependency project
    set(CMAKE_LIST_CONTENT "
    cmake_minimum_required(VERSION 3.28)
    include(ExternalProject)
ExternalProject_Add(yaml-cpp
    SOURCE_DIR ${PROJECT_BINARY_DIR}/Download/yaml-cpp
    GIT_REPOSITORY  https://github.com/jbeder/yaml-cpp.git
    DOWNLOAD_COMMAND " "
    UPDATE_COMMAND " "
    BINARY_DIR ${PROJECT_BINARY_DIR}/ThirdParty/yaml-cpp

    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${AUTOBUILD_INSTALL_PATH}
        -DCMAKE_BUILD_TYPE=\$\{CMAKE_BUILD_TYPE\}
        -DYAML_CPP_BUILD_TOOLS=OFF
        -DYAML_CPP_BUILD_TESTS=OFF
        -DYAML_CPP_BUILD_CONTRIB=OFF
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_LINKER=${CMAKE_LINKER}
        -DINSTALL_CMAKE_DIR=${PROJECT_BINARY_DIR}/cmake

    INSTALL_DIR ${AUTOBUILD_INSTALL_PATH}
    )")



    file(WRITE ${trigger_build_dir}/CMakeLists.txt "${CMAKE_LIST_CONTENT}")
    if (MSVC)
  if (NOT BUILD_DEBUG_ONLY)
  if (NOT MSVC_RELEASE_BUILD_TYPE)
        set(MSVC_RELEASE_BUILD_TYPE "Release")
    endif()

    execute_process(COMMAND ${CMAKE_COMMAND} -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER}
        -D CMAKE_BUILD_TYPE=${MSVC_RELEASE_BUILD_TYPE} -G ${CMAKE_GENERATOR} ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/yaml_autobuild_config_release.log
        )
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${MSVC_RELEASE_BUILD_TYPE}
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/yaml_autobuild_build_release.log
        )
      endif()

  if (NOT BUILD_RELEASE_ONLY)
 execute_process(COMMAND ${CMAKE_COMMAND} -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER}
        -D CMAKE_BUILD_TYPE=Debug -G ${CMAKE_GENERATOR} ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/yaml_autobuild_config_debug.log
        )
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Debug
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/yaml_autobuild_build_debug.log
        )
endif()
else(MSVC)
execute_process(COMMAND ${CMAKE_COMMAND} -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER}
        -D CMAKE_BUILD_TYPE=${LOCAL_BUILD_TYPE} -G ${CMAKE_GENERATOR} ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/yaml_autobuild_config.log
        )
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${LOCAL_BUILD_TYPE}
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/yaml_autobuild_build.log
        )
endif(MSVC)
endfunction()
