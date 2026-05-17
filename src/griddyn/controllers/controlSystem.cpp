/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "controlSystem.h"

#include "../Block.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include <string>
#include <vector>

namespace griddyn {
controlSystem::controlSystem(const std::string& objName): GridSubModel(objName) {}

controlSystem::~controlSystem() = default;

CoreObject* controlSystem::clone(CoreObject* obj) const
{
    auto* cs = cloneBase<controlSystem, GridSubModel>(this, obj);
    if (cs == nullptr) {
        return obj;
    }
    return cs;
}

void controlSystem::add(CoreObject* obj)
{
    if (dynamic_cast<Block*>(obj) != nullptr) {
        add(static_cast<Block*>(obj));
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void controlSystem::add(Block* blk)
{
    blocks.push_back(blk);
    blk->locIndex = static_cast<index_t>(blocks.size()) - 1;
    addSubObject(blk);
}

void controlSystem::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& bb : blocks) {
        bb->dynInitializeA(time0, flags);
    }
}
void controlSystem::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& /*desiredOutput*/,
                                         IOdata& /*inputSet*/)
{
}

void controlSystem::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        GridSubModel::set(param, val);
    }
}

void controlSystem::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty() || param[0] == '#') {
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

index_t controlSystem::findIndex(std::string_view /*field*/, const solverMode& /*sMode*/) const
{
    return kInvalidLocation;
}

void controlSystem::residual(const IOdata& /*inputs*/,
                             const stateData& /*sD*/,
                             double /*resid*/[],
                             const solverMode& /*sMode*/)
{
}

void controlSystem::jacobianElements(const IOdata& /*inputs*/,
                                     const stateData& /*sD*/,
                                     matrixData<double>& /*md*/,
                                     const IOlocs& /*inputLocs*/,
                                     const solverMode& /*sMode*/)
{
}

void controlSystem::timestep(coreTime /*time*/,
                             const IOdata& /*inputs*/,
                             const solverMode& /*sMode*/)
{
}

void controlSystem::rootTest(const IOdata& /*inputs*/,
                             const stateData& /*sD*/,
                             double /*roots*/[],
                             const solverMode& /*sMode*/)
{
}

void controlSystem::rootTrigger(coreTime /*time*/,
                                const IOdata& /*inputs*/,
                                const std::vector<int>& /*rootMask*/,
                                const solverMode& /*sMode*/)
{
}

change_code controlSystem::rootCheck(const IOdata& /*inputs*/,
                                     const stateData& /*sD*/,
                                     const solverMode& /*sMode*/,
                                     check_level_t /*level*/)
{
    return change_code::no_change;
}
// virtual void setTime(coreTime time){prevTime=time;};
}  // namespace griddyn
