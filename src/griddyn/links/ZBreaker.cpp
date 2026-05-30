/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "ZBreaker.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <string>

namespace griddyn::links {
using units::unit;

static TypeFactory<ZBreaker>
    glf("link", std::to_array<std::string_view>({"zbreaker", "zline", "busbreaker"}));

ZBreaker::ZBreaker(const std::string& objName): Link(objName), merged(CoreObject::extra_bool)
{
    opFlags.set(network_connected);
}
CoreObject* ZBreaker::clone(CoreObject* obj) const
{
    auto lnk = cloneBase<ZBreaker, Link>(this, obj);
    if (lnk == nullptr) {
        return obj;
    }
    return lnk;
}
// parameter set functions

void ZBreaker::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Link::set(param, val);
    }
}

void ZBreaker::set(std::string_view param, double val, unit unitType)
{
    if (param.empty()) {
    } else {
        Link::set(param, val, unitType);
    }
}

void ZBreaker::switchChange(int /*switchNum*/)
{
    coordinateMergeStatus();
}
void ZBreaker::pFlowObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    coordinateMergeStatus();
}
void ZBreaker::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    coordinateMergeStatus();
}
void ZBreaker::switchMode(index_t /*num*/, bool mode)
{
    // TODO(phlpt): This shouldn't cause enable/disable. Replace this with some of the
    // checks for enabled disable
    if (mode == opFlags[switch1_open_flag]) {
        return;
    }

    opFlags.flip(switch1_open_flag);
    opFlags.flip(switch2_open_flag);
    if (opFlags[pFlow_initialized]) {
        if (linkInfo.v1 < 0.2) {
            alert(this, POTENTIAL_FAULT_CHANGE);
        }
        coordinateMergeStatus();
    }

    /*if (opFlags[switch2_open_flag])
{
enable();
opFlags.reset(switch2_open_flag);
}
else
{
disable();
opFlags.set(switch2_open_flag);
}*/
}

void ZBreaker::updateLocalCache()
{
    if (!isEnabled()) {
        return;
    }
    linkInfo.v1 = B1->getVoltage();
    linkInfo.v2 = linkInfo.v1;
}
void ZBreaker::updateLocalCache(const IOdata& /*inputs*/,
                                const StateData& sD,
                                const SolverMode& /*sMode*/)
{
    if (!isEnabled()) {
        return;
    }
    if (!sD.updateRequired(linkInfo.seqID)) {
        return;
    }
    linkInfo = {};
    linkInfo.seqID = sD.seqID;
    linkInfo.v1 = B1->getVoltage();
    linkInfo.v2 = linkInfo.v1;
}

double ZBreaker::quickupdateP()
{
    return 0;
}
void ZBreaker::coordinateMergeStatus()
{
    if (isConnected()) {
        if (!merged) {
            merge();
        }
    } else if (merged) {
        unmerge();
    }
}
void ZBreaker::merge()
{
    B1->mergeBus(B2);
    merged = true;
}

void ZBreaker::unmerge()
{
    B1->unmergeBus(B2);
    merged = false;
}

int ZBreaker::fixRealPower(double /*power*/,
                           id_type_t /*measureTerminal*/,
                           id_type_t /*fixedTerminal*/,
                           units::unit /*unitType*/)
{
    return 1;
}
int ZBreaker::fixPower(double /*rPower*/,
                       double /*qPower*/,
                       id_type_t /*measureTerminal*/,
                       id_type_t /*fixedTerminal*/,
                       units::unit /*unitType*/)
{
    return 1;
}

}  // namespace griddyn::links
