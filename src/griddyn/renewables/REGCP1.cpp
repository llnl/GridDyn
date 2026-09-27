/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REGCP1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <string>

namespace griddyn {

REGCP1::REGCP1(const std::string& name): REGCA1(name) {}

CoreObject* REGCP1::clone(CoreObject* obj) const
{
    return cloneBase<REGCP1, REGCA1>(this, obj);
}

void REGCP1::set(std::string_view param, double val, units::unit unitType)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "pll") {
        if (val != 0.0) {
            throw InvalidParameterValue("REGCP1 PLL angle rotation is not implemented");
        }
        return;
    }
    REGCA1::set(param, val, unitType);
}

void REGCP1::set(std::string_view param, std::string_view val)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "pll") {
        const auto value = gmlc::utilities::convertToLowerCase(std::string{val});
        if (!value.empty() && value != "none" && value != "null" && value != "0") {
            throw InvalidParameterValue("REGCP1 PLL angle rotation is not implemented");
        }
        return;
    }
    GridComponent::set(param, val);
}

}  // namespace griddyn
