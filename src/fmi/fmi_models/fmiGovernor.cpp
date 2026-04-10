/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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

namespace griddyn {
namespace fmi {
    fmiGovernor::fmiGovernor(const std::string& objName): fmiMEWrapper<Governor>(objName) {}

    coreObject* fmiGovernor::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<fmiGovernor, fmiMEWrapper<Governor>>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }

        return nobj;
    }

    void fmiGovernor::set(const std::string& param, const std::string& val)
    {
        if (param.empty()) {
        } else {
            fmiMEWrapper<Governor>::set(param, val);
        }
    }

    void fmiGovernor::set(const std::string& param, double val, units::unit unitType)
    {
        if (param.empty()) {
        } else {
            fmiMEWrapper<Governor>::set(param, val, unitType);
        }
    }

}  // namespace fmi
}  // namespace griddyn
