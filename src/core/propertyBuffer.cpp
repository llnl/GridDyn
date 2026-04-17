/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "propertyBuffer.h"

#include "coreObject.h"
#include "helperObject.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
namespace griddyn {
void propertyBuffer::set(std::string_view param, std::string_view val)
{
    properties.emplace_back(std::string{param}, property_type(std::string{val}));
}
void propertyBuffer::set(std::string_view param, double val)
{
    properties.emplace_back(std::string{param}, property_type(val));
}
void propertyBuffer::set(std::string_view param, double val, units::unit unitType)
{
    properties.emplace_back(std::string{param}, property_type(std::make_pair(val, unitType)));
}
void propertyBuffer::set(std::string_view param, int val)
{
    properties.emplace_back(std::string{param}, property_type(val));
}
void propertyBuffer::setFlag(std::string_view flag, bool val)
{
    properties.emplace_back(std::string{flag}, property_type(val));
}

void propertyBuffer::remove(std::string_view param)
{
    auto checkMatch = [param](const auto& input) { return (std::get<0>(input) == param); };

    auto strend = std::remove_if(properties.begin(), properties.end(), checkMatch);
    properties.erase(strend, properties.end());
}

void propertyBuffer::clear()
{
    properties.clear();
}
void propertyBuffer::apply(coreObject* obj) const
{
    for (auto& prop : properties) {
        switch (prop.second.index()) {
            case 0:
                obj->set(prop.first, mpark::get<double>(prop.second));
                break;
            case 1:
                obj->set(prop.first,
                         mpark::get<std::pair<double, units::unit>>(prop.second).first,
                         mpark::get<std::pair<double, units::unit>>(prop.second).second);
                break;
            case 2:
                obj->set(prop.first, mpark::get<int>(prop.second));
                break;
            case 3:
                obj->setFlag(prop.first, mpark::get<bool>(prop.second));
                break;
            case 4:
                obj->set(prop.first, mpark::get<std::string>(prop.second));
                break;
        }
    }
}

}  // namespace griddyn
