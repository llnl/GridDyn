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
fmiExciter::fmiExciter(const std::string& objName): fmiMEWrapper<Exciter>(objName) {}

coreObject* fmiExciter::clone(coreObject* obj) const
{
    auto nobj = cloneBase<fmiExciter, fmiMEWrapper<Exciter>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void fmiExciter::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        fmiMEWrapper<Exciter>::set(param, val);
    }
}

void fmiExciter::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty()) {
    } else {
        fmiMEWrapper<Exciter>::set(param, val, unitType);
    }
}

}  // namespace griddyn::fmi
