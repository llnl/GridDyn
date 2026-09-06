/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelGENSAE.h"

#include "core/CoreObjectTemplates.hpp"
#include "utilities/Saturation.h"
#include <string>

namespace griddyn::genmodels {
GenModelGENSAE::GenModelGENSAE(const std::string& objName): GenModelGENSAL(objName)
{
    sat.setType(utilities::Saturation::SaturationType::EXPONENTIAL);
    sat.setParam(S10, S12);
}

CoreObject* GenModelGENSAE::clone(CoreObject* obj) const
{
    auto* cloneObject = cloneBase<GenModelGENSAE, GenModelGENSAL>(this, obj);
    if (cloneObject == nullptr) {
        return obj;
    }
    cloneObject->sat = sat;
    return cloneObject;
}

bool GenModelGENSAE::usesExponentialSaturation() const
{
    return true;
}

}  // namespace griddyn::genmodels
