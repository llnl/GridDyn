#-------------------------------------------------------------------------------
# Copyright (c) 2013-2013, Lars Baehren <lbaehren@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#  * Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#-------------------------------------------------------------------------------

# SQLite is an embedded SQL database engine. Unlike most other SQL databases, SQLite does not have a
# separate server process. SQLite reads and writes directly to ordinary disk files. A complete SQL
# database with multiple tables, indices, triggers, and views, is contained in a single disk file.
# The database file format is cross-platform - you can freely copy a database between 32-bit and
# 64-bit systems or between big-endian and little-endian architectures.
#
# The following variables are set when SQLITE is found: SQLITE_FOUND      = Set to true, if all
# components of SQLite have been found. SQLITE_INCLUDES   = Include path for the header files of
# SQLite SQLITE_LIBRARIES  = Link these to use SQLite SQLITE_LFLAGS     = Linker flags (optional)

if(NOT SQLITE_FOUND)

    if(NOT SQLITE_ROOT_DIR)
        set(SQLITE_ROOT_DIR ${CMAKE_INSTALL_PREFIX})
    endif(NOT SQLITE_ROOT_DIR)

    # ____________________________________________________________________________ Package
    # configuration

    find_program(
        SQLITE_PC sqlite3.pc sqlite.pc HINTS ${SQLITE_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
        PATH_SUFFIXES lib lib/pkgconfig
    )

    if(SQLITE_PC)

        # Installation prefix
        file(STRINGS ${SQLITE_PC} SQLITE_ROOT_DIR REGEX "prefix=" LIMIT_COUNT 1)
        string(REGEX REPLACE "prefix=" "" SQLITE_ROOT_DIR ${SQLITE_ROOT_DIR})

        # Version number
        file(STRINGS ${SQLITE_PC} SQLITE_VERSION REGEX "Version:")
        if(SQLITE_VERSION)
            string(REGEX REPLACE "Version: " "" SQLITE_VERSION ${SQLITE_VERSION})
            string(REGEX REPLACE "\\." ";" SQLITE_VERSION ${SQLITE_VERSION})
            #
            list(GET SQLITE_VERSION 0 SQLITE_VERSION_MAJOR)
            list(GET SQLITE_VERSION 1 SQLITE_VERSION_MINOR)
            list(GET SQLITE_VERSION 2 SQLITE_VERSION_PATCH)
            # Reassemble full version number
            set(SQLITE_VERSION "${SQLITE_VERSION_MAJOR}")
            set(SQLITE_VERSION "${SQLITE_VERSION}.${SQLITE_VERSION_MINOR}")
            set(SQLITE_VERSION "${SQLITE_VERSION}.${SQLITE_VERSION_PATCH}")
        endif(SQLITE_VERSION)

    endif(SQLITE_PC)

    # ____________________________________________________________________________ Check for the
    # header files

    if(SQLITE_PC)
        find_path(
            SQLITE_INCLUDES
            NAMES sqlite3.h sqlite3ext.h
            HINTS ${SQLITE_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
            PATH_SUFFIXES include NO_CMAKE_SYSTEM_PATH
        )
    else(SQLITE_PC)
        find_path(
            SQLITE_INCLUDES
            NAMES sqlite3.h sqlite3ext.h
            HINTS ${SQLITE_ROOT_DIR}/include ${CMAKE_INSTALL_PREFIX}
            PATH_SUFFIXES include
        )
    endif(SQLITE_PC)

    # ____________________________________________________________________________ Check for the
    # library

    find_library(
        SQLITE_LIBRARIES sqlite3 HINTS ${SQLITE_ROOT_DIR} ${CMAKE_INSTALL_PREFIX} PATH_SUFFIXES lib
    )

    # ____________________________________________________________________________ Check for the
    # executable

    find_program(
        SQLITE_EXECUTABLE sqlite3 HINTS ${SQLITE_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
        PATH_SUFFIXES bin
    )

    # ____________________________________________________________________________ Actions taken
    # when all components have been found

    find_package_handle_standard_args(
        SQLITE DEFAULT_MSG SQLITE_LIBRARIES SQLITE_INCLUDES SQLITE_EXECUTABLE
    )

    if(SQLITE_FOUND)
        if(NOT SQLITE_FIND_QUIETLY)
            message(STATUS "Found components for SQLite")
            message(STATUS "SQLITE_ROOT_DIR   = ${SQLITE_ROOT_DIR}")
            message(STATUS "SQLITE_VERSION    = ${SQLITE_VERSION}")
            message(STATUS "SQLITE_INCLUDES   = ${SQLITE_INCLUDES}")
            message(STATUS "SQLITE_LIBRARIES  = ${SQLITE_LIBRARIES}")
            message(STATUS "SQLITE_EXECUTABLE = ${SQLITE_EXECUTABLE}")
        endif(NOT SQLITE_FIND_QUIETLY)
    else(SQLITE_FOUND)
        if(SQLITE_FIND_REQUIRED)
            message(FATAL_ERROR "Could not find SQLite!")
        endif(SQLITE_FIND_REQUIRED)
    endif(SQLITE_FOUND)

    # ____________________________________________________________________________ Mark advanced
    # variables

    mark_as_advanced(SQLITE_ROOT_DIR SQLITE_INCLUDES SQLITE_LIBRARIES SQLITE_EXECUTABLE)

endif(NOT SQLITE_FOUND)
