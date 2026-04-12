find_package(Java REQUIRED)
find_package(JNI REQUIRED)
include(UseJava)

include_directories(${JAVA_INCLUDE_PATH})
include_directories(${JNI_INCLUDE_DIRS})
if(DISABLE_SWIG OR NOT SWIG_EXECUTABLE)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/interface/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    add_library(JNIgriddyn SHARED interface/griddynJavaJAVA_wrap.c)
    target_link_libraries(JNIgriddyn griddynSharedLib ${JAVA_LIBRARIES})
    set_target_properties(JNIgriddyn PROPERTIES FOLDER interfaces)
else(DISABLE_SWIG)
    # Enable generation using swig
    include(UseSWIG)
    set_property(SOURCE griddynJava.i PROPERTY SWIG_MODULE_NAME JNIgriddyn)

    set(CMAKE_SWIG_FLAGS "-package;com.java.griddyn")
    if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_GREATER 3.7)
        swig_add_library(
            JNIgriddyn
            TYPE MODULE
            LANGUAGE java
            SOURCES griddynJava.i
        )
    else()
        swig_add_module(JNIgriddyn java griddynJava.i)
    endif()

    swig_link_libraries(JNIgriddyn griddyn_shared_lib)
    swig_link_libraries(JNIgriddyn ${JAVA_LIBRARIES})

    set_target_properties(JNIgriddyn PROPERTIES FOLDER interfaces)

    if(APPLE)

        # SET_TARGET_PROPERTIES(griddynSharedLib PROPERTIES MACOSX_RPATH TRUE)
        # SET_TARGET_PROPERTIES(griddynSharedLib PROPERTIES CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

        # SET_TARGET_PROPERTIES(_griddyn PROPERTIES MACOSX_RPATH TRUE)
        # SET_TARGET_PROPERTIES(_griddyn PROPERTIES CMAKE_INSTALL_RPATH_USE_LINK_PATH
        # SET_TARGET_PROPERTIES(griddyn PROPERTIES INSTALL_RPATH "@loader_path/../../..")

    else()

        # SET_TARGET_PROPERTIES(_griddyn PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
        # SET_TARGET_PROPERTIES(_griddyn PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

    endif()

endif() # DISABLE_SWIG

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/MakeJarCMakeLists.txt.in ${CMAKE_CURRENT_BINARY_DIR}/CMakeLists.txt
    @ONLY
)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/buildjar/)

# -D ADDITIONAL_JAR_FILES=$<TARGET_FILE:griddyn_shared_lib>;$<TARGET_FILE:JNIgriddyn>
add_custom_command(
    TARGET "JNIgriddyn" POST_BUILD
    COMMAND ${CMAKE_COMMAND} -D LIBRARY_FILE=$<TARGET_FILE:JNIgriddyn> -P
            ${CMAKE_CURRENT_SOURCE_DIR}/addLoadLibraryCommand.cmake
)

add_custom_command(
    TARGET "JNIgriddyn"
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} ARGS ..
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/buildjar"
    VERBATIM
)

add_custom_command(
    TARGET "JNIgriddyn"
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} ARGS --build . --target griddyn
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/buildjar/"
    COMMENT "Building jar file"
    VERBATIM
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/buildjar/griddyn.jar DESTINATION java COMPONENT java)
install(FILES $<TARGET_FILE:griddyn_shared_lib> DESTINATION java COMPONENT java)
install(TARGETS JNIgriddyn DESTINATION java COMPONENT java)
install(FILES ${KEY_LIBRARY_FILES} DESTINATION java COMPONENT java)
