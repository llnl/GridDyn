# Generate SWIG wrapper (for Octave)
message(STATUS "Building Octave interface")

find_package(Octave REQUIRED)
if(NOT MKOCTFILE_EXECUTABLE)
    show_variable(
        OCTAVE_INSTALL_LOCATION PATH "installation location for Octave"
        "${OCTAVE_INSTALL_LOCATION}"
    )
endif()

if(DISABLE_SWIG OR NOT SWIG_EXECUTABLE)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/griddynOCTAVE_wrap.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/griddynOCTAVE_wrap.cpp COPYONLY
    )

else()

    # Compile OCT file add_custom_target("griddynOCT" ALL)
    file(GLOB SHARED_LIB_HEADERS ${CMAKE_SOURCE_DIR}/src/griddyn_shared/*.h)

    # custom command for building the wrap file
    add_custom_command(
        OUTPUT ${CMAKE_CURENT_BINARY_DIR}/griddynOCTAVE_wrap.cxx
        COMMAND
            "${SWIG_EXECUTABLE}" "-octave" "-c++" -o "griddynOCTAVE_wrap.cxx"
            "-I${CMAKE_SOURCE_DIR}/src/griddyn_shared/" ${CMAKE_CURRENT_SOURCE_DIR}/griddynOctave.i
        DEPENDS ${CMAKE_SOURCE_DIR}/swig/griddyn.i ${CMAKE_SOURCE_DIR}/swig/octave/griddynOctave.i
                ${SHARED_LIB_HEADERS}
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/griddynOCTAVE_wrap.cpp
        COMMAND ${CMAKE_COMMAND} -D VOID_SIZE=${CMAKE_SIZEOF_VOID_P} -P
                ${CMAKE_CURRENT_SOURCE_DIR}/modifyOctSourceFile.cmake
        DEPENDS ${PROJECT_CURENT_BINARY_DIR}/griddynOCTAVE_wrap.cxx
    )

endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mkgriddynOctFile.m
    COMMAND
        ${CMAKE_COMMAND} -D LIBRARY_FILE=$<TARGET_LINKER_FILE:griddyn_shared_lib> -D
        BUILD_FILE=${CMAKE_CURRENT_BINARY_DIR}/griddynOCTAVE_wrap.cpp -D
        LIBRARY_INCLUDE_LOCATION=${CMAKE_SOURCE_DIR}/src/griddyn_shared/ -D
        SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/generateOCTcreationScript.cmake
    DEPENDS ${PROJECT_CURRENT_BINARY_DIR}/griddynOCTAVE_wrap.cpp
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/griddyn.oct
    COMMAND ${OCTAVE_EXECUTABLE} "-q" "${CMAKE_CURRENT_BINARY_DIR}/mkgriddynOCTFile.m"
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mkgriddynOCTFile.m griddyn_shared_lib
            ${CMAKE_CURRENT_BINARY_DIR}/griddynOCTAVE_wrap.cpp
)

add_custom_target(griddynOCT ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/griddyn.oct)

set_target_properties(griddynOCT PROPERTIES FOLDER interfaces)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/griddyn.oct DESTINATION octave COMPONENT octave)
install(FILES $<TARGET_FILE:griddyn_shared_lib> DESTINATION octave COMPONENT octave)
install(FILES ${KEY_LIBRARY_FILES} DESTINATION octave COMPONENT octave)
