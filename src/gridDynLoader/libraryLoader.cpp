/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * LLNS Copyright Start
 * Copyright (c) 2016, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
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
