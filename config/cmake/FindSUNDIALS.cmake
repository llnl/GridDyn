# CMake file to locate SUNDIALS and its useful composite projects
# The first development of this file was made from Windows users who
# use:
#
#
# Inputs variables this file can process (variable must be given before find_package(SUITESPARES ...) command) :
#   * SUNDIALS_VERBOSE            Default to OFF
#
#
#
# Help variables this file handle internally :
#   * SUNDIALS_SEARCH_LIB_POSTFIX        Is set in cache (as advanced) to look into the right lib/lib64 dir for libraries (user can change)
#
#
# Variables this file provide :
#   * SUNDIALS_FOUND                     True if SUNDIALS given COMPONENTS include and libraries were found
#   * SUNDIALS_INCLUDE_DIRS              Paths containing SUNDIALS needed headers (depend on which COMPONENTS you gave)
#   * SUNDIALS_LIBRARIES                 Absolute paths of SUNDIALS libs found (depend on which COMPONENTS you gave)
#
#
# Detailed variables this file provide :
#   * SUNDIALS_<UPPPER_CASE_COMPONENT>_FOUND        True if the given component to look for is found (INCLUDE DIR and LIBRARY)
#   * SUNDIALS_<UPPPER_CASE_COMPONENT>_INCLUDE_DIR    The path directory where we can found all component header files
#   * SUNDIALS_<UPPPER_CASE_COMPONENT>_LIBRARY        The file path to the component library
#   Note: If a component is not found, a SUNDIALS_<UPPPER_CASE_COMPONENT>_DIR cache variable is set to allow user set the search directory.
#
#
# Possible components to find are (maybe some others can be available):
#   * IDA
#   * IDAS
#   * CVODE
#   * CVODES
#   * KINSOL
#   * ARKODE
#   * NVECOPENMP
#   * NVECSERIAL
#
# How to use this file :
#   (opt) set(SUNDIALS_VERBOSE ON)
#   ( 1 ) find_package(SUNDIALS) ## searches for ida and kinsol and the base required components
#   ( 2 ) find_package(SUNDIALS COMPONENTS metis CHOLMOD)         ## be careful, components are case sensitive
#   ( 3 ) find_package(SUNDIALS COMPONENTS metis sundials)    ## valid on windows (linux have no sundials library)
#   ( 4 ) find_package(SUNDIALS COMPONENTS sundials)
#
#    if(SUNDIALS_FOUND)
#       include_directories(${SUNDIALS_INCLUDE_DIRS})
#         target_link_library(<myProject> ${SUNDIALS_LIBRARIES})
#      endif()
#
# Created by jesnault (jerome.esnault@inria.fr) 2014-01-15
# Updated by jesnault (jerome.esnault@inria.fr) 2014-01-21
# Licensed under 3-Claused BSD License. See https://github.com/jlblancoc/suitesparse-metis-for-windows/blob/master/LICENSE.md
# File modified for use with SUNDIALS based on the sundials version by Philip Top 2017-01-06

# check if global root SUNDIALS folder is set or not and cache it in order to let user fill it
if(NOT SUNDIALS_DIR)
    set(SUNDIALS_DIR "$ENV{SUNDIALS_DIR}" CACHE PATH "SUNDIALS root directory")
endif()
if(SUNDIALS_DIR)
    file(TO_CMAKE_PATH ${SUNDIALS_DIR} SUNDIALS_DIR)
endif()

# set default verbosity Process the CMake automatically-generated var: SUNDIALS_FIND_QUIETLY:
# supersedes *_VERBOSE.
if(NOT SUNDIALS_VERBOSE OR SUNDIALS_FIND_QUIETLY)
    set(SUNDIALS_VERBOSE OFF)
endif()

if(SUNDIALS_VERBOSE)
    message(STATUS "Start to FindSUNDIALS.cmake :")
endif()

# set the LIB POSTFIX to find in a right directory according to what kind of compiler we use
# (32/64bits)
if(CMAKE_SIZEOF_VOID_P EQUAL 8) # Size in bytes!
    set(SUNDIALS_SEARCH_LIB_POSTFIX "64" CACHE STRING "suffix for 32/64 dir placement")
else() # Size in bytes!
    set(SUNDIALS_SEARCH_LIB_POSTFIX "" CACHE STRING "suffix for 32/64 dir placement")
endif()
if(SUNDIALS_SEARCH_LIB_POSTFIX)
    mark_as_advanced(SUNDIALS_SEARCH_LIB_POSTFIX)
    if(SUNDIALS_VERBOSE)
        message(
            STATUS
                "   find_library will search inside lib${SUNDIALS_SEARCH_LIB_POSTFIX} directory (can be changed with SUNDIALS_SEARCH_LIB_POSTFIX)"
        )
    endif()
endif()

# This utility macro is used to find all sundials projects by giving its name Since the name
# structure is the same for lib name and include dir name, we can use a generic way to find all of
# these with simple cmake lines of code
macro(SUNDIALS_FIND_COMPONENTS)

    # Look for each component the same way : * For include dir the reference file is the
    # <component>.h * for library fileName the reference is the <component> itself (cmake will
    # prepend/append necessary prefix/suffix according to the plateform)
    foreach(sundialsComp ${SUNDIALS_FIND_COMPONENTS})

        # used to construct specific cmake variables (in upper case) according to the component, but
        # also used for find_*()
        string(TOUPPER ${sundialsComp} sundialsCompUC)
        string(TOLOWER ${sundialsComp} sundialsCompLC)

        # try to find include dir (looking for very important header file)
        if(NOT ${sundialsCompUC} STREQUAL "NVECOPENMP" AND NOT ${sundialsCompUC} STREQUAL
                                                           "NVECSERIAL"
        )
            find_path(
                SUNDIALS_${sundialsCompUC}_INCLUDE_DIR
                NAMES ${sundialsComp}.h
                      ${sundialsCompLC}.h
                      ${sundialsCompUC}.h
                      sundials_${sundialsComp}.h
                      sundials_${sundialsCompLC}.h
                      SUNDIALS_${sundialsCompUC}.h
                      ${sundialsComp}/${sundialsComp}.h
                      ${sundialsCompLC}/${sundialsCompLC}.h
                      ${sundialsCompUC}/${sundialsCompUC}.h
                      sundials/sundials_${sundialsComp}.h
                      sundials/sundials_${sundialsCompLC}.h
                      sundials/SUNDIALS_${sundialsCompUC}.h
                HINTS ${SUNDIALS_DIR}/include
                      ${SUNDIALS_DIR}/include/sundials
                      ${SUNDIALS_DIR}/sundials/include
                      ${SUNDIALS_DIR}/include/${sundialsComp}
                      ${SUNDIALS_DIR}/${sundialsComp}/include
                      ${${sundialsCompUC}_DIR}/include
                      ${${sundialsCompUC}_DIR}/${sundialsComp}/include
                      ${${sundialsCompUC}_DIR}
                PATHS /opt/local/include
                      /usr/include
                      /usr/local/include
                      /usr/include/sundials
                      /usr/local/include/sundials
                      /usr/include/${sundialsComp}
                      /usr/local/include/${sundialsComp}
            )
        else()
            find_path(
                SUNDIALS_${sundialsCompUC}_INCLUDE_DIR
                NAMES nvector_serial.h nvector_openmp.h nvector/nvector_serial.h
                      nvector/nvector_openmp.h
                HINTS ${SUNDIALS_DIR}/include
                      ${SUNDIALS_DIR}/include/sundials
                      ${SUNDIALS_DIR}/sundials/include
                      ${SUNDIALS_DIR}/include/${sundialsComp}
                      ${SUNDIALS_DIR}/${sundialsComp}/include
                      ${${sundialsCompUC}_DIR}/include
                      ${${sundialsCompUC}_DIR}/${sundialsComp}/include
                      ${${sundialsCompUC}_DIR}
                PATHS /opt/local/include
                      /usr/include
                      /usr/local/include
                      /usr/include/sundials
                      /usr/local/include/sundials
                      /usr/include/${sundialsComp}
                      /usr/local/include/${sundialsComp}
            )
        endif()
        # check if found
        if(NOT SUNDIALS_${sundialsCompUC}_INCLUDE_DIR)
            if(SUNDIALS_VERBOSE)
                message(
                    WARNING
                        "   Failed to find ${sundialsComp} :\nSUNDIALS_${sundialsCompUC}_INCLUDE_DIR not found.\nCheck you write correctly the component name (case sensitive),\nor set the SUNDIALS_${sundialsCompUC}_DIR to look inside"
                )
            endif()
        else()
            list(APPEND SUNDIALS_INCLUDE_DIRS ${SUNDIALS_${sundialsCompUC}_INCLUDE_DIR})
        endif()

        # try to find filepath lib name (looking for very important lib file)
        find_library(
            SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE
            NAMES lib${sundialsComp}
                  lib${sundialsCompLC}
                  lib${sundialsCompUC}
                  ${sundialsComp}
                  ${sundialsCompLC}
                  ${sundialsCompUC}
                  sundials_${sundialsComp}
                  sundials_${sundialsCompLC}
                  SUNDIALS_${sundialsCompUC}
            HINTS ${SUNDIALS_DIR}/lib${SUNDIALS_SEARCH_LIB_POSTFIX} ${SUNDIALS_DIR}/lib
                  ${${sundialsCompUC}_DIR}/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
                  ${${sundialsCompUC}_DIR}/lib ${${sundialsCompUC}_DIR}
            PATHS /opt/local/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
                  /usr/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
                  /usr/local/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
            PATH_SUFFIXES Release
        )
        find_library(
            SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG
            NAMES ${sundialsComp}d
                  ${sundialsCompLC}d
                  ${sundialsCompUC}d
                  lib${sundialsComp}d
                  lib${sundialsCompLC}d
                  lib${sundialsCompUC}d
                  sundials_${sundialsComp}d
                  sundials_${sundialsCompLC}d
                  SUNDIALS_${sundialsCompUC}d
            HINTS ${SUNDIALS_DIR}/lib${SUNDIALS_SEARCH_LIB_POSTFIX} ${SUNDIALS_DIR}/lib
                  ${${sundialsCompUC}_DIR}/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
                  ${${sundialsCompUC}_DIR}/lib ${${sundialsCompUC}_DIR}
            PATHS /opt/local/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
                  /usr/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
                  /usr/local/lib${SUNDIALS_SEARCH_LIB_POSTFIX}
            PATH_SUFFIXES Debug
        )

        # check and auto complete release with debug if release missing and vice versa
        if(SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE)
            if(NOT SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG)
                set(SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG
                    ${SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE} CACHE PATH "Path to a library."
                                                                        FORCE
                )
            endif()
        endif()
        if(SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG)
            if(NOT SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE)
                set(SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE
                    ${SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG} CACHE PATH "Path to a library."
                                                                      FORCE
                )
            endif()
        endif()

        # check and append the and SUNDIALS_LIBRARIES list, and warn if not found (release and
        # debug) otherwise
        if(NOT SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE
           AND NOT SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG
        )
            if(SUNDIALS_VERBOSE)
                message(
                    WARNING
                        "   Failed to find ${sundialsComp} :
            Check you write correctly the component name (case sensitive),
            or set the SUNDIALS_${sundialsCompUC}_DIR to look inside,
            or set directly SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG and SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE
            "
                )
            endif()
        else()
            list(
                APPEND
                SUNDIALS_LIBRARIES
                optimized
                "${SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE}"
                debug
                "${SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG}"
            )
            if(TARGET SUNDIALS::${sundialsCompLC})

            else()
                add_library(SUNDIALS::${sundialsCompLC} STATIC IMPORTED)
                set_property(
                    TARGET SUNDIALS::${sundialsCompLC} APPEND PROPERTY IMPORTED_CONFIGURATIONS
                                                                       RELEASE
                )
                set_property(
                    TARGET SUNDIALS::${sundialsCompLC} APPEND PROPERTY IMPORTED_CONFIGURATIONS
                                                                       DEBUG
                )

            endif()
            set_target_properties(
                SUNDIALS::${sundialsCompLC}
                PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                           IMPORTED_LOCATION_RELEASE "${SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE}"
                           IMPORTED_LOCATION_DEBUG "${SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG}"
            )
            list(APPEND SUNDIALS_ACTUAL_TARGETS "SUNDIALS::${sundialsCompLC}")
        endif()

        # here we allow to find at least the include OR the lib dir and just warn if one of both
        # missing
        if(NOT SUNDIALS_${sundialsCompUC}_INCLUDE_DIR
           AND NOT SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE
        )
            set(SUNDIALS_${sundialsCompUC}_FOUND OFF)
        else()
            set(SUNDIALS_${sundialsCompUC}_FOUND ON)
            if(SUNDIALS_${sundialsCompUC}_INCLUDE_DIR)
                set_target_properties(
                    SUNDIALS::${sundialsCompLC}
                    PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                               "${SUNDIALS_${sundialsCompUC}_INCLUDE_DIR}"
                )
            endif()
        endif()

        # if one of both (include dir or filepath lib), then we provide a new cmake cache variable
        # for the search. Otherwise we don't need anymore to expose all intermediates variables
        if(NOT SUNDIALS_${sundialsCompUC}_FOUND)
            set(SUNDIALS_${sundialsCompUC}_DIR "$ENV{SUNDIALS_${sundialsCompUC}_DIR}"
                CACHE PATH "${sundialsComp} root directory"
            )
        else()
            mark_as_advanced(SUNDIALS_${sundialsCompUC}_INCLUDE_DIR)
            mark_as_advanced(SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE)
            mark_as_advanced(SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG)
            if(DEFINED SUNDIALS_${sundialsCompUC}_DIR)
                mark_as_advanced(SUNDIALS_${sundialsCompUC}_DIR)
            endif()
        endif()

        if(SUNDIALS_VERBOSE)
            message(
                STATUS
                    "   SUNDIALS_${sundialsCompUC}_FOUND = ${SUNDIALS_${sundialsCompUC}_FOUND} : "
            )
            message(
                STATUS
                    "      * SUNDIALS_${sundialsCompUC}_INCLUDE_DIR = ${SUNDIALS_${sundialsCompUC}_INCLUDE_DIR}"
            )
            message(
                STATUS
                    "      * SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG = ${SUNDIALS_${sundialsCompUC}_LIBRARY_DEBUG}"
            )
            message(
                STATUS
                    "      * SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE = ${SUNDIALS_${sundialsCompUC}_LIBRARY_RELEASE}"
            )
        endif()

        list(APPEND SUNDIALS_FOUND_LIST SUNDIALS_${sundialsCompUC}_FOUND)

    endforeach()

    # set the final SUNDIALS_FOUND based on all previous components found (status)
    foreach(sundialsComp ${SUNDIALS_FIND_COMPONENTS})
        string(TOUPPER ${sundialsComp} sundialsCompUC)
        set(SUNDIALS_FOUND ON)
        if(SUNDIALS_VERBOSE)
            message(STATUS "final check: ${SUNDIALS_${sundialsCompUC}_FOUND}")
        endif()
        if(NOT ${SUNDIALS_${sundialsCompUC}_FOUND})
            if(${SUNDIALS_FIND_REQUIRED_${sundialsCompUC}}) # if the component was not found and is
                                                            # required
                set(SUNDIALS_FOUND OFF)
                break() # one required component not found is enough to fail
            endif()
        endif()
    endforeach()
endmacro()

# Default behavior if user don't use the COMPONENTS flag in find_package(SUNDIALS ...) command
if(NOT SUNDIALS_FIND_COMPONENTS)
    list(APPEND SUNDIALS_FIND_COMPONENTS IDA KINSOL nvecserial)
else()
    list(APPEND SUNDIALS_FIND_COMPONENTS nvecserial)
endif()

sundials_find_components()

if(SUNDIALS_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES SUNDIALS_INCLUDE_DIRS)
endif()

if(SUNDIALS_VERBOSE)
    message(STATUS "Finish to FindSUNDIALS.cmake => SUNDIALS_FOUND=${SUNDIALS_FOUND}")
endif()

# Show error if not found and _REQUIRED
if(NOT SUNDIALS_FOUND)
    # make FIND_PACKAGE friendly
    if(NOT SUNDIALS_FIND_QUIETLY)
        if(SUNDIALS_FIND_REQUIRED)
            message(FATAL_ERROR "SUNDIALS required but some headers or libs not found.")
        else()
            message(STATUS "ERROR: SUNDIALS was not found.")
        endif()
    endif()
else()
    # add the linkages to the targets
    if(TARGET SUNDIALS::SUNDIALS)

    else()
        add_library(SUNDIALS::SUNDIALS INTERFACE IMPORTED)
    endif()
    set_target_properties(
        SUNDIALS::SUNDIALS PROPERTIES INTERFACE_LINK_LIBRARIES "${SUNDIALS_ACTUAL_TARGETS}"
    )
    set_target_properties(
        SUNDIALS::SUNDIALS PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${SUNDIALS_INCLUDE_DIRS}"
    )
endif()
