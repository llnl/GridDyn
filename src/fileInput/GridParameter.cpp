/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "GridParameter.h"

#include "core/CoreExceptions.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/gridDynDefinitions.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
GridParameter::GridParameter() = default;
GridParameter::GridParameter(const std::string& str)
{
    fromString(str);
}
GridParameter::GridParameter(std::string fld, double val):
    field(std::move(fld)), value(val), valid(true)
{
}
GridParameter::GridParameter(std::string fld, std::string val):
    field(std::move(fld)), strVal(std::move(val)), valid(true), stringType(true)
{
}

void GridParameter::reset()
{
    valid = false, stringType = false, paramUnits = units::defunit;
    applyIndex.resize(0);
}

void GridParameter::fromString(const std::string& str)
{
    using gmlc::utilities::string_viewOps::trim;
    using std::string_view;
    string_view strv(str);
    valid = false;
    size_t equalPos = strv.find_last_of('=');
    if (equalPos == std::string::npos) {
        throw(invalidParameterValue(str));
    }
    valid = true;
    auto fieldString = trim(strv.substr(0, equalPos));

    // now read the value
    strVal = std::string{strv.substr(equalPos + 1)};
    value = gmlc::utilities::numeric_conversionComplete(strVal, kNullVal);
    stringType = (value == kNullVal);

    equalPos = fieldString.find_first_of('(');
    if (equalPos != std::string_view::npos) {
        const size_t closingParenPos = fieldString.find_last_of(')');
        paramUnits = units::unit_cast_from_string(
            std::string{fieldString.substr(equalPos + 1, closingParenPos - equalPos - 1)});
        field = std::string{fieldString.substr(0, equalPos)};
    } else {
        field = std::string{fieldString};
    }
}

}  // namespace griddyn
