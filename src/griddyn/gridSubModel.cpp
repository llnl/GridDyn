/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridSubModel.h"

#include "measurement/objectGrabbers.h"
#include <string>

namespace griddyn {
GridSubModel::GridSubModel(const std::string& objName): GridComponent(objName)
{
    opFlags.set(no_powerflow_operations);
    m_outputSize = 1;
}

void GridSubModel::pFlowInitializeA(coreTime time, std::uint32_t flags)
{
    GridComponent::pFlowInitializeA(time, flags);
}

void GridSubModel::pFlowInitializeB()
{
    GridComponent::pFlowInitializeB();
}
void GridSubModel::dynInitializeA(coreTime time, std::uint32_t flags)
{
    if (isEnabled()) {
        dynObjectInitializeA(time, flags);

        auto& so = offsets.getOffsets(cLocalSolverMode);
        if (getSubObjects().empty()) {
            so.localLoadAll(true);
        } else {
            loadStateSizes(cLocalSolverMode);
        }

        so.setOffset(0);
        prevTime = time;
        updateFlags(true);
        setupDynFlags();
    }
}

void GridSubModel::dynInitializeB(const IOdata& inputs,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet)
{
    if (isEnabled()) {
        // make sure the state vectors are sized properly
        auto ns = offsets.local().local.totalSize();
        m_state.resize(ns, 0);
        m_dstate_dt.clear();
        m_dstate_dt.resize(ns, 0);

        dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        if (updatePeriod < maxTime) {
            enable_updates();
            setUpdateTime(prevTime + updatePeriod);
            alert(this, UPDATE_REQUIRED);
        }
        opFlags.set(dyn_initialized);
    }
}

double GridSubModel::get(std::string_view param, units::unit unitType) const
{
    auto fptr = getObjectFunction(this, std::string{param});
    if (fptr.first) {
        CoreObject* tobj = const_cast<GridSubModel*>(this);
        return convert(fptr.first(tobj), fptr.second, unitType, systemBasePower);
    }

    return GridComponent::get(param, unitType);
}
}  // namespace griddyn

