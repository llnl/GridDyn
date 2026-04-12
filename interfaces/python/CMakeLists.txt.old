cmake_minimum_required(VERSION 3.4)

FIND_PACKAGE(PythonLibs REQUIRED)
FIND_PACKAGE(PythonInterp REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PYTHON_INCLUDE_PATH})
include_directories(${PYTHON_INCLUDE_DIRS})
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src/gridDynCombined)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src/griddyn_shared)
link_directories(${external_link_directories})

make_directory(${PROJECT_BINARY_DIR}/python/griddyn)

set(PYTHONFLAG "")
set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} "-DPy_USING_UNICODE")
set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} "-noproxydel")

set(CMAKE_SWIG_OUTDIR ${CMAKE_CURRENT_SOURCE_DIR}/target${PYTHONFLAG}/extra)
set(CMAKE_SWIG_WRAP_OUTDIR ${CMAKE_CURRENT_SOURCE_DIR}/target${PYTHONFLAG}/source/dummy)

set_property(SOURCE ../griddyn.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE ../griddyn.i  PROPERTY SWIG_MODULE_NAME griddyn)
# Generate SWIG wrapper
# Add swig module
swig_module_initialize(griddyn python)
message("Python libraries = ${PYTHON_LIBRARIES}")
message("Python = ${PYTHON_EXECUTABLE}")
message("griddynDynCombined=" gridDynCombined)
message("griddyn_shared_lib=" griddyn_shared_lib)

swig_add_source_to_module(griddyn FALSE ../griddyn.i ../../src/griddyn_shared/griddyn_export.h ../../src/griddyn_shared/griddyn_export_advanced.h)
swig_add_library(griddyn LANGUAGE python SOURCES ../griddyn.i)
swig_link_libraries(griddyn ${PYTHON_LIBRARIES} griddyn_shared_lib)
get_directory_property(swig_extra_clean_files ADDITIONAL_MAKE_CLEAN_FILES)
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${PYTHON_FILE}")

set(SETUP_PY_IN ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in)
set(SETUP_PY_OUT ${CMAKE_CURRENT_BINARY_DIR}/setup.py)
set(DEPS         ${CMAKE_CURRENT_SOURCE_DIR}/__init__.py)
set(OUTPUT       ${CMAKE_CURRENT_BINARY_DIR}/pythonlog)
set(MODULE       ${CMAKE_CURRENT_BINARY_DIR}/griddyn)

configure_file(${SETUP_PY_IN} ${SETUP_PY_OUT})
file(MAKE_DIRECTORY ${MODULE})
file(COPY ${DEPS} DESTINATION ${MODULE})

add_custom_command(OUTPUT ${OUTPUT} POST_BUILD
                   COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY_OUT} build
                   COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
                  )

add_custom_target(make_gen  DEPENDS ${OUTPUT})
target_link_libraries(_griddyn griddyn_shared_lib)

install(FILES  ${CMAKE_CURRENT_BINARY_DIR}/_griddyn.so DESTINATION  ${MODULE})
install(FILES  ${CMAKE_SWIG_OUTDIR}/griddyn.py DESTINATION  ${MODULE})
install(CODE "execute_process(COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY_OUT} install)")
