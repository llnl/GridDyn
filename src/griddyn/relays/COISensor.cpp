/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "COISensor.h"

#include "../GenModel.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../generators/DynamicGenerator.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <functional>
#include <string>

namespace griddyn {
COISensor::COISensor(const std::string& name): Sensor(name)
{
    m_outputSize = 2;
    outputStrings = {{"frequency", "omega"}, {"angle", "delta"}};
}

CoreObject* COISensor::clone(CoreObject* obj) const
{
    auto* result = cloneBase<COISensor, Sensor>(this, obj);
    if (result != nullptr) {
        result->machines.clear();
    }
    return result == nullptr ? obj : result;
}

void COISensor::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!filterBlocks.empty() || !inputStrings.empty() || !dataSources.empty()) {
        throw InvalidParameterValue(
            "COI measurement does not accept generic filter or input blocks");
    }
    auto* owner = dynamic_cast<GridArea*>(getParent());
    if (owner == nullptr) {
        throw InvalidParameterValue("COI measurement requires an owning area");
    }

    machines.clear();
    const double systemBase = owner->basePower();
    if (!std::isfinite(systemBase) || systemBase <= 0.0) {
        throw InvalidParameterValue("COI measurement requires a positive area power base");
    }
    double totalInertia = 0.0;
    std::function<void(GridArea*)> addAreaMachines = [&](GridArea* area) {
        for (index_t busIndex = 0; area->getBus(busIndex) != nullptr; ++busIndex) {
            auto* bus = area->getBus(busIndex);
            if (!bus->isEnabled()) {
                continue;
            }
            for (index_t generatorIndex = 0; bus->getGen(generatorIndex) != nullptr;
                 ++generatorIndex) {
                auto* generator = dynamic_cast<DynamicGenerator*>(bus->getGen(generatorIndex));
                if (generator == nullptr || !generator->isEnabled()) {
                    continue;
                }
                auto* model = generator->find("genmodel");
                if (model == nullptr) {
                    throw InvalidParameterValue("COI dynamic generator has no generator model");
                }
                const double inertia = model->get("h");
                if (!std::isfinite(inertia) || inertia <= 0.0) {
                    continue;
                }
                const double machineBase = generator->get("mbase", units::MVAR);
                if (!std::isfinite(machineBase) || machineBase <= 0.0) {
                    throw InvalidParameterValue("COI generator MBASE must be positive and finite");
                }
                const double weight = inertia * machineBase / systemBase;
                machines.push_back({generator, weight});
                totalInertia += weight;
            }
        }
        for (index_t areaIndex = 0; area->getArea(areaIndex) != nullptr; ++areaIndex) {
            auto* child = area->getArea(areaIndex);
            if (child->isEnabled()) {
                addAreaMachines(child);
            }
        }
    };
    addAreaMachines(owner);
    if (machines.empty() || !std::isfinite(totalInertia) || totalInertia <= 0.0) {
        throw InvalidParameterValue(
            "COI measurement requires an enabled synchronous dynamic generator with positive inertia");
    }
    for (auto& machine : machines) {
        machine.weight /= totalInertia;
    }

    Sensor::dynObjectInitializeA(time0, flags);
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = 0;
    local.jacSize = 2 * (machines.size() + 1);
    m_state.assign(2, 0.0);
    m_dstate_dt.assign(2, 0.0);
}

void COISensor::dynObjectInitializeB(const IOdata& /*inputs*/,
                                     const IOdata& /*desiredOutput*/,
                                     IOdata& fieldSet)
{
    m_state[0] = 1.0;
    m_state[1] = 0.0;
    fieldSet.resize(m_outputSize);
    fieldSet[0] = m_state[0];
    fieldSet[1] = m_state[1];
}

double COISensor::getOutput(const IOdata& /*inputs*/,
                            const StateData& stateData,
                            const SolverMode& sMode,
                            index_t outNum) const
{
    if (outNum >= m_outputSize) {
        return kNullVal;
    }
    if (stateData.state == nullptr || isLocal(sMode)) {
        return m_state[outNum];
    }
    if (hasAlgebraic(sMode)) {
        return stateData.state[offsets.getAlgOffset(sMode) + outNum];
    }
    if (stateData.algState != nullptr && sMode.pairedOffsetIndex != kNullLocation) {
        const auto& paired = offsets.getSolverMode(sMode.pairedOffsetIndex);
        return stateData.algState[offsets.getAlgOffset(paired) + outNum];
    }
    return m_state[outNum];
}

double COISensor::getOutput(index_t outNum) const
{
    return outNum < m_state.size() ? m_state[outNum] : kNullVal;
}

index_t COISensor::getOutputLoc(const SolverMode& sMode, index_t outNum) const
{
    if (outNum >= m_outputSize || !hasAlgebraic(sMode)) {
        return kNullLocation;
    }
    return offsets.getAlgOffset(sMode) + outNum;
}

void COISensor::outputPartialDerivatives(const IOdata& /*inputs*/,
                                         const StateData& /*stateData*/,
                                         MatrixData<double>& matrixData,
                                         const SolverMode& sMode)
{
    for (index_t output = 0; output < m_outputSize; ++output) {
        matrixData.assignCheckCol(output, getOutputLoc(sMode, output), 1.0);
    }
}

void COISensor::residual(const IOdata& /*inputs*/,
                         const StateData& stateData,
                         double resid[],
                         const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    for (index_t output = 0; output < m_outputSize; ++output) {
        loc.destLoc[output] = loc.algStateLoc[output];
    }
    for (const auto& machine : machines) {
        loc.destLoc[0] -= machine.weight * machine.generator->getFreq(stateData, sMode);
        loc.destLoc[1] -= machine.weight * machine.generator->getAngle(stateData, sMode);
    }
}

void COISensor::algebraicUpdate(const IOdata& /*inputs*/,
                                const StateData& stateData,
                                double update[],
                                const SolverMode& sMode,
                                double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = 0.0;
    loc.destLoc[1] = 0.0;
    for (const auto& machine : machines) {
        loc.destLoc[0] += machine.weight * machine.generator->getFreq(stateData, sMode);
        loc.destLoc[1] += machine.weight * machine.generator->getAngle(stateData, sMode);
    }
}

void COISensor::jacobianElements(const IOdata& /*inputs*/,
                                 const StateData& /*stateData*/,
                                 MatrixData<double>& matrixData,
                                 const IOlocs& /*inputLocs*/,
                                 const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto firstRow = offsets.getAlgOffset(sMode);
    matrixData.assign(firstRow, firstRow, 1.0);
    matrixData.assign(firstRow + 1, firstRow + 1, 1.0);
    if (!hasDifferential(sMode)) {
        return;
    }
    for (const auto& machine : machines) {
        index_t frequencyColumn = kNullLocation;
        index_t angleColumn = kNullLocation;
        machine.generator->getFreq({}, sMode, &frequencyColumn);
        machine.generator->getAngle({}, sMode, &angleColumn);
        matrixData.assignCheckCol(firstRow, frequencyColumn, -machine.weight);
        matrixData.assignCheckCol(firstRow + 1, angleColumn, -machine.weight);
    }
}

stringVec COISensor::localStateNames() const
{
    return {"frequency", "angle"};
}

}  // namespace griddyn
