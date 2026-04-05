# addLoadLibraryCommand.cmake
#adds a load library function to the JNIgriddyn.java

if(NOT LIBRARY_FILE)
    set(LIBRARY_FILE JNIgriddyn)
else()
    get_filename_component(LIBRARY_FILE ${LIBRARY_FILE} NAME_WE)
    string(REGEX REPLACE "^lib" "" LIBRARY_FILE ${LIBRARY_FILE})
endif()
file(READ griddynJNI.java GRIDDYN_JNI_SOURCE)
string(FIND "${GRIDDYN_JNI_SOURCE}" "System.loadLibrary" ALREADY_LOADED)
if(${ALREADY_LOADED} LESS 0)
    string(
        REPLACE
            "public class griddynJNI {"
            "public class griddynJNI {\n  static {\n    System.loadLibrary\(\"${LIBRARY_FILE}\"\);\n  }"
            GRIDDYN_JNI_SOURCE
            "${GRIDDYN_JNI_SOURCE}"
    )

    file(WRITE griddynJNI.java "${GRIDDYN_JNI_SOURCE}")
endif()
