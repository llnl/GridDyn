/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiGovernor.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridBus.h"
#include <string>

namespace griddyn::fmi {
FmiGovernor::FmiGovernor(const std::string& objName): FmiMEWrapper<Governor>(objName) {}

coreObject* FmiGovernor::clone(coreObject* obj) const
{
    auto nobj = cloneBase<FmiGovernor, FmiMEWrapper<Governor>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void FmiGovernor::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<Governor>::set(param, val);
    }
}

void FmiGovernor::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<Governor>::set(param, val, unitType);
    }
}

}  // namespace griddyn::fmi
