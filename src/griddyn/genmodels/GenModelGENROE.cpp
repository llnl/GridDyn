/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelGENROE.h"

#include "core/CoreObjectTemplates.hpp"
#include "utilities/Saturation.h"
#include <string>

namespace griddyn::genmodels {
GenModelGENROE::GenModelGENROE(const std::string& objName): GenModelGENROU(objName)
{
    sat.setType(utilities::Saturation::SaturationType::EXPONENTIAL);
    sat.setParam(S10, S12);
}

CoreObject* GenModelGENROE::clone(CoreObject* obj) const
{
    auto* cloneObject = cloneBase<GenModelGENROE, GenModelGENROU>(this, obj);
    if (cloneObject == nullptr) {
        return obj;
    }
    cloneObject->sat = sat;
    return cloneObject;
}

}  // namespace griddyn::genmodels
