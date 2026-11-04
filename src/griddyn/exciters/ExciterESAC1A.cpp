/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterESAC1A.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include <cmath>
#include <string>

namespace griddyn::exciters {

const ModelDescription& ExciterESAC1A::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterESAC1A>(
        "ExciterESAC1A",
        &ExciterEXAC1::getModelDescription(),
        {parameter({"vamax"}, &ExciterESAC1A::Vamax, units::one, "Vamax upper limit"),
         parameter({"vamin"}, &ExciterESAC1A::Vamin, units::one, "Vamin lower limit")});
    return description;
}

void ExciterESAC1A::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ExciterESAC1A::getModelDescription();
    getParamString<ExciterESAC1A, ExciterEXAC1>(this,
                                                pstr,
                                                description.numericNames(),
                                                description.stringNames(),
                                                description.flagNames(),
                                                pstype);
}

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
    if (!ExciterESAC1A::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        ExciterEXAC1::set(param, val, unitType);
    }
}

double ExciterESAC1A::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            ExciterESAC1A::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return ExciterEXAC1::get(param, unitType);
}

double ExciterESAC1A::regulatorUpperLimit() const
{
    return Vamax;
}

bool ExciterESAC1A::adjustRegulatorInitialUpperLimit(double initialValue)
{
    return adjustInitialUpperLimit(initialValue, Vamax, "ESAC1A initial control-element output");
}

double ExciterESAC1A::regulatorLowerLimit() const
{
    return Vamin;
}

void ExciterESAC1A::set(std::string_view param, std::string_view val)
{
    if (!ExciterESAC1A::getModelDescription().trySetLocal(*this, param, val)) {
        ExciterEXAC1::set(param, val);
    }
}

}  // namespace griddyn::exciters
