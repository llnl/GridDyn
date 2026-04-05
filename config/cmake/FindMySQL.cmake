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

# * Check for the presence of MYSQL
#
# The following variables are set when MYSQL is found: MYSQL_FOUND      = Set to true, if all
# components of MYSQL have been found. MYSQL_INCLUDES   = Include path for the header files of MYSQL
# MYSQL_LIBRARIES  = Link these to use MYSQL MYSQL_LFLAGS     = Linker flags (optional)

if(NOT MYSQL_FOUND)

    if(NOT MYSQL_ROOT_DIR)
        set(MYSQL_ROOT_DIR ${CMAKE_INSTALL_PREFIX})
    endif(NOT MYSQL_ROOT_DIR)

    # _____________________________________________________________________________ Check for the
    # header files

    find_path(MYSQL_INCLUDES mysql.h HINTS ${MYSQL_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
              PATH_SUFFIXES include mysql include/mysql mysql5/mysql
    )

    # _____________________________________________________________________________ Check for the
    # library

    find_library(
        MYSQL_LIBRARIES mysqlclient HINTS ${MYSQL_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
        PATH_SUFFIXES lib mysql lib/mysql mysql5/mysql
    )

    # Extract the library path
    if(MYSQL_LIBRARIES)
        get_filename_component(MYSQL_LIBRARY_PATH ${MYSQL_LIBRARIES} PATH)
    endif(MYSQL_LIBRARIES)

    # _____________________________________________________________________________ Actions taken
    # when all components have been found

    if(MYSQL_INCLUDES AND MYSQL_LIBRARIES)
        set(MYSQL_FOUND TRUE)
    else(MYSQL_INCLUDES AND MYSQL_LIBRARIES)
        set(MYSQL_FOUND FALSE)
        if(NOT MYSQL_FIND_QUIETLY)
            if(NOT MYSQL_INCLUDES)
                message(STATUS "Unable to find MYSQL header files!")
            endif(NOT MYSQL_INCLUDES)
            if(NOT MYSQL_LIBRARIES)
                message(STATUS "Unable to find MYSQL library files!")
            endif(NOT MYSQL_LIBRARIES)
        endif(NOT MYSQL_FIND_QUIETLY)
    endif(MYSQL_INCLUDES AND MYSQL_LIBRARIES)

    if(MYSQL_FOUND)
        if(NOT MYSQL_FIND_QUIETLY)
            message(STATUS "Found components for MYSQL")
            message(STATUS "MYSQL_ROOT_DIR  = ${MYSQL_ROOT_DIR}")
            message(STATUS "MYSQL_INCLUDES  = ${MYSQL_INCLUDES}")
            message(STATUS "MYSQL_LIBRARIES = ${MYSQL_LIBRARIES}")
        endif(NOT MYSQL_FIND_QUIETLY)
    else(MYSQL_FOUND)
        if(MYSQL_FIND_REQUIRED)
            message(FATAL_ERROR "Could not find MYSQL!")
        endif(MYSQL_FIND_REQUIRED)
    endif(MYSQL_FOUND)

    # _____________________________________________________________________________ Mark advanced
    # variables

    mark_as_advanced(MYSQL_ROOT_DIR MYSQL_INCLUDES MYSQL_LIBRARIES)

endif(NOT MYSQL_FOUND)
