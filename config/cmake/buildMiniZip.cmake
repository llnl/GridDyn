# This function is used to force a build on a dependent project at cmake configuration phase.
#

function (build_minizip)

include(escape_string)

    escape_string(cxx_compiler_string ${CMAKE_CXX_COMPILER})
    escape_string(c_compiler_string ${CMAKE_C_COMPILER})
    escape_string(linker_string ${CMAKE_LINKER})

    escape_string(zlib_includes_string ${ZLIB_ROOT_DIR})

    escape_string(binary_dir_string ${AUTOBUILD_INSTALL_PATH})

    escape_string(project_src_dir_string ${PROJECT_SOURCE_DIR})

    set(trigger_build_dir ${binary_dir_string}/autobuild/force_minizip)

    #mktemp dir in build tree
    file(MAKE_DIRECTORY ${trigger_build_dir} ${trigger_build_dir}/build)

    #generate false dependency project
    set(CMAKE_LIST_CONTENT "
    cmake_minimum_required(VERSION 3.28)
    include(ExternalProject)
ExternalProject_Add(minizip
    SOURCE_DIR ${project_src_dir_string}/ThirdParty/Minizip
    DOWNLOAD_COMMAND " "
    UPDATE_COMMAND " "
    BINARY_DIR ${binary_dir_string}/ThirdParty/Minizip

    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${binary_dir_string}
        -DCMAKE_BUILD_TYPE=\$\{CMAKE_BUILD_TYPE\}
        -DCMAKE_MODULE_PATH=${project_src_dir_string}/config/cmake
        -DZLIB_LOCATION=${zlib_includes_string}
        -DCMAKE_C_COMPILER=${c_compiler_string}
        -DCMAKE_LINKER=${linker_string}
        -DCMAKE_POSITION_INDEPENDENT_CODE=${CMAKE_POSITION_INDEPENDENT_CODE}

    INSTALL_DIR ${binary_dir_string}
    )")


    file(WRITE ${trigger_build_dir}/CMakeLists.txt "${CMAKE_LIST_CONTENT}")

message(STATUS ${trigger_build_dir})

#execute_process(COMMAND ${CMAKE_COMMAND} WORKING_DIRECTORY ${trigger_build_dir}/build)
if (MSVC)

if (NOT BUILD_RELEASE_ONLY)
message(STATUS "Configuring Minizip Autobuild for debug: logging to ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_config_debug.log")
    execute_process(COMMAND ${CMAKE_COMMAND}  -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER}
        -D CMAKE_BUILD_TYPE=Debug -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_config_debug.log
        )

    message(STATUS "Building minizip Autobuild for debug: logging to ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_build_debug.log")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Debug
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_build_debug.log
        )

endif()

  if (NOT BUILD_DEBUG_ONLY)

   if (NOT MSVC_RELEASE_BUILD_TYPE)
        set(MSVC_RELEASE_BUILD_TYPE "Release")
    endif()

message(STATUS "Configuring Minizip Autobuild for ${MSVC_RELEASE_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_config_release.log")
    execute_process(COMMAND ${CMAKE_COMMAND}  -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER}
        -D CMAKE_BUILD_TYPE=${MSVC_RELEASE_BUILD_TYPE} -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_config_release.log
        )

    message(STATUS "Building minizip Autobuild for ${MSVC_RELEASE_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_build_release.log")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${MSVC_RELEASE_BUILD_TYPE}
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_build_release.log
        )
endif()
else(MSVC)

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

    message(STATUS "Configuring Minizip Autobuild for ${LOCAL_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_config.log")
    execute_process(COMMAND ${CMAKE_COMMAND}  -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER}
        -D CMAKE_BUILD_TYPE=${LOCAL_BUILD_TYPE} -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_config.log
        )

    message(STATUS "Building minizip Autobuild for ${CLOCAL_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_build.log")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${LOCAL_BUILD_TYPE}
        WORKING_DIRECTORY ${trigger_build_dir}/build
        OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/minizip_autobuild_build.log
        )

endif(MSVC)
endfunction()
