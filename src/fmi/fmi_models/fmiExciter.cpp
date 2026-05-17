/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiExciter.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridBus.h"
#include <string>

namespace griddyn::fmi {
FmiExciter::FmiExciter(const std::string& objName): FmiMEWrapper<Exciter>(objName) {}

CoreObject* FmiExciter::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FmiExciter, FmiMEWrapper<Exciter>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void FmiExciter::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<Exciter>::set(param, val);
    }
}

void FmiExciter::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<Exciter>::set(param, val, unitType);
    }
}

}  // namespace griddyn::fmi

