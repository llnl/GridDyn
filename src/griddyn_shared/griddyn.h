/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
@brief import header for importing the griddyn API
*/

/** @defgroup GridDyn_api the group of function defining the C api to gridDyn
 *  @{
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_H_
#    define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_H_

#    if defined _WIN32 || defined __CYGWIN__
/* Note: both gcc & MSVC on Windows support this syntax. */
#        define GRIDDYN_EXPORT __declspec(dllimport)
#    else
#        define GRIDDYN_EXPORT
#    endif  // defined _WIN32 || defined __CYGWIN__
#    include "griddyn_export.h"
#    include "griddyn_export_advanced.h"

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_H_
