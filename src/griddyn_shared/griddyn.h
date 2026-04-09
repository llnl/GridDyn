/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

/** @file
@brief import header for importing the griddyn API
*/

/** @defgroup GridDyn_api the group of function defining the C api to gridDyn
 *  @{
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_H_

#if defined _WIN32 || defined __CYGWIN__
/* Note: both gcc & MSVC on Windows support this syntax. */
#    define GRIDDYN_EXPORT __declspec(dllimport)
#else
#    define GRIDDYN_EXPORT
#endif  // defined _WIN32 || defined __CYGWIN__
#include "griddyn_export.h"
#include "griddyn_export_advanced.h"

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_H_
