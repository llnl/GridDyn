/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ControlSystem.h"

#include "../Block.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <string>
#include <vector>

namespace griddyn {
ControlSystem::ControlSystem(const std::string& objName): GridSubModel(objName) {}

ControlSystem::~ControlSystem() = default;

CoreObject* ControlSystem::clone(CoreObject* obj) const
{
    auto* controlSystemClone = cloneBase<ControlSystem, GridSubModel>(this, obj);
    if (controlSystemClone == nullptr) {
        return obj;
    }
    return controlSystemClone;
}

void ControlSystem::add(CoreObject* obj)
{
    if (dynamic_cast<GridBlock*>(obj) != nullptr) {
        add(static_cast<GridBlock*>(obj));
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void ControlSystem::add(GridBlock* blk)
{
    blocks.push_back(blk);
    blk->locIndex = static_cast<index_t>(blocks.size()) - 1;
    addSubObject(blk);
}

void ControlSystem::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& blockObject : blocks) {
        blockObject->dynInitializeA(time0, flags);
    }
}
void ControlSystem::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& /*desiredOutput*/,
                                         IOdata& /*inputSet*/)
{
}

void ControlSystem::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        GridSubModel::set(param, val);
    }
}

void ControlSystem::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty() || param[0] == '#') {
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

index_t ControlSystem::findIndex(std::string_view /*field*/, const solverMode& /*sMode*/) const
{
    return kInvalidLocation;
}

void ControlSystem::residual(const IOdata& /*inputs*/,
                             const stateData& /*sD*/,
                             double /*resid*/[],
                             const solverMode& /*sMode*/)
{
}

void ControlSystem::jacobianElements(const IOdata& /*inputs*/,
                                     const stateData& /*sD*/,
                                     matrixData<double>& /*md*/,
                                     const IOlocs& /*inputLocs*/,
                                     const solverMode& /*sMode*/)
{
}

void ControlSystem::timestep(coreTime /*time*/,
                             const IOdata& /*inputs*/,
                             const solverMode& /*sMode*/)
{
}

void ControlSystem::rootTest(const IOdata& /*inputs*/,
                             const stateData& /*sD*/,
                             double /*roots*/[],
                             const solverMode& /*sMode*/)
{
}

void ControlSystem::rootTrigger(coreTime /*time*/,
                                const IOdata& /*inputs*/,
                                const std::vector<int>& /*rootMask*/,
                                const solverMode& /*sMode*/)
{
}

ChangeCode ControlSystem::rootCheck(const IOdata& /*inputs*/,
                                    const stateData& /*sD*/,
                                    const solverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    return ChangeCode::NO_CHANGE;
}
// virtual void setTime(coreTime time){prevTime=time;};
}  // namespace griddyn
