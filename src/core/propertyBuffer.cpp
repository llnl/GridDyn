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
#include <utility>
namespace griddyn {
void propertyBuffer::set(const std::string& param, const std::string& val)
{
    properties.emplace_back(param, property_type(val));
}
void propertyBuffer::set(const std::string& param, double val)
{
    properties.emplace_back(param, property_type(val));
}
void propertyBuffer::set(const std::string& param, double val, units::unit unitType)
{
    properties.emplace_back(param, property_type(std::make_pair(val, unitType)));
}
void propertyBuffer::set(const std::string& param, int val)
{
    properties.emplace_back(param, property_type(val));
}
void propertyBuffer::setFlag(const std::string& flag, bool val)
{
    properties.emplace_back(flag, property_type(val));
}

void propertyBuffer::remove(const std::string& param)
{
    // Using auto lambda here still seems like magic that this works
    auto checkMatch = [param](auto input) { return (std::get<0>(input) == param); };

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
