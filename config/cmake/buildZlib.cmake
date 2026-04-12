# This function is used to force a build on a dependent project at cmake configuration phase.
#

function(build_zlib)

    if(CMAKE_BUILD_TYPE)
        list(
            APPEND
            valid_btypes
            "Release"
            "Debug"
            "RelWithDebInfo"
            "MinSizeRel"
        )
        if(${CMAKE_BUILD_TYPE} IN_LIST valid_btypes)
            set(LOCAL_BUILD_TYPE ${CMAKE_BUILD_TYPE})
        else()
            set(LOCAL_BUILD_TYPE "RelWithDebInfo")
        endif()
    else()
        set(LOCAL_BUILD_TYPE "Release")
    endif()

    set(trigger_build_dir ${CMAKE_BINARY_DIR}/autobuild/force_zlib)

    # mktemp dir in build tree
    file(MAKE_DIRECTORY ${trigger_build_dir} ${trigger_build_dir}/build)

    # generate false dependency project
    set(CMAKE_LIST_CONTENT
        "
    cmake_minimum_required(VERSION 3.28)

    include(ExternalProject)
    ExternalProject_Add(zlib
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/ThirdParty/Zlib/zlib-1.3.2
        DOWNLOAD_COMMAND "
        "
        UPDATE_COMMAND "
        "
        BINARY_DIR ${PROJECT_BINARY_DIR}/ThirdParty/Zlib

        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${AUTOBUILD_INSTALL_PATH}
            -DCMAKE_BUILD_TYPE=\$\{CMAKE_BUILD_TYPE\}
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_LINKER=${CMAKE_LINKER}
            -DCMAKE_POSITION_INDEPENDENT_CODE=${CMAKE_POSITION_INDEPENDENT_CODE}

        INSTALL_DIR ${AUTOBUILD_INSTALL_PATH}
    )
"
    )

    file(WRITE ${trigger_build_dir}/CMakeLists.txt "${CMAKE_LIST_CONTENT}")

    if(MSVC)

        if(NOT BUILD_RELEASE_ONLY)
            message(
                STATUS
                    "Configuring zlib Autobuild for Debug: logging to ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_config_debug.log"
            )
            execute_process(
                COMMAND
                    ${CMAKE_COMMAND} -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D
                    CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER} -D
                    CMAKE_BUILD_TYPE=Debug -G ${CMAKE_GENERATOR} -A "${CMAKE_GENERATOR_PLATFORM}" ..
                WORKING_DIRECTORY ${trigger_build_dir}/build
                OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_config_debug.log
            )

            message(
                STATUS
                    "Building zlib Autobuild for debug: logging to ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_build_debug.log"
            )
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build . --config Debug
                WORKING_DIRECTORY ${trigger_build_dir}/build
                OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_build_debug.log
            )
        endif()

        if(NOT BUILD_DEBUG_ONLY)
            if(NOT MSVC_RELEASE_BUILD_TYPE)
                set(MSVC_RELEASE_BUILD_TYPE "Release")
            endif()

            message(
                STATUS
                    "Configuring zlib Autobuild for ${MSVC_RELEASE_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_config_release.log"
            )
            execute_process(
                COMMAND
                    ${CMAKE_COMMAND} -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D
                    CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER} -D
                    CMAKE_BUILD_TYPE=${MSVC_RELEASE_BUILD_TYPE} -G ${CMAKE_GENERATOR} -A
                    "${CMAKE_GENERATOR_PLATFORM}" ..
                WORKING_DIRECTORY ${trigger_build_dir}/build
                OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_config_release.log
            )

            message(
                STATUS
                    "Building zlib Autobuild for ${MSVC_RELEASE_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_build_release.log"
            )
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build . --config ${MSVC_RELEASE_BUILD_TYPE}
                WORKING_DIRECTORY ${trigger_build_dir}/build
                OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_build_release.log
            )
        endif()
    else(MSVC)
        message(
            STATUS
                "Configuring zlib Autobuild for ${LOCAL_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_config.log"
        )
        execute_process(
            COMMAND
                ${CMAKE_COMMAND} -Wno-dev -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -D
                CMAKE_C_COMPILER=${CMAKE_C_COMPILER} -D CMAKE_LINKER=${CMAKE_LINKER} -D
                CMAKE_BUILD_TYPE=${LOCAL_BUILD_TYPE} -G ${CMAKE_GENERATOR} -A
                "${CMAKE_GENERATOR_PLATFORM}" ..
            WORKING_DIRECTORY ${trigger_build_dir}/build
            OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_config.log
        )

        message(
            STATUS
                "Building zlib Autobuild for ${LOCAL_BUILD_TYPE}: logging to ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_build.log"
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build . --config ${LOCAL_BUILD_TYPE}
            WORKING_DIRECTORY ${trigger_build_dir}/build
            OUTPUT_FILE ${PROJECT_BINARY_DIR}/logs/zlib_autobuild_build.log
        )
    endif(MSVC)
endfunction()
