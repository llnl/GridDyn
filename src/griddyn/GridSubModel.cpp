/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridSubModel.h"

#include "measurement/ObjectGrabbers.h"
#include <string>

namespace griddyn {
GridSubModel::GridSubModel(const std::string& objName): GridComponent(objName)
{
    opFlags.set(NO_POWERFLOW_OPERATIONS);
    m_outputSize = 1;
}

void GridSubModel::pFlowInitializeA(CoreTime time, std::uint32_t flags)
{
    GridComponent::pFlowInitializeA(time, flags);
}

void GridSubModel::pFlowInitializeB()
{
    GridComponent::pFlowInitializeB();
}
void GridSubModel::dynInitializeA(CoreTime time, std::uint32_t flags)
{
    if (isEnabled()) {
        dynObjectInitializeA(time, flags);

        auto& solverOffsetsRef = offsets.getOffsets(cLocalSolverMode);
        if (getSubObjects().empty()) {
            solverOffsetsRef.localLoadAll(true);
        } else {
            loadStateSizes(cLocalSolverMode);
        }

        solverOffsetsRef.setOffset(0);
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
        auto stateCount = offsets.local().local.totalSize();
        m_state.resize(stateCount, 0);
        m_dstate_dt.clear();
        m_dstate_dt.resize(stateCount, 0);

        dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        if (updatePeriod < maxTime) {
            enableUpdates();
            setUpdateTime(prevTime + updatePeriod);
            alert(this, UPDATE_REQUIRED);
        }
        opFlags.set(DYN_INITIALIZED);
    }
}

bool GridSubModel::setOutputInitializationTarget(index_t /*outputIndex*/, double /*target*/)
{
    return false;
}

double GridSubModel::get(std::string_view param, units::unit unitType) const
{
    auto fptr = getObjectFunction(this, param);
    if (fptr.first) {
        CoreObject* tobj = const_cast<GridSubModel*>(this);
        return convert(fptr.first(tobj), fptr.second, unitType, systemBasePower);
    }

    return GridComponent::get(param, unitType);
}
}  // namespace griddyn
