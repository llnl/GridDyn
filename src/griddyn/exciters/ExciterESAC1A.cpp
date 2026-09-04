/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterESAC1A.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <cmath>

namespace griddyn::exciters {
ExciterESAC1A::ExciterESAC1A(const std::string& objName): ExciterEXAC1(objName) {}

CoreObject* ExciterESAC1A::clone(CoreObject* obj) const
{
    auto* clone = cloneBase<ExciterESAC1A, ExciterEXAC1>(this, obj);
    if (clone == nullptr) {
        return obj;
    }
    ExciterEXAC1::clone(clone);
    clone->Vamax = Vamax;
    clone->Vamin = Vamin;
    return clone;
}

void ExciterESAC1A::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(Vamax) || !std::isfinite(Vamin) || (Vamax < Vamin)) {
        throw InvalidParameterValue("ESAC1A control-element limits");
    }
    ExciterEXAC1::dynObjectInitializeA(time0, flags);
}

void ExciterESAC1A::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "vamax") {
        Vamax = val;
    } else if (param == "vamin") {
        Vamin = val;
    } else {
        ExciterEXAC1::set(param, val, unitType);
    }
}

double ExciterESAC1A::get(std::string_view param, units::unit unitType) const
{
    if (param == "vamax") {
        return Vamax;
    }
    if (param == "vamin") {
        return Vamin;
    }
    return ExciterEXAC1::get(param, unitType);
}

double ExciterESAC1A::regulatorUpperLimit() const
{
    return Vamax;
}

double ExciterESAC1A::regulatorLowerLimit() const
{
    return Vamin;
}
}  // namespace griddyn::exciters
