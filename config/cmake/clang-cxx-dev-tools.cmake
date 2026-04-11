# Additional targets to perform clang-format/clang-tidy
# derived from http://www.labri.fr/perso/fleury/posts/programming/using-clang-tidy-and-clang-format.html
# Get all project files
#src/*.[ch]pp src/*.[ch]xx src/*.cc src/*.hh  src/*.[CHI] src/*.[ch]
file(
    GLOB_RECURSE
    ALL_TEST_FILES
    test/systemTests/*.[ch]pp
    test/libraryTests/*.[ch]pp
    test/componentTests/*.[ch]pp
    test/extraTests/*.[ch]pp
)

file(GLOB ALL_EXCITER_FILES src/griddyn/exciter/*.[ch]pp)
file(GLOB ALL_GOVERNOR_FILES src/griddyn/governor/*.[ch]pp)

file(GLOB ALL_STABILIZER_FILES src/griddyn/stabilizers/*.[ch]pp)

file(GLOB ALL_BLOCK_FILES src/griddyn/blocks/*.[ch]pp)

file(GLOB ALL_CONTROLLER_FILES src/griddyn/controllers/*.[ch]pp)

file(GLOB_RECURSE ALL_GENMODEL_FILES src/griddyn/stabilizers/*.[ch]pp)

set(ALL_SUBMODEL_FILES
    ${ALL_EXCITER_FILES}
    ${ALL_GOVERNOR_FILES}
    ${ALL_STABILIZER_FILES}
    ${ALL_BLOCK_FILES}
    ${ALL_CONTROLLER_FILES}
    ${ALL_GENMODEL_FILES}
)

file(GLOB_RECURSE ALL_CORE_FILES src/core/*.[ch]pp)
file(GLOB ALL_LINK_FILES src/griddyn/Links/*.[ch]pp)
file(GLOB ALL_LOAD_FILES src/griddyn/Loads/*.[ch]pp)
file(GLOB ALL_SOURCE_FILES src/griddyn/sources/*.[ch]pp)

file(GLOB ALL_SOLVER_FILES src/griddyn/solvers/*.[ch]pp)

file(GLOB ALL_BASE_FILES src/griddyn/*.[ch]pp)

file(GLOB ALL_PRIMARY_FILES src/griddyn/primary/*.[ch]pp)

file(GLOB ALL_SIMULATION_FILES src/griddyn/simulation/*.[ch]pp)

file(GLOB ALL_RELAY_FILES src/griddyn/relays/*.[ch]pp)

file(GLOB ALL_GENERATOR_FILES src/griddyn/generators/*.[ch]pp)

file(GLOB ALL_EVENT_FILES src/griddyn/events/*.[ch]pp)
file(GLOB ALL_MEASUREMENT_FILES src/griddyn/measurement/*.[ch]pp)
file(GLOB ALL_COMMS_FILES src/griddyn/comms/*.[ch]pp)
file(GLOB ALL_INPUT_FILES src/griddynFileInput/*.[ch]pp)

file(GLOB ALL_INTERPRETER_FILES src/formatInterpreters/*.[ch]pp)

file(GLOB ALL_UTILITY_FILES src/utilities/*.[ch]pp)

set(INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}/src/griddyn
    ${ROJECT_SOURCE_DIR}/src/utilities
    ${PROJECT_SOURCE_DIR}/src/formatInterpreters
    ${PROJECT_SOURCE_DIR}/src/gridDynFileInput
    ${PROJECT_SOURCE_DIR}/src/gridDynCombined
    ${PROJECT_SOURCE_DIR}/test
    ${griddyn_optional_include_dirs}
    ${SUNDIALS_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/libs/include
    ${PROJECT_SOURCE_DIR}/ThirdParty
    ${Boost_INCLUDE_DIR}
)

set(INCLUDES "")
foreach(f ${INCLUDE_DIRECTORIES})
    list(APPEND INCLUDES "-I${f}")
endforeach(f)

# Adding clang-format target if executable is found
find_program(CLANG_FORMAT "clang-format")
if(CLANG_FORMAT)
    add_custom_target(clang-format-test COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_TEST_FILES})

    add_custom_target(
        clang-format-submodels COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_SUBMODEL_FILES}
    )

    add_custom_target(clang-format-relays COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_RELAY_FILES})

    add_custom_target(
        clang-format-core COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_CORE_FILES}
                                  ${ALL_BASE_FILES}
    )

    add_custom_target(
        clang-format-primary COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_PRIMARY_FILES}
                                     ${ALL_SIMULATION_FILES}
    )

    add_custom_target(
        clang-format-utility COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_UTILITY_FILES}
    )

    add_custom_target(
        clang-format-secondary COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_LOAD_FILES}
                                       ${ALL_LINK_FILES} ${ALL_SOURCE_FILES} ${ALL_GENERATOR_FILES}
    )

    add_custom_target(
        clang-format-helpers
        COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_COMMS_FILES} ${ALL_MEASUREMENT_FILES}
                ${ALL_EVENTS_FILES} ${ALL_SOLVER_FILES}
    )

    add_custom_target(
        clang-format-inputs COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_INPUT_FILES}
                                    ${ALL_INTERPRETER_FILES}
    )
endif()

# Adding clang-tidy target if executable is found
find_program(CLANG_TIDY "clang-tidy")
if(CLANG_TIDY)
    add_custom_target(
        clang-tidy-test COMMAND ${CLANG_TIDY} ${ALL_TEST_FILES} -config='' -- -std=c++14
                                ${INCLUDES}
    )

    add_custom_target(
        clang-tidy-secondary
        COMMAND ${CLANG_TIDY} ${ALL_LINK_FILES} ${ALL_LOAD_FILES} ${ALL_SOURCE_FILES}
                ${ALL_GENERATOR_FILES} -config='' -- -std=c++14 ${INCLUDES}
    )

    add_custom_target(
        clang-tidy-relay COMMAND ${CLANG_TIDY} ${ALL_RELAY_FILES} -config='' -- -std=c++14
                                 ${INCLUDES}
    )

    add_custom_target(
        clang-tidy-primary COMMAND ${CLANG_TIDY} ${ALL_PRIMARY_FILES} ${ALL_SIMULATION_FILES}
                                   -config='' -- -std=c++14 ${INCLUDES}
    )
    add_custom_target(
        clang-tidy-utility COMMAND ${CLANG_TIDY} ${ALL_UTILITY_FILES} -config='' -- -std=c++14
                                   ${INCLUDES}
    )

    add_custom_target(
        clang-tidy-core COMMAND ${CLANG_TIDY} ${ALL_CORE_FILES} ${ALL_BASE_FILES} -config='' --
                                -std=c++14 ${INCLUDES}
    )
    add_custom_target(
        clang-tidy-submodels COMMAND ${CLANG_TIDY} ${ALL_SUBMODEL_FILES} -config='' -- -std=c++14
                                     ${INCLUDES}
    )

    add_custom_target(
        clang-tidy-inputs COMMAND ${CLANG_TIDY} ${ALL_INPUT_FILES} ${ALL_INTERPRETER_FILES}
                                  -config='' -- -std=c++14 ${INCLUDES}
    )

    add_custom_target(
        clang-tidy-helpers
        COMMAND ${CLANG_TIDY} ${ALL_COMMS_FILES} ${ALL_MEASUREMENT_FILES} ${ALL_EVENTS_FILES}
                ${ALL_SOLVER_FILES} -config='' -- -std=c++14 ${INCLUDES}
    )
endif()
