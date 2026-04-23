/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helperObject.h"

#include "coreExceptions.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewOps.h"
#include "utilities/dataDictionary.h"
#include <string>
#include <string_view>
#include <utility>
namespace griddyn {
namespace {
    dataDictionary<std::uint64_t, std::string> descriptionDictionary;
}

// start at 100 since there are some objects that use low numbers as a check for interface number
// and the id as secondary
std::atomic<std::uint64_t> helperObject::s_obcnt(101);

helperObject::helperObject() noexcept: m_oid(s_obcnt++) {}
helperObject::~helperObject() = default;

helperObject::helperObject(std::string objectName): m_oid(s_obcnt++), um_name(std::move(objectName))
{
}

void helperObject::set(std::string_view param, std::string_view val)
{
    if ((param == "name") || (param == "id")) {
        setName(val);
    } else if (param == "description") {
        setDescription(val);
    } else if ((param == "flags") || (param == "flag")) {
        setMultipleFlags(this, val);
    } else {
        throw(unrecognizedParameter(std::string{param}));
    }
}

void helperObject::set(std::string_view param, double val)
{
    setFlag(param, (val > 0.1));
}
void helperObject::setDescription(std::string_view description)  // NOLINT
{
    descriptionDictionary.update(m_oid, std::string{description});
}

std::string helperObject::getDescription() const
{
    return descriptionDictionary.query(m_oid);
}
void helperObject::setFlag(std::string_view flag, bool /*val*/)
{
    throw(unrecognizedParameter(std::string{flag}));
}
bool helperObject::getFlag(std::string_view flag) const
{
    throw(unrecognizedParameter(std::string{flag}));
}
double helperObject::get(std::string_view param) const
{
    return getFlag(param) ? 1.0 : 0.0;
}
void helperObject::nameUpdate() {}
void helperObject::makeNewOID()
{
    m_oid = ++s_obcnt;
}
coreObject* helperObject::getOwner() const
{
    return nullptr;
}
void setMultipleFlags(helperObject* obj, std::string_view flags)
{
    auto lcflags = gmlc::utilities::convertToLowerCase(flags);
    auto flgs = gmlc::utilities::string_viewOps::split(lcflags);
    gmlc::utilities::string_viewOps::trim(flgs);
    for (const auto& flag : flgs) {
        if (flag.empty()) {
            continue;
        }
        if (flag.front() != '-') {
            obj->setFlag(std::string{flag}, true);
        } else {
            obj->setFlag(std::string{flag.substr(1)}, false);
        }
    }
}

}  // namespace griddyn
