/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridSecondary.h"

#include "core/coreObjectTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gridBus.h"
#include "gridSubModel.h"
#include <string>
#include <vector>

namespace griddyn {
static gridBus defBus(1.0, 0);

gridSecondary::gridSecondary(const std::string& objName): gridComponent(objName), bus(&defBus)
{
    m_outputSize = 2;
    m_inputSize = 3;
}

CoreObject* gridSecondary::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<gridSecondary, gridComponent>(this, obj);
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
        auto nobj = findMatchingObject(bus, newRoot);
        if (dynamic_cast<gridBus*>(nobj) != nullptr) {
            bus = static_cast<gridBus*>(nobj);
        }
    }
    gridComponent::updateObjectLinkages(newRoot);
}

void gridSecondary::pFlowInitializeA(coreTime time0, std::uint32_t flags)
{
    bus = static_cast<gridBus*>(getParent()->find("bus"));
    if (bus == nullptr) {
        bus = &defBus;
    }
    gridComponent::pFlowInitializeA(time0, flags);
}

void gridSecondary::pFlowInitializeB()
{
    gridComponent::pFlowInitializeB();
}
void gridSecondary::dynInitializeA(coreTime time0, std::uint32_t flags)
{
    gridComponent::dynInitializeA(time0, flags);
}

void gridSecondary::dynInitializeB(const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    if (isEnabled()) {
        auto ns = stateSize(cLocalSolverMode);
        m_state.resize(ns, 0);
        m_dstate_dt.clear();
        m_dstate_dt.resize(ns, 0);
        dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        if (updatePeriod < maxTime) {
            setUpdateTime(prevTime + updatePeriod);
            enable_updates();
            alert(this, UPDATE_REQUIRED);
        }
        opFlags.set(dyn_initialized);
    }
}

void gridSecondary::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (!getSubObjects().empty()) {
        for (auto& subobj : getSubObjects()) {
            if (dynamic_cast<gridSubModel*>(subobj) != nullptr) {
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
    gridComponent::set(param, val);
}
void gridSecondary::set(std::string_view param, double val, units::unit unitType)
{
    if (!param.empty()) {
        gridComponent::set(param, val, unitType);
    }
}

double gridSecondary::getRealPower(const IOdata& /*inputs*/,
                                   const stateData& /*sD*/,
                                   const solverMode& /*sMode*/) const
{
    return 0.0;
}

double gridSecondary::getReactivePower(const IOdata& /*inputs*/,
                                       const stateData& /*sD*/,
                                       const solverMode& /*sMode*/) const
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
                                const stateData& /*sD*/,
                                const solverMode& /*sMode*/,
                                index_t /*outputNum*/) const
{
    return 0.0;
}

double gridSecondary::getOutput(const IOdata& inputs,
                                const stateData& sD,
                                const solverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum == PoutLocation) {
        return getRealPower(inputs, sD, sMode);
    }
    if (outputNum == QoutLocation) {
        return getReactivePower(inputs, sD, sMode);
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
                                 const stateData& sD,
                                 const solverMode& sMode) const
{
    IOdata out(2);
    out[PoutLocation] = getRealPower(inputs, sD, sMode);
    out[QoutLocation] = getReactivePower(inputs, sD, sMode);
    return out;
}

IOdata gridSecondary::predictOutputs(coreTime /*predictionTime*/,
                                     const IOdata& inputs,
                                     const stateData& sD,
                                     const solverMode& sMode) const
{
    IOdata out(2);
    out[PoutLocation] = getRealPower(inputs, sD, sMode);
    out[QoutLocation] = getReactivePower(inputs, sD, sMode);
    return out;
}

static const std::vector<stringVec> inputNamesStr{
    {"voltage", "v", "volt"},
    {"angle", "theta", "ang", "a"},
    {"frequency", "freq", "f", "omega"},
};

const std::vector<stringVec>& gridSecondary::inputNames() const
{
    return inputNamesStr;
}

static const std::vector<stringVec> outputNamesStr{
    {"p", "power", "realpower", "real"},
    {"q", "reactive", "reactivepower"},
};

const std::vector<stringVec>& gridSecondary::outputNames() const
{
    return outputNamesStr;
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
            return units::puMW;
        case QoutLocation:
            return units::puMW;

        default:
            return units::defunit;
    }
}

}  // namespace griddyn

