/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RampSource.h"

#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include <ctime>
#include <string>

namespace griddyn::sources {
rampSource::rampSource(const std::string& objName, double startVal): Source(objName, startVal) {}
CoreObject* rampSource::clone(CoreObject* obj) const
{
    auto ld = cloneBase<rampSource, Source>(this, obj);
    if (ld == nullptr) {
        return obj;
    }
    ld->mp_dOdt = mp_dOdt;
    return ld;
}

// set properties
void rampSource::set(std::string_view param, std::string_view val)
{
    Source::set(param, val);
}
void rampSource::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "dodt") || (param == "ramp") || (param == "rate")) {
        mp_dOdt = val;
    } else {
        Source::set(param, val, unitType);
    }
}

double rampSource::computeOutput(coreTime time) const
{
    auto tdiff = time - prevTime;
    return m_output + mp_dOdt * tdiff;
}

double rampSource::getDoutdt(const IOdata& /*inputs*/,
                             const StateData& /*sD*/,
                             const SolverMode& /*sMode*/,
                             index_t num) const
{
    return (num == 0) ? mp_dOdt : 0.0;
}
}  // namespace griddyn::sources
