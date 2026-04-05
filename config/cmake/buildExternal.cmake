#build an external cmake project

function(build_external_project target prefix url) # FOLLOWING ARGUMENTS are the CMAKE_ARGS of
                                                   # ExternalProject_Add

    set(trigger_build_dir ${CMAKE_BINARY_DIR}/force_${target})

    # mktemp dir in build tree
    file(MAKE_DIRECTORY ${trigger_build_dir} ${trigger_build_dir}/build)

    # generate false dependency project
    set(CMAKE_LIST_CONTENT
        "
        cmake_minimum_required(VERSION 3.28)

        include(ExternalProject)
        ExternalProject_add(${target}
                PREFIX ${prefix}/${target}
                URL ${url}
                CMAKE_ARGS ${ARGN}
                INSTALL_COMMAND \"\"
                )

        add_custom_target(trigger_${target})
        add_dependencies(trigger_${target} ${target})
    "
    )

    file(WRITE ${trigger_build_dir}/CMakeLists.txt "${CMAKE_LIST_CONTENT}")

    execute_process(COMMAND ${CMAKE_COMMAND} .. WORKING_DIRECTORY ${trigger_build_dir}/build)
    execute_process(COMMAND ${CMAKE_COMMAND} --build . WORKING_DIRECTORY ${trigger_build_dir}/build)

endfunction()
