/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridSecondary.h"

#include "GridBus.h"
#include "GridSubModel.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <string>
#include <vector>

namespace griddyn {
namespace {
    GridBus& defaultBus()
    {
        static GridBus defaultBusInstance(1.0, 0);
        return defaultBusInstance;
    }

    const std::vector<stringVec>& secondaryInputNames()
    {
        static const std::vector<stringVec> inputNames{
            {"voltage", "v", "volt"},
            {"angle", "theta", "ang", "a"},
            {"frequency", "freq", "f", "omega"},
        };
        return inputNames;
    }

    const std::vector<stringVec>& secondaryOutputNames()
    {
        static const std::vector<stringVec> outputNames{
            {"p", "power", "realpower", "real"},
            {"q", "reactive", "reactivepower"},
        };
        return outputNames;
    }
}  // namespace

gridSecondary::gridSecondary(const std::string& objName): GridComponent(objName), bus(&defaultBus())
{
    m_outputSize = 2;
    m_inputSize = 3;
}

CoreObject* gridSecondary::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<gridSecondary, GridComponent>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->localBaseVoltage = localBaseVoltage;
    nobj->bus = bus;
    return nobj;
}

void gridSecondary::updateObjectLinkages(CoreObject* newRoot)
{
    if (opFlags[pFlow_initialized]) {
        auto* matchedObject = findMatchingObject(bus, newRoot);
        if (dynamic_cast<GridBus*>(matchedObject) != nullptr) {
            bus = static_cast<GridBus*>(matchedObject);
        }
    }
    GridComponent::updateObjectLinkages(newRoot);
}

void gridSecondary::pFlowInitializeA(coreTime time0, std::uint32_t flags)
{
    bus = static_cast<GridBus*>(getParent()->find("bus"));
    if (bus == nullptr) {
        bus = &defaultBus();
    }
    GridComponent::pFlowInitializeA(time0, flags);
}

void gridSecondary::pFlowInitializeB()
{
    GridComponent::pFlowInitializeB();
}
void gridSecondary::dynInitializeA(coreTime time0, std::uint32_t flags)
{
    GridComponent::dynInitializeA(time0, flags);
}

void gridSecondary::dynInitializeB(const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    if (isEnabled()) {
        auto stateCount = stateSize(cLocalSolverMode);
        m_state.resize(stateCount, 0);
        m_dstate_dt.clear();
        m_dstate_dt.resize(stateCount, 0);
        dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        if (updatePeriod < maxTime) {
            setUpdateTime(prevTime + updatePeriod);
            enableUpdates();
            alert(this, UPDATE_REQUIRED);
        }
        opFlags.set(dyn_initialized);
    }
}

void gridSecondary::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (!getSubObjects().empty()) {
        for (const auto& subobj : getSubObjects()) {
            if (subobj == nullptr) {
                continue;
            }
            if (dynamic_cast<GridSubModel*>(subobj) != nullptr) {
                if ((subobj->checkFlag(pflow_init_required)) ||
                    (CHECK_CONTROLFLAG(flags, force_constant_pflow_initialization))) {
                    subobj->pFlowInitializeA(time0, flags);
                }
            } else {
                subobj->pFlowInitializeA(time0, flags);
            }
        }
    }
    prevTime = time0;
}

void gridSecondary::set(std::string_view param, std::string_view val)
{
    GridComponent::set(param, val);
}
void gridSecondary::set(std::string_view param, double val, units::unit unitType)
{
    if (!param.empty()) {
        GridComponent::set(param, val, unitType);
    }
}

double gridSecondary::getRealPower(const IOdata& /*inputs*/,
                                   const StateData& /*stateDataValue*/,
                                   const SolverMode& /*sMode*/) const
{
    return 0.0;
}

double gridSecondary::getReactivePower(const IOdata& /*inputs*/,
                                       const StateData& /*stateDataValue*/,
                                       const SolverMode& /*sMode*/) const
{
    return 0.0;
}

double gridSecondary::getRealPower() const
{
    return 0.0;
}
double gridSecondary::getReactivePower() const
{
    return 0.0;
}
double gridSecondary::getAdjustableCapacityUp(coreTime /*time*/) const
{
    return 0.0;
}
double gridSecondary::getAdjustableCapacityDown(coreTime /*time*/) const
{
    return 0.0;
}
double gridSecondary::getDoutdt(const IOdata& /*inputs*/,
                                const StateData& /*stateDataValue*/,
                                const SolverMode& /*sMode*/,
                                index_t /*outputNum*/) const
{
    return 0.0;
}

double gridSecondary::getOutput(const IOdata& inputs,
                                const StateData& stateDataValue,
                                const SolverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum == PoutLocation) {
        return getRealPower(inputs, stateDataValue, sMode);
    }
    if (outputNum == QoutLocation) {
        return getReactivePower(inputs, stateDataValue, sMode);
    }
    return kNullVal;
}

double gridSecondary::getOutput(index_t outputNum) const
{
    if (outputNum == PoutLocation) {
        return getRealPower();
    }
    if (outputNum == QoutLocation) {
        return getReactivePower();
    }
    return kNullVal;
}

IOdata gridSecondary::getOutputs(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode) const
{
    IOdata out(2);
    out[PoutLocation] = getRealPower(inputs, stateDataValue, sMode);
    out[QoutLocation] = getReactivePower(inputs, stateDataValue, sMode);
    return out;
}

IOdata gridSecondary::predictOutputs(coreTime /*predictionTime*/,
                                     const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     const SolverMode& sMode) const
{
    IOdata out(2);
    out[PoutLocation] = getRealPower(inputs, stateDataValue, sMode);
    out[QoutLocation] = getReactivePower(inputs, stateDataValue, sMode);
    return out;
}

const std::vector<stringVec>& gridSecondary::inputNames() const
{
    return secondaryInputNames();
}

const std::vector<stringVec>& gridSecondary::outputNames() const
{
    return secondaryOutputNames();
}

units::unit gridSecondary::inputUnits(index_t inputNum) const
{
    switch (inputNum) {
        case voltageInLocation:
            return units::puV;
        case angleInLocation:
            return units::rad;
        case frequencyInLocation:
            return units::puHz;
        default:
            return units::defunit;
    }
}

units::unit gridSecondary::outputUnits(index_t outputNum) const
{
    switch (outputNum) {
        case PoutLocation:
        case QoutLocation:
            return units::puMW;

        default:
            return units::defunit;
    }
}

}  // namespace griddyn
