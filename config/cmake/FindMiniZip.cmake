#--------------------------------------------------------------------------------
# Copyright (c) 2012-2013, Lars Baehren <lbaehren@gmail.com>
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
#--------------------------------------------------------------------------------

# * Check for the presence of MINIZIP
#
# The following variables are set when MINIZIP is found: MINIZIP_FOUND      = Set to true, if all
# components of MINIZIP have been found. MINIZIP_INCLUDES   = Include path for the header files of
# MINIZIP MINIZIP_LIBRARIES  = Link these to use MINIZIP MINIZIP_LFLAGS     = Linker flags
# (optional)

if(NOT MINIZIP_FOUND)

    if(NOT MINIZIP_ROOT_DIR)
        set(MINIZIP_ROOT_DIR ${CMAKE_INSTALL_PREFIX})
    endif(NOT MINIZIP_ROOT_DIR)

    # _____________________________________________________________________________ Check for the
    # header files
    if(NOT NO_SYSTEM_MINIZIP)
        find_path(
            MINIZIP_INCLUDES
            NAMES minizip/minizip.h
            HINTS ${MINIZIP_ROOT_DIR}
            PATH_SUFFIXES include
        )

        # _____________________________________________________________________________ Check for
        # the library

        find_library(
            MINIZIP_LIBRARIES
            NAMES libminizip minizip
            HINTS ${MINIZIP_ROOT_DIR}
            PATH_SUFFIXES lib
        )
    else(NOT NO_SYSTEM_MINIZIP)
        find_path(
            MINIZIP_INCLUDES
            NAMES minizip/minizip.h
            HINTS ${MINIZIP_ROOT_DIR}
            PATH_SUFFIXES include
            NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH
        )

        # _____________________________________________________________________________ Check for
        # the library

        find_library(
            MINIZIP_LIBRARIES
            NAMES libminizip minizip
            HINTS ${MINIZIP_ROOT_DIR}
            PATH_SUFFIXES lib
            NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH
        )
    endif(NOT NO_SYSTEM_MINIZIP)

    # _____________________________________________________________________________ Actions taken
    # when all components have been found

    if(MINIZIP_INCLUDES AND MINIZIP_LIBRARIES)
        set(MINIZIP_FOUND TRUE)
    else(MINIZIP_INCLUDES AND MINIZIP_LIBRARIES)
        set(MINIZIP_FOUND FALSE)
        if(NOT MINIZIP_FIND_QUIETLY)
            if(NOT MINIZIP_INCLUDES)
                message(STATUS "Unable to find MINIZIP header files!")
            endif(NOT MINIZIP_INCLUDES)
            if(NOT MINIZIP_LIBRARIES)
                message(STATUS "Unable to find MINIZIP library files!")
            endif(NOT MINIZIP_LIBRARIES)
        endif(NOT MINIZIP_FIND_QUIETLY)
    endif(MINIZIP_INCLUDES AND MINIZIP_LIBRARIES)

    if(MINIZIP_FOUND)
        if(NOT MINIZIP_FIND_QUIETLY)
            message(STATUS "Found components for MINIZIP")
            message(STATUS "MINIZIP_ROOT_DIR  = ${MINIZIP_ROOT_DIR}")
            message(STATUS "MINIZIP_INCLUDES  = ${MINIZIP_INCLUDES}")
            message(STATUS "MINIZIP_LIBRARIES = ${MINIZIP_LIBRARIES}")
        endif(NOT MINIZIP_FIND_QUIETLY)
    else(MINIZIP_FOUND)
        if(MINIZIP_FIND_REQUIRED)
            message(FATAL_ERROR "Could not find MINIZIP!")
        endif(MINIZIP_FIND_REQUIRED)
    endif(MINIZIP_FOUND)

    # _____________________________________________________________________________ Mark advanced
    # variables

    mark_as_advanced(MINIZIP_ROOT_DIR MINIZIP_INCLUDES MINIZIP_LIBRARIES)

endif(NOT MINIZIP_FOUND)
