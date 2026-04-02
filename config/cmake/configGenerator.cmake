# LLNS Copyright Start
# Copyright (c) 2017, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

# With C++20 and modern compiler minimums (GCC 13+/Clang 18+), these standard
# library/compiler features are baseline and no longer need compile-probing.
set(HAVE_OPTIONAL ON)
set(HAVE_VARIANT ON)
set(HAVE_STRING_VIEW ON)
set(HAVE_CLAMP ON)
set(HAVE_HYPOT3 ON)
set(HAVE_IF_CONSTEXPR ON)
set(HAVE_FALLTHROUGH ON)
set(HAVE_UNUSED ON)
set(HAVE_VARIABLE_TEMPLATES ON)
set(HAVE_SHARED_MUTEX ON)
set(HAVE_SHARED_TIMED_MUTEX ON)
set(HAVE_EXP_STRING_VIEW ON)

if (NOT NO_CONFIG_GENERATION)
    if (CONFIGURE_TARGET_LOCATION)
        CONFIGURE_FILE(${CMAKE_CURRENT_LIST_DIR}/compiler-config.h.in ${CONFIGURE_TARGET_LOCATION}/compiler-config.h)
    else()
        CONFIGURE_FILE(${CMAKE_CURRENT_LIST_DIR}/compiler-config.h.in ${PROJECT_BINARY_DIR}/compiler-config.h)
    endif()
endif(NOT NO_CONFIG_GENERATION)
