/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "libraryLoader.h"

#include "griddyn-config.h"

#ifdef ENABLE_EXTRA_MODELS
#    include "extraModels.h"
#endif

#ifdef ENABLE_FMI
#    include "fmiGDinfo.h"
#endif

void loadLibraries()
{
#ifdef ENABLE_FMI
    loadFmiLibrary();
#endif

#ifdef ENABLE_EXTRA_MODELS
    loadExtraModels("");
#endif
}
