if(DISABLE_SWIG OR NOT SWIG_EXECUTABLE)

    message("Building without swig")

    add_custom_target("griddynPYTHON" ALL)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in ${CMAKE_CURRENT_BINARY_DIR}/setup.py)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/griddynPythonPYTHON_wrap.c
        ${CMAKE_CURRENT_BINARY_DIR}/griddynPythonPYTHON_wrap.c COPYONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/griddyn.py ${CMAKE_CURRENT_BINARY_DIR}/griddyn.py COPYONLY
    )

    add_custom_target("griddynPYTHONEXT" ALL DEPENDS griddyn_shared_lib griddynPYTHON)

    add_custom_command(
        TARGET "griddynPYTHONEXT"
        POST_BUILD
        COMMAND "pip" #
                ARGS "install" "-e" "."
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Building Python extension"
    )
else() # use swig in this branch
    # https://stackoverflow.com/a/3818084/5451769
    if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        # Update if necessary
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-long-long -Wno-overlength-strings")
    endif()

    set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} "-DPy_USING_UNICODE")
    set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} "-noproxydel")

    find_package(PythonLibs REQUIRED)
    include_directories(${PYTHON_INCLUDE_PATH})
    include_directories(${PYTHON_INCLUDE_DIRS})

    set(CMAKE_MACOSX_RPATH 1)
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")

    set_property(SOURCE ../griddyn.i PROPERTY SWIG_MODULE_NAME griddyn)

    if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_GREATER 3.7)
        swig_add_library(
            griddyn
            TYPE MODULE
            LANGUAGE python
            SOURCES griddynPython.i
        )
    else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_GREATER 3.7)
        swig_add_module(griddyn python griddynPython.i)
    endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_GREATER 3.7)

    swig_link_libraries(griddyn griddyn_shared_lib)

    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # https://groups.google.com/a/continuum.io/d/msg/anaconda/057P4uNWyCU/Iem6OtjBCQAJ
        set_target_properties(_griddyn PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
    else()
        swig_link_libraries(griddyn ${PYTHON_LIBRARIES})
    endif()

    set_target_properties(_griddyn PROPERTIES FOLDER interfaces)

    # execute_process(COMMAND python -c "from distutils.sysconfig import get_python_lib; print
    # get_python_lib()" OUTPUT_VARIABLE PYTHON_SITE_PACKAGES OUTPUT_STRIP_TRAILING_WHITESPACE)

    install(TARGETS _griddyn DESTINATION python COMPONENT python)

endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/griddyn.py DESTINATION python COMPONENT python)
install(FILES $<TARGET_FILE:griddyn_shared_lib> DESTINATION python COMPONENT python)
install(FILES ${KEY_LIBRARY_FILES} DESTINATION python COMPONENT python)
