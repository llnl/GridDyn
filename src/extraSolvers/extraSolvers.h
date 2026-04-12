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
#pragma once

#include <string>
namespace griddyn {
/** @brief load the extra models specified by the subset indicator into the object factory
@param[in] subset specify a subset of the models to load
"all" or use default for all models
others will be added as the library grows
*/
void loadExtraSolvers(const std::string& subset = "");
}  // namespace griddyn
