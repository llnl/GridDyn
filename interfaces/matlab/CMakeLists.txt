# Generate SWIG wrapper (for both MATLAB)

set(Matlab_FIND_COMPONENTS "MAIN_PROGRAM")
find_package(MATLAB)

if(NOT DISABLE_SWIG AND SWIG_EXECUTABLE)

    execute_process(COMMAND ${SWIG_EXECUTABLE} -help OUTPUT_VARIABLE SWIG_HELP_OUTPUT)
    string(FIND "${SWIG_HELP_OUTPUT}" "-matlab" MATLAB_HELP_FOUND)
    if(${MATLAB_HELP_FOUND} LESS 0)
        set(MATLAB_SWIG_NOT_AVAILABLE 1)
        message(WARNING " SWIG VERSION does not support matlab, reverting to build only")
    endif()
endif()

message(STATUS "Building MATLAB")
if(DISABLE_SWIG OR MATLAB_SWIG_NOT_AVAILABLE)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp
        COMMAND "${CMAKE_COMMAND}" -E copy ${CMAKE_CURRENT_SOURCE_DIR}/target/griddynMEX.cpp
                ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/target/+griddyn DESTINATION matlab
            COMPONENT matlab
    )

    file(GLOB MATLAB_FILES ${CMAKE_CURRENT_SOURCE_DIR}/target/*.m)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/target/+griddyn DESTINATION matlab
            COMPONENT matlab
    )
    install(FILES ${MATLAB_FILES} DESTINATION matlab COMPONENT matlab)
else()

    file(GLOB SHARED_LIB_HEADERS ${CMAKE_SOURCE_DIR}/src/shared_library/*.h)

    # custom command for building the wrap file
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp
        COMMAND
            "${SWIG_EXECUTABLE}" "-matlab" "-c++" -o "griddynMEX.cpp"
            "-I${CMAKE_SOURCE_DIR}/src/griddyn_shared" ${CMAKE_CURRENT_SOURCE_DIR}/griddynMatlab.i
        DEPENDS ${CMAKE_SOURCE_DIR}/swig/griddyn.i ${CMAKE_CURRENT_SOURCE_DIR}/griddynMatlab.i
                ${SHARED_LIB_HEADERS}
    )

    # ADD_CUSTOM_COMMAND(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp COMMAND ${CMAKE_COMMAND}
    # -P ${CMAKE_CURRENT_SOURCE_DIR}/modifyOctSourceFile.cmake DEPENDS
    # ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp )

    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/+griddyn DESTINATION matlab COMPONENT matlab)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SwigGet.m ${CMAKE_CURRENT_BINARY_DIR}/SwigMem.m
                  ${CMAKE_CURRENT_BINARY_DIR}/SwigRef.m DESTINATION matlab COMPONENT matlab
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/GetFullPath.m DESTINATION matlab COMPONENT matlab)
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mkgriddynMEXFile.m
    COMMAND
        ${CMAKE_COMMAND} -D LIBRARY_FILE=$<TARGET_LINKER_FILE:griddyn_shared_lib> -D
        BUILD_FILE=${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp -D
        LIBRARY_INCLUDE_LOCATION=${CMAKE_SOURCE_DIR}/src/griddyn_shared/ -D
        SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/generateMEXcreationScript.cmake
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.${Matlab_MEX_EXTENSION}
    COMMAND ${Matlab_MAIN_PROGRAM} -nojvm -nodesktop -r
            "\"run('${CMAKE_CURRENT_BINARY_DIR}/mkgriddynMEXFile.m');quit;\""
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mkgriddynMEXFile.m griddyn_shared_lib
            ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.cpp
)

add_custom_target(
    griddynMEX ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.${Matlab_MEX_EXTENSION}
)

set_target_properties(griddynMEX PROPERTIES FOLDER interfaces)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/griddynMEX.${Matlab_MEX_EXTENSION} DESTINATION matlab
        COMPONENT matlab
)
install(FILES $<TARGET_FILE:griddyn_shared_lib> DESTINATION matlab COMPONENT matlab)
install(FILES ${KEY_LIBRARY_FILES} DESTINATION matlab COMPONENT matlab)
