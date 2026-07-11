find_package(
    Python 3.10 REQUIRED
    COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule
)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE nanobind_ROOT
    RESULT_VARIABLE nanobind_result
)

if(NOT nanobind_result EQUAL 0)
    message(FATAL_ERROR "Unable to locate nanobind with ${Python_EXECUTABLE} -m nanobind --cmake_dir")
endif()

find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(griddyn_python_core NB_STATIC griddyn_python.cpp)

target_link_libraries(
    griddyn_python_core
    PRIVATE runner griddyn griddyn_base griddyn_optional
)

set_target_properties(
    griddyn_python_core
    PROPERTIES
        OUTPUT_NAME "_core"
        DEBUG_POSTFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}/griddyn"
        FOLDER interfaces/python
)

add_custom_command(
    TARGET griddyn_python_core
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/griddyn/__init__.py"
            "${CMAKE_CURRENT_BINARY_DIR}/griddyn/__init__.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/griddyn/py.typed"
            "${CMAKE_CURRENT_BINARY_DIR}/griddyn/py.typed"
)

install(TARGETS griddyn_python_core LIBRARY DESTINATION griddyn)
install(FILES griddyn/__init__.py griddyn/py.typed DESTINATION griddyn)
