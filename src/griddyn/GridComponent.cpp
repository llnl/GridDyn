/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridComponent.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::ensureSizeAtLeast;

GridComponent::GridComponent(const std::string& objName): CoreObject(objName)
{
    offsets.setAlgOffset(0, cLocalSolverMode);
}

GridComponent::~GridComponent()
{
    for (auto& subObject : subObjectList) {
        removeReference(subObject, this);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
CoreObject* GridComponent::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<GridComponent, CoreObject>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->m_inputSize = m_inputSize;
    nobj->m_outputSize = m_outputSize;
    nobj->opFlags = opFlags;
    nobj->systemBaseFrequency = systemBaseFrequency;
    nobj->systemBasePower = systemBasePower;
    nobj->localBaseVoltage = localBaseVoltage;
    if (nobj->subObjectList.empty()) {
        for (const auto& subobj : subObjectList) {
            try {
                nobj->add(subobj->clone());
            }
            catch (const UnrecognizedObjectException&) {
                continue;
            }
        }
    } else {
        auto csz = nobj->subObjectList.size();
        // clone the subObjects
        for (size_t ii = 0; ii < subObjectList.size(); ++ii) {
            if (subObjectList[ii]->locIndex != kNullLocation) {
                bool fnd = false;
                for (size_t kk = 0; kk < csz; ++kk) {
                    if (nobj->subObjectList[kk]->locIndex == subObjectList[ii]->locIndex) {
                        if (typeid(nobj->subObjectList[kk]) ==
                            typeid(
                                subObjectList[ii]))  // make sure the types are same before cloning
                        {
                            subObjectList[ii]->clone(nobj->subObjectList[kk]);
                            fnd = true;
                            break;
                        }
                    }
                }
                if (!fnd) {
                    try {
                        nobj->add(subObjectList[ii]->clone());
                    }
                    catch (const UnrecognizedObjectException&) {
                        continue;
                    }
                }
            } else {
                if (ii >= csz) {
                    try {
                        nobj->add(subObjectList[ii]->clone());
                    }
                    catch (const UnrecognizedObjectException&) {
                        continue;
                    }
                } else {
                    if (typeid(subObjectList[ii]) == typeid(nobj->subObjectList[ii])) {
                        subObjectList[ii]->clone(nobj->subObjectList[ii]);
                    } else {
                        try {
                            nobj->add(subObjectList[ii]->clone());
                        }
                        catch (const UnrecognizedObjectException&) {
                            continue;
                        }
                    }
                }
            }
        }
    }

    return nobj;
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::updateObjectLinkages(CoreObject* newRoot)
{
    for (auto* subobj : getSubObjects()) {
        subobj->updateObjectLinkages(newRoot);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::pFlowInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (localBaseVoltage == kNullVal) {
        if (isRoot()) {  // NOLINT
            localBaseVoltage = 120000.0;
        } else if (dynamic_cast<GridComponent*>(getParent()) != nullptr) {
            localBaseVoltage = static_cast<GridComponent*>(getParent())->localBaseVoltage;
        } else {
            localBaseVoltage = 120000.0;
        }
    }
    if (isEnabled()) {
        pFlowObjectInitializeA(time0, flags);
        prevTime = time0;
        updateFlags(false);
        setupPFlowFlags();
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::pFlowInitializeB()
{
    if (isEnabled()) {
        pFlowObjectInitializeB();
        opFlags.set(POWERFLOW_INITIALIZED);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::dynInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (isEnabled()) {
        dynObjectInitializeA(time0, flags);
        prevTime = time0;
        updateFlags(true);
        setupDynFlags();
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::dynInitializeB(const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    if (isEnabled()) {
        dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        if (updatePeriod < maxTime) {
            setUpdateTime(prevTime + updatePeriod);
            enableUpdates();
        }
        opFlags.set(DYN_INITIALIZED);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    for (auto& subobj : subObjectList) {
        subobj->pFlowInitializeA(time0, flags);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::pFlowObjectInitializeB()
{
    for (auto& subobj : subObjectList) {
        subobj->pFlowInitializeB();
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    for (auto& subobj : subObjectList) {
        subobj->dynInitializeA(time0, flags);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    for (auto& subobj : subObjectList) {
        if (!subobj->checkFlag(SEPARATE_PROCESSING)) {
            subobj->dynInitializeB(inputs, desiredOutput, fieldSet);
        }
    }
}

count_t GridComponent::stateSize(const SolverMode& sMode)
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    count_t ssize = (hasAlgebraic(sMode)) ?
        (solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
         solverOffsetsValue.total.aSize) :
        0;
    if (hasDifferential(sMode)) {
        ssize += solverOffsetsValue.total.diffSize;
    }
    return ssize;
}

count_t GridComponent::stateSize(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    count_t ssize = (hasAlgebraic(sMode)) ?
        (solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
         solverOffsetsValue.total.aSize) :
        0;
    if (hasDifferential(sMode)) {
        ssize += solverOffsetsValue.total.diffSize;
    }
    return ssize;
}

count_t GridComponent::totalAlgSize(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
        solverOffsetsValue.total.aSize;
}

count_t GridComponent::totalAlgSize(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return (solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
            solverOffsetsValue.total.aSize);
}

count_t GridComponent::algSize(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.algSize;
}

count_t GridComponent::algSize(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.algSize;
}

count_t GridComponent::diffSize(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.diffSize;
}

count_t GridComponent::diffSize(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.diffSize;
}

count_t GridComponent::rootSize(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.rootsLoaded)) {
        loadRootSizes(sMode);
    }
    return solverOffsetsValue.total.algRoots + solverOffsetsValue.total.diffRoots;
}

count_t GridComponent::rootSize(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);

    return solverOffsetsValue.total.algRoots + solverOffsetsValue.total.diffRoots;
}

count_t GridComponent::jacSize(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.jacobianLoaded)) {
        loadJacobianSizes(sMode);
    }
    return solverOffsetsValue.total.jacSize;
}

count_t GridComponent::jacSize(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.jacSize;
}

count_t GridComponent::voltageStateCount(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.vSize;
}

count_t GridComponent::voltageStateCount(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.vSize;
}

count_t GridComponent::angleStateCount(const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.aSize;
}

count_t GridComponent::angleStateCount(const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.aSize;
}

const SolverOffsets& GridComponent::getOffsets(const SolverMode& sMode) const
{
    return offsets.getOffsets(sMode);
}
// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::setOffsets(const SolverOffsets& newOffsets, const SolverMode& sMode)
{
    offsets.setOffsets(newOffsets, sMode);

    if (!subObjectList.empty()) {
        SolverOffsets newSubOffsets(newOffsets);
        newSubOffsets.localIncrement(offsets.getOffsets(sMode));
        for (auto& subobj : subObjectList) {
            if (subobj->isEnabled()) {
                subobj->setOffsets(newSubOffsets, sMode);
                newSubOffsets.increment(subobj->offsets.getOffsets(sMode));
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::setOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!subObjectList.empty()) {
        for (auto& subobj : subObjectList) {
            if (subobj->isEnabled()) {
                subobj->setOffset(newOffset, sMode);
                newOffset += subobj->stateSize(sMode);
            }
        }
    }
    offsets.setOffset(newOffset, sMode);
}

bool GridComponent::isStateCountLoaded(const SolverMode& sMode) const
{
    return offsets.isStateCountLoaded(sMode);
}

bool GridComponent::isJacobianCountLoaded(const SolverMode& sMode) const
{
    return offsets.isJacobianCountLoaded(sMode);
}

bool GridComponent::isRootCountLoaded(const SolverMode& sMode) const
{
    return offsets.isRootCountLoaded(sMode);
}

static const auto& userSettableFlags()
{
    static const std::map<std::string_view, OperationFlags, std::less<>> flags{
        {"use_bus_frequency", USES_BUS_FREQUENCY},
        {"LATE_B_INITIALIZE", LATE_B_INITIALIZE},
        {"error", ERROR_FLAG},
        {"no_gridcomponent_set", NO_GRIDCOMPONENT_SET},
        {"disable_flag_update", DISABLE_FLAG_UPDATES},
        {"flag_update_required", FLAG_UPDATE_REQUIRED},
        {"pflow_init_required", PFLOW_INIT_REQUIRED},
        {"SAMPLED_ONLY", NO_DYNAMICS},
    };
    return flags;
}

// there isn't that many flags that we want to be user settable, most are controlled by the model so
// allowing them to be set by an external function might not be the best thing
void GridComponent::setFlag(std::string_view flag, bool val)
{
    const auto& flags = userSettableFlags();
    auto ffind = flags.find(flag);
    if (ffind != flags.end()) {
        opFlags.set(ffind->second, val);
        if (flag == "SAMPLED_ONLY") {
            if (opFlags[POWERFLOW_INITIALIZED]) {
                offsets.unload();
            }
        }
    } else if (flag == "connected") {
        if (val) {
            if (!isConnected()) {
                reconnect();
            }
        } else if (isConnected()) {
            disconnect();
        }
    } else if (flag == "disconnected") {
        if (val) {
            if (isConnected()) {
                disconnect();
            }
        } else if (!isConnected()) {
            reconnect();
        }
    } else if (subObjectSet(flag, val)) {
        return;
    } else {
        CoreObject::setFlag(flag, val);
    }
}

static const auto& parentSettableFlags()
{
    static const std::vector<index_t> flags{
        SAMPLED_ONLY,
        NO_GRIDCOMPONENT_SET,
        SEPARATE_PROCESSING,
    };
    return flags;
}

void GridComponent::parentSetFlag(index_t flagID, bool val, CoreObject* checkParent)
{
    if (isSameObject(getParent(), checkParent)) {
        const auto& flags = parentSettableFlags();
        if (std::binary_search(flags.begin(), flags.end(), flagID)) {
            opFlags[flagID] = val;
        }
    }
}

static const auto& flagMap()
{
    static const std::map<std::string_view, OperationFlags, std::less<>> flags{
        {"constraints", HAS_CONSTRAINTS},
        {"roots", HAS_ROOTS},
        {"alg_roots", HAS_ALG_ROOTS},
        {"voltage_adjustments", HAS_POWERFLOW_ADJUSTMENTS},
        {"preex", PRE_EX_REQUESTED},
        {"use_bus_frequency", USES_BUS_FREQUENCY},
        {"pflow_states", HAS_PFLOW_STATES},
        {"dyn_states", HAS_DYN_STATES},
        {"differential_states", HAS_DIFFERENTIAL_STATES},
        {"not_cloneable", NOT_CLONEABLE},
        {"remote_voltage_control", REMOTE_VOLTAGE_CONTROL},
        {"local_voltage_control", LOCAL_VOLTAGE_CONTROL},
        {"indirect_voltage_control", INDIRECT_VOLTAGE_CONTROL},
        {"remote_power_control", REMOTE_POWER_CONTROL},
        {"local_power_control", LOCAL_POWER_CONTROL},
        {"indirect_power_control", INDIRECT_POWER_CONTROL},
        {"pflow_initialized", POWERFLOW_INITIALIZED},
        {"powerflow_initialized", POWERFLOW_INITIALIZED},
        {"dyn_initialized", DYN_INITIALIZED},
        {"armed", OBJECT_ARMED_FLAG},
        {"late_b_initialize", LATE_B_INITIALIZE},
        {"object_flag1", OBJECT_FLAG1},
        {"object_flag2", OBJECT_FLAG2},
        {"object_flag3", OBJECT_FLAG3},
        {"object_flag4", OBJECT_FLAG4},
        {"object_flag5", OBJECT_FLAG5},
        {"object_flag6", OBJECT_FLAG6},
        {"object_flag7", OBJECT_FLAG7},
        {"object_flag8", OBJECT_FLAG8},
        {"object_flag9", OBJECT_FLAG9},
        {"object_flag10", OBJECT_FLAG10},
        {"object_flag11", OBJECT_FLAG11},
        {"object_flag12", OBJECT_FLAG12},
        {"state_change", STATE_CHANGE_FLAG},
        {"object_change", OBJECT_CHANGE_FLAG},
        {"constraint_change", CONSTRAINT_CHANGE_FLAG},
        {"root_change", ROOT_CHANGE_FLAG},
        {"jacobian_count_change", JACOBIAN_COUNT_CHANGE_FLAG},
        {"slack_bus_change", static_cast<OperationFlags>(29)},
        {"voltage_control_change", static_cast<OperationFlags>(30)},
        {"error", ERROR_FLAG},
        {"connectivity_change", CONNECTIVITY_CHANGE_FLAG},
        {"no_powerflow_operations", NO_POWERFLOW_OPERATIONS},
        {"disconnected", DISCONNECTED},
        {"no_dynamics", NO_DYNAMICS},
        {"sampled_only", NO_DYNAMICS},
        {"disable_flag_update", DISABLE_FLAG_UPDATES},
        {"flag_update_required", FLAG_UPDATE_REQUIRED},
        {"differential_output", DIFFERENTIAL_OUTPUT},
        {"multipart_calculation_capable", MULTIPART_CALCULATION_CAPABLE},
        {"pflow_init_required", PFLOW_INIT_REQUIRED},
        {"dc_only", DC_ONLY},
        {"dc_capable", DC_CAPABLE},
        {"dc_terminal2", DC_TERMINAL2},
        {"separate_processing", SEPARATE_PROCESSING},
        {"three_phase_only", THREE_PHASE_ONLY},
        {"three_phase_capable", THREE_PHASE_CAPABLE},
        {"three_phase_terminal2", THREE_PHASE_TERMINAL2},
    };
    return flags;
}

bool GridComponent::getFlag(std::string_view flag) const
{
    const auto& flags = flagMap();
    auto flagfind = flags.find(flag);
    if (flagfind != flags.end()) {
        return opFlags[flagfind->second];
    }
    return CoreObject::getFlag(flag);
}

bool GridComponent::checkFlag(index_t flagID) const
{
    return opFlags.test(flagID);
}
bool GridComponent::hasStates(const SolverMode& sMode) const
{
    return (stateSize(sMode) > 0);
}
bool GridComponent::isArmed() const
{
    return opFlags[OBJECT_ARMED_FLAG];
}
bool GridComponent::isCloneable() const
{
    return !opFlags[NOT_CLONEABLE];
}
bool GridComponent::isConnected() const
{
    return !(opFlags[DISCONNECTED]);
}
void GridComponent::reconnect()
{
    opFlags.set(DISCONNECTED, false);
}
void GridComponent::disconnect()
{
    opFlags.set(DISCONNECTED);
}
static constexpr auto localNumericStrings =
    std::array<std::string_view, 3>{"status", "basefrequency", "basepower"};

static constexpr auto localStringStrings = std::array<std::string_view, 1>{"status"};

static constexpr std::array<std::string_view, 0> localFlagStrings{};

void GridComponent::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<GridComponent, CoreObject>(
        this, pstr, localNumericStrings, localStringStrings, localFlagStrings, pstype);
}

void GridComponent::set(std::string_view param, std::string_view val)
{
    if (opFlags[NO_GRIDCOMPONENT_SET]) {
        throw(UnrecognizedParameter(param));
    }

    if (param == "status") {
        auto valueLower = convertToLowerCase(std::string{val});
        if ((valueLower == "on") || (valueLower == "in") || (valueLower == "enabled")) {
            if (!isEnabled()) {
                enable();
                if ((opFlags[HAS_PFLOW_STATES]) || (opFlags[HAS_DYN_STATES])) {
                    alert(this, STATE_COUNT_CHANGE);
                }
            }
        } else if ((valueLower == "off") || (valueLower == "out") || (valueLower == "disabled")) {
            if (isEnabled()) {
                if ((opFlags[HAS_PFLOW_STATES]) || (opFlags[HAS_DYN_STATES])) {
                    alert(this, STATE_COUNT_CHANGE);
                }
                disable();
            }
        } else if (valueLower == "connected") {
            if (!isConnected()) {
                reconnect();
            }
        } else if (valueLower == "disconnected") {
            if (isConnected()) {
                disconnect();
            }
        }
    } else if (param == "flags") {
        setMultipleFlags(this, val);
    } else if (subObjectSet(param, val)) {
        return;
    } else {
        CoreObject::set(param, val);
    }
}

static auto hasParameterPath(std::string_view param)
{
    return (param.find_last_of(":?") != std::string_view::npos);
}
bool GridComponent::subObjectSet(std::string_view param, double val, units::unit unitType)
{
    if (hasParameterPath(param)) {
        const ObjectInfo pinfo(std::string{param}, this);
        if (pinfo.mObject != nullptr) {
            if (pinfo.mUnitType != units::defunit) {
                pinfo.mObject->set(pinfo.mField, val, pinfo.mUnitType);
            } else {
                pinfo.mObject->set(pinfo.mField, val, unitType);
            }
            return true;
        }
        throw(UnrecognizedParameter(param));
    }
    return false;
}

bool GridComponent::subObjectSet(std::string_view param, std::string_view val)
{
    if (hasParameterPath(param)) {
        const ObjectInfo pinfo(std::string{param}, this);
        if (pinfo.mObject != nullptr) {
            pinfo.mObject->set(pinfo.mField, val);
        } else {
            throw(UnrecognizedParameter(param));
        }
        return true;
    }
    return false;
}

bool GridComponent::subObjectSet(std::string_view flag, bool val)
{
    if (hasParameterPath(flag)) {
        const ObjectInfo pinfo(std::string{flag}, this);
        if (pinfo.mObject != nullptr) {
            pinfo.mObject->setFlag(pinfo.mField, val);
        } else {
            throw(UnrecognizedParameter(flag));
        }
        return true;
    }
    return false;
}

double GridComponent::subObjectGet(std::string_view param, units::unit unitType) const
{
    if (hasParameterPath(param)) {
        const ObjectInfo pinfo(std::string{param}, this);
        if (pinfo.mObject != nullptr) {
            if (pinfo.mUnitType != units::defunit) {
                return pinfo.mObject->get(pinfo.mField, pinfo.mUnitType);
            }
            return pinfo.mObject->get(pinfo.mField, unitType);
        }
        throw(UnrecognizedParameter(param));
    }
    return kNullVal;
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::set(std::string_view param, double val, units::unit unitType)
{
    if (opFlags[NO_GRIDCOMPONENT_SET]) {
        throw(UnrecognizedParameter(param));
    }

    if ((param == "enabled") || (param == "status")) {
        if (val > 0.1) {
            if (!isEnabled()) {
                enable();
                if (opFlags[HAS_DYN_STATES]) {
                    alert(this, STATE_COUNT_CHANGE);
                }
            }
        } else {
            if (isEnabled()) {
                if (opFlags[HAS_DYN_STATES]) {
                    alert(this, STATE_COUNT_CHANGE);
                }
                disable();
            }
        }
    } else if (param == "connected") {
        if (val > 0.1) {
            if (!isConnected()) {
                reconnect();
            }
        } else {
            if (isConnected()) {
                disconnect();
            }
        }
    } else if ((param == "basepower") || (param == "basemw") || (param == "basemva")) {
        systemBasePower = units::convert(val, unitType, units::MW);
        setAll("all", "basepower", systemBasePower);
    } else if ((param == "basevoltage") || (param == "vbase") || (param == "voltagebase") ||
               (param == "basev") || (param == "bv") || (param == "base voltage")) {
        localBaseVoltage = units::convert(val, unitType, units::V);
    } else if ((param == "basefreq") || (param == "basefrequency") ||
               (param == "systembasefrequency")) {
        systemBaseFrequency = units::convert(val, unitType, units::rad / units::s);
        setAll("all", "basefreq", systemBasePower);
    } else if (subObjectSet(param, val, unitType)) {
        return;
    } else {
        CoreObject::set(param, val, unitType);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::setAll(std::string_view type,
                           std::string_view param,
                           double val,
                           units::unit unitType)
{
    if ((type == "all") || (type == "sub") || (type == "object")) {
        for (auto& subobj : subObjectList) {
            subobj->set(param, val, unitType);
        }
    }
}

double GridComponent::get(std::string_view param, units::unit unitType) const
{
    double out = kNullVal;
    if (param == "basepower") {
        out = units::convert(systemBasePower, units::MVAR, unitType, systemBasePower);
    } else if (param == "subobjectcount") {
        out = static_cast<double>(subObjectList.size());
    } else if (param == "basefrequency") {
        out = units::convert(systemBaseFrequency, units::rad / units::s, unitType);
    } else if (param == "basevoltage") {
        out = units::convert(localBaseVoltage, units::kV, unitType);
    } else if (param == "jacsize") {
        if (opFlags[DYN_INITIALIZED]) {
            out = jacSize(cDaeSolverMode);
        } else {
            out = jacSize(cPflowSolverMode);
        }
    } else if (param == "statesize") {
        if (opFlags[DYN_INITIALIZED]) {
            out = stateSize(cDaeSolverMode);
        } else {
            out = stateSize(cPflowSolverMode);
        }
    } else if (param == "algsize") {
        if (opFlags[DYN_INITIALIZED]) {
            out = algSize(cDaeSolverMode);
        } else {
            out = algSize(cPflowSolverMode);
        }
    } else if (param == "diffsize") {
        out = diffSize(cDaeSolverMode);
    } else if (param == "rootsize") {
        out = rootSize(cDaeSolverMode);
    } else {
        out = subObjectGet(param, unitType);
        if (out == kNullVal) {
            const auto& outNames = outputNames();
            for (index_t ii = 0; ii < m_outputSize; ++ii) {
                for (const auto& oname : outNames[ii]) {
                    if (oname == param) {
                        return units::convert(getOutput(ii),
                                              outputUnits(ii),
                                              unitType,
                                              systemBasePower);
                    }
                }
            }
            out = CoreObject::get(param, unitType);
        }
    }
    return out;
}

void GridComponent::addSubObject(GridComponent* comp)
{
    if (comp == nullptr) {
        return;
    }
    if (std::any_of(subObjectList.begin(),
                    subObjectList.end(),
                    [comp](const CoreObject* subObject) {
                        return isSameObject(subObject, comp);
                    })) {
        return;
    }
    comp->setParent(this);
    comp->addOwningReference();
    comp->systemBaseFrequency = systemBaseFrequency;
    comp->systemBasePower = systemBasePower;
    subObjectList.push_back(comp);
    if (opFlags[POWERFLOW_INITIALIZED]) {
        offsets.unload(true);
        alert(this, OBJECT_COUNT_INCREASE);
        opFlags[DYN_INITIALIZED] = false;
        opFlags[POWERFLOW_INITIALIZED] = false;
    }
}

void GridComponent::removeSubObject(GridComponent* obj)
{
    if (!subObjectList.empty()) {
        auto rmobj =
            std::find_if(subObjectList.begin(), subObjectList.end(), [obj](CoreObject* subObject) {
                return isSameObject(subObject, obj);
            });
        if (rmobj != subObjectList.end()) {
            removeReference(*rmobj, this);
            subObjectList.erase(rmobj);
            if (opFlags[POWERFLOW_INITIALIZED]) {
                offsets.unload(true);
                alert(this, OBJECT_COUNT_DECREASE);
            }
        }
    }
}

void GridComponent::replaceSubObject(GridComponent* newObj, GridComponent* oldObj)
{
    if (subObjectList.empty()) {
        addSubObject(newObj);
        return;
    }
    if (newObj == nullptr) {
        removeSubObject(oldObj);
        return;
    }
    auto repobj = std::find_if(subObjectList.begin(),
                               subObjectList.end(),
                               [oldObj](const CoreObject* subObject) {
                                   return isSameObject(subObject, oldObj);
                               });
    if (repobj != subObjectList.end()) {
        removeReference(*repobj, this);
        newObj->setParent(this);
        newObj->addOwningReference();
        newObj->systemBaseFrequency = systemBaseFrequency;
        newObj->systemBasePower = systemBasePower;
        *repobj = newObj;
        if (opFlags[POWERFLOW_INITIALIZED]) {
            offsets.unload(true);
            alert(this, OBJECT_COUNT_CHANGE);
            opFlags[DYN_INITIALIZED] = false;
            opFlags[POWERFLOW_INITIALIZED] = false;
        }
    } else {
        addSubObject(newObj);
        return;
    }
}
void GridComponent::remove(CoreObject* obj)
{
    if (dynamic_cast<GridComponent*>(obj) != nullptr) {
        removeSubObject(static_cast<GridComponent*>(obj));
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::reset(ResetLevels level)
{
    for (auto& subobj : subObjectList) {
        subobj->reset(level);
    }
}

// NOLINTBEGIN(misc-no-recursion)
ChangeCode
    GridComponent::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, CheckLevel level)
{
    auto ret = ChangeCode::NO_CHANGE;

    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(HAS_POWERFLOW_ADJUSTMENTS))) {
            continue;
        }
        const auto iret = subobj->powerFlowAdjust(inputs, flags, level);
        ret = std::max(iret, ret);
    }
    return ret;
}
// NOLINTEND(misc-no-recursion)

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::setState(CoreTime time,
                             const double state[],
                             const double dstateDt[],
                             const SolverMode& sMode)
{
    prevTime = time;
    if (!hasStates(sMode))  // use the const version of stateSize
    {
        return;
    }
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    const auto& localStates =
        (subObjectList.empty()) ? (solverOffsetsValue.total) : (solverOffsetsValue.local);

    if (hasAlgebraic(sMode)) {
        if (localStates.algSize > 0) {
            std::copy(state + solverOffsetsValue.algOffset,
                      state + solverOffsetsValue.algOffset + localStates.algSize,
                      m_state.data());
        }
    }
    if (localStates.diffSize > 0) {
        if (isDifferentialOnly(sMode)) {
            std::copy(state + solverOffsetsValue.diffOffset,
                      state + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_state.data() + algSize(cLocalSolverMode));
            std::copy(dstateDt + solverOffsetsValue.diffOffset,
                      dstateDt + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_dstate_dt.data() + algSize(cLocalSolverMode));
        } else {
            std::copy(state + solverOffsetsValue.diffOffset,
                      state + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_state.data() + localStates.algSize);
            std::copy(dstateDt + solverOffsetsValue.diffOffset,
                      dstateDt + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_dstate_dt.data() + localStates.algSize);
        }
    }

    for (auto& sub : subObjectList) {
        sub->setState(time, state, dstateDt, sMode);
    }
}
// for saving the state
// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::guessState(CoreTime time,
                               double state[],
                               double dstateDt[],
                               const SolverMode& sMode)
{
    if (!hasStates(sMode)) {
        return;
    }
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    const auto& localStates =
        (subObjectList.empty()) ? (solverOffsetsValue.total) : (solverOffsetsValue.local);

    if (hasAlgebraic(sMode)) {
        if (localStates.algSize > 0) {
            assert(solverOffsetsValue.algOffset != kNullLocation);
            std::copy(m_state.begin(),
                      m_state.begin() + localStates.algSize,
                      state + solverOffsetsValue.algOffset);
        }
    }
    if (localStates.diffSize > 0) {
        if (isDifferentialOnly(sMode)) {
            assert(solverOffsetsValue.diffOffset != kNullLocation);
            const index_t localAlgSize = algSize(cLocalSolverMode);
            std::copy(m_state.begin() + localAlgSize,
                      m_state.begin() + localAlgSize + localStates.diffSize,
                      state + solverOffsetsValue.diffOffset);
            std::copy(m_dstate_dt.data() + localAlgSize,
                      m_dstate_dt.data() + localAlgSize + localStates.diffSize,
                      dstateDt + solverOffsetsValue.diffOffset);
        } else {
            if (solverOffsetsValue.diffOffset == kNullLocation) {
                std::cout << std::format("{}::{} in mode {} {} ds={}, do={}\n",
                                         getParent()->getName(),
                                         getName(),
                                         static_cast<int>(isLocal(sMode)),
                                         static_cast<int>(isDAE(sMode)),
                                         static_cast<int>(solverOffsetsValue.total.diffSize),
                                         static_cast<int>(solverOffsetsValue.diffOffset));
                // printStackTrace ();
            }
            assert(solverOffsetsValue.diffOffset != kNullLocation);
            const count_t stateCount = localStates.algSize + localStates.diffSize;
            std::copy(m_state.begin() + localStates.algSize,
                      m_state.begin() + stateCount,
                      state + solverOffsetsValue.diffOffset);
            std::copy(m_dstate_dt.data() + localStates.algSize,
                      m_dstate_dt.data() + stateCount,
                      dstateDt + solverOffsetsValue.diffOffset);
        }
    }

    for (auto& sub : subObjectList) {
        sub->guessState(time, state, dstateDt, sMode);
    }
}

void GridComponent::setupPFlowFlags()
{
    auto stateCount = stateSize(cPflowSolverMode);
    opFlags.set(HAS_PFLOW_STATES, (stateCount > 0));
    // load the subobject pflow states;
    for (auto& sub : subObjectList) {
        if (sub->checkFlag(HAS_PFLOW_STATES)) {
            opFlags.set(HAS_SUBOBJECT_PFLOW_STATES);
            return;
        }
    }
}

void GridComponent::setupDynFlags()
{
    auto stateCount = stateSize(cDaeSolverMode);

    opFlags.set(HAS_DYN_STATES, (stateCount > 0));
    const auto& solverOffsetsValue = offsets.getOffsets(cDaeSolverMode);
    if (solverOffsetsValue.total.algRoots > 0) {
        opFlags.set(HAS_ALG_ROOTS);
        opFlags.set(HAS_ROOTS);
    } else if (solverOffsetsValue.total.diffRoots > 0) {
        opFlags.reset(HAS_ALG_ROOTS);
        opFlags.set(HAS_ROOTS);
    } else {
        opFlags.reset(HAS_ALG_ROOTS);
        opFlags.reset(HAS_ROOTS);
    }
}

double GridComponent::getState(index_t offset) const
{
    if (isValidIndex(offset, m_state)) {
        return m_state[offset];
    }
    return kNullVal;
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::loadSizesSub(const SolverMode& sMode, SizeCategory category)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    switch (category) {
        case SizeCategory::STATE_SIZE_UPDATE:
            solverOffsetsValue.localStateLoad(false);
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isStateCountLoaded(sMode))) {
                        sub->loadStateSizes(sMode);
                    }
                    if (sub->checkFlag(SAMPLED_ONLY)) {
                        continue;
                    }
                    solverOffsetsValue.addStateSizes(sub->offsets.getOffsets(sMode));
                }
            }
            solverOffsetsValue.stateLoaded = true;
            break;
        case SizeCategory::JACOBIAN_SIZE_UPDATE:
            solverOffsetsValue.total.jacSize = solverOffsetsValue.local.jacSize;
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isJacobianCountLoaded(sMode))) {
                        sub->loadJacobianSizes(sMode);
                    }
                    if (sub->checkFlag(SAMPLED_ONLY)) {
                        continue;
                    }
                    solverOffsetsValue.addJacobianSizes(sub->offsets.getOffsets(sMode));
                }
            }
            solverOffsetsValue.jacobianLoaded = true;
            break;
        case SizeCategory::ROOT_SIZE_UPDATE:
            solverOffsetsValue.total.algRoots = solverOffsetsValue.local.algRoots;
            solverOffsetsValue.total.diffRoots = solverOffsetsValue.local.diffRoots;
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isRootCountLoaded(sMode))) {
                        sub->loadRootSizes(sMode);
                    }
                    if (sub->checkFlag(SAMPLED_ONLY)) {
                        continue;
                    }
                    solverOffsetsValue.addRootSizes(sub->offsets.getOffsets(sMode));
                }
            }
            solverOffsetsValue.rootsLoaded = true;
            break;
    }
}

StateSizes GridComponent::localStateSizes(const SolverMode& /*sMode*/) const
{
    return offsets.local().local;
}

count_t GridComponent::localJacobianCount(const SolverMode& /*sMode*/) const
{
    return offsets.local().local.jacSize;
}

std::pair<count_t, count_t> GridComponent::LocalRootCount(const SolverMode& /*sMode*/) const
{
    const auto& localCounts = offsets.local().local;
    return std::make_pair(localCounts.algRoots, localCounts.diffRoots);
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::loadStateSizes(const SolverMode& sMode)
{
    if (isStateCountLoaded(sMode)) {
        return;
    }
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        solverOffsetsValue.reset();
        solverOffsetsValue.setLoaded();
        return;
    }
    if ((!isDynamic(sMode)) && (opFlags[NO_POWERFLOW_OPERATIONS])) {
        solverOffsetsValue.stateReset();
        solverOffsetsValue.stateLoaded = true;
        return;
    }
    if ((isDynamic(sMode)) && (opFlags[NO_DYNAMICS])) {
        solverOffsetsValue.stateReset();
        solverOffsetsValue.stateLoaded = true;
    }

    if (!(solverOffsetsValue.stateLoaded)) {
        if (!isLocal(sMode))  // don't reset if it is the local offsets
        {
            solverOffsetsValue.stateReset();
        }
        auto selfSizes = localStateSizes(sMode);
        if (hasAlgebraic(sMode)) {
            solverOffsetsValue.local.aSize = selfSizes.aSize;
            solverOffsetsValue.local.vSize = selfSizes.vSize;
            solverOffsetsValue.local.algSize = selfSizes.algSize;
        }
        if (hasDifferential(sMode)) {
            solverOffsetsValue.local.diffSize = selfSizes.diffSize;
        }
    }

    if (opFlags[SAMPLED_ONLY])  // no states
    {
        if (sMode == cLocalSolverMode) {
            for (auto& sub : subObjectList) {
                sub->setFlag("SAMPLED_ONLY");
            }
        } else {
            solverOffsetsValue.local.reset();
            solverOffsetsValue.total.reset();
        }
    }
    if (subObjectList.empty()) {
        solverOffsetsValue.localStateLoad(true);
    } else {
        loadSizesSub(sMode, SizeCategory::STATE_SIZE_UPDATE);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::loadRootSizes(const SolverMode& sMode)
{
    if (isRootCountLoaded(sMode)) {
        return;
    }
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        solverOffsetsValue.reset();
        solverOffsetsValue.setLoaded();
        return;
    }
    if (!isDynamic(sMode)) {
        solverOffsetsValue.rootCountReset();
        solverOffsetsValue.rootsLoaded = true;
        return;
    }

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        solverOffsetsValue.rootCountReset();
    }
    auto selfSizes = LocalRootCount(sMode);
    if (!(solverOffsetsValue.rootsLoaded)) {
        solverOffsetsValue.local.algRoots = selfSizes.first;
        solverOffsetsValue.local.diffRoots = selfSizes.second;
    }

    if (subObjectList.empty()) {
        solverOffsetsValue.total.algRoots = solverOffsetsValue.local.algRoots;
        solverOffsetsValue.total.diffRoots = solverOffsetsValue.local.diffRoots;
        solverOffsetsValue.rootsLoaded = true;
    } else {
        loadSizesSub(sMode, SizeCategory::ROOT_SIZE_UPDATE);
    }
    if ((solverOffsetsValue.total.diffRoots > 0) || (solverOffsetsValue.total.algRoots > 0)) {
        opFlags.set(HAS_ROOTS);
        if (solverOffsetsValue.total.algRoots > 0) {
            opFlags.set(HAS_ALG_ROOTS);
        }
    } else {
        opFlags.reset(HAS_ROOTS);
        opFlags.reset(HAS_ALG_ROOTS);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::loadJacobianSizes(const SolverMode& sMode)
{
    if (isJacobianCountLoaded(sMode)) {
        return;
    }
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        solverOffsetsValue.reset();
        solverOffsetsValue.setLoaded();
        return;
    }

    auto selfJacCount = localJacobianCount(sMode);

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        solverOffsetsValue.jacobianCountReset();
    }

    if (!(solverOffsetsValue.jacobianLoaded)) {
        solverOffsetsValue.local.jacSize = selfJacCount;
    }

    if (subObjectList.empty()) {
        solverOffsetsValue.total.jacSize = solverOffsetsValue.local.jacSize;
        solverOffsetsValue.jacobianLoaded = true;
    } else {
        loadSizesSub(sMode, SizeCategory::JACOBIAN_SIZE_UPDATE);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::getTols(double tols[], const SolverMode& sMode)
{
    for (auto& subObj : subObjectList) {
        if (subObj->isEnabled()) {
            subObj->getTols(tols, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::getVariableType(double sdata[], const SolverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (subObjectList.empty()) {
        if (solverOffsetsValue.total.algSize > 0) {
            auto offset = solverOffsetsValue.algOffset;
            for (index_t kk = 0; kk < solverOffsetsValue.total.algSize; ++kk) {
                sdata[offset + kk] = ALGEBRAIC_VARIABLE;
            }
        }
        if (solverOffsetsValue.total.diffSize > 0) {
            auto offset = solverOffsetsValue.diffOffset;
            for (index_t kk = 0; kk < solverOffsetsValue.total.diffSize; ++kk) {
                sdata[offset + kk] = DIFFERENTIAL_VARIABLE;
            }
        }
    } else {
        if (solverOffsetsValue.local.algSize > 0) {
            auto offset = solverOffsetsValue.algOffset;
            for (index_t kk = 0; kk < solverOffsetsValue.local.algSize; ++kk) {
                sdata[offset + kk] = ALGEBRAIC_VARIABLE;
            }
        }
        if (solverOffsetsValue.local.diffSize > 0) {
            auto offset = solverOffsetsValue.diffOffset;
            for (index_t kk = 0; kk < solverOffsetsValue.local.diffSize; ++kk) {
                sdata[offset + kk] = DIFFERENTIAL_VARIABLE;
            }
        }
        for (auto& subobj : subObjectList) {
            if (subobj->isEnabled()) {
                subobj->getVariableType(sdata, sMode);
            }
        }
    }
}

static const auto& alertFlags()
{
    static const std::map<int, int> flags{
        std::make_pair(FLAG_CHANGE, 1),
        std::make_pair(STATE_COUNT_INCREASE, 3),
        std::make_pair(STATE_COUNT_DECREASE, 3),
        std::make_pair(STATE_COUNT_CHANGE, 3),
        std::make_pair(ROOT_COUNT_INCREASE, 2),
        std::make_pair(ROOT_COUNT_DECREASE, 2),
        std::make_pair(ROOT_COUNT_CHANGE, 2),
        std::make_pair(JAC_COUNT_INCREASE, 4),
        std::make_pair(JAC_COUNT_DECREASE, 4),
        std::make_pair(JAC_COUNT_CHANGE, 4),
        std::make_pair(OBJECT_COUNT_INCREASE, 5),
        std::make_pair(OBJECT_COUNT_DECREASE, 5),
        std::make_pair(OBJECT_COUNT_CHANGE, 5),
        std::make_pair(CONSTRAINT_COUNT_DECREASE, 1),
        std::make_pair(CONSTRAINT_COUNT_INCREASE, 1),
        std::make_pair(CONSTRAINT_COUNT_CHANGE, 1),
    };
    return flags;
}

void GridComponent::alert(CoreObject* object, int code)
{
    if ((code >= MIN_CHANGE_ALERT) && (code <= MAX_CHANGE_ALERT)) {
        const auto& flags = alertFlags();
        auto res = flags.find(code);
        if (res != flags.end()) {
            if (!opFlags[DISABLE_FLAG_UPDATES]) {
                updateFlags();
            } else {
                opFlags.set(FLAG_UPDATE_REQUIRED);
            }
            switch (res->second) {
                case 3:
                    offsets.stateUnload();
                    offsets.jacobianUnload(true);
                    break;
                case 2:
                    offsets.rootUnload(true);
                    break;
                case 4:
                    offsets.jacobianUnload(true);
                    break;
                case 5:
                    offsets.stateUnload();
                    offsets.jacobianUnload(true);
                    offsets.rootUnload(true);
                    break;
                default:
                    break;
            }
        }
    }
    CoreObject::alert(object, code);
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::getConstraints(double constraints[], const SolverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if ((subobj->isEnabled()) && (subobj->checkFlag(HAS_CONSTRAINTS))) {
            subobj->getConstraints(constraints, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::setRootOffset(index_t newRootOffset, const SolverMode& sMode)
{
    offsets.setRootOffset(newRootOffset, sMode);
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    auto nextRootOffset = solverOffsetsValue.local.algRoots + solverOffsetsValue.local.diffRoots;
    for (auto& rootObject : subObjectList) {
        rootObject->setRootOffset(newRootOffset + nextRootOffset, sMode);
        nextRootOffset += rootObject->rootSize(sMode);
    }
}

static const stringVec& emptyStrings()
{
    static const stringVec strings{};
    return strings;
}

stringVec GridComponent::localStateNames() const
{
    return emptyStrings();
}

static const std::vector<stringVec>& inputNamesStorage()
{
    static const std::vector<stringVec> names{
        {"input0", "i0"},
        {"input1", "i1"},
        {"input2", "i2"},
        {"input3", "i3"},
        {"input4", "i4"},
        {"input5", "i5"},
        {"input6", "i6"},
        {"input7", "i7"},
        {"input8", "i8"},
        {"input9", "i9"},
        {"input10", "i10"},
        {"input11", "i11"},
    };
    return names;
}

const std::vector<stringVec>& GridComponent::inputNames() const
{
    return inputNamesStorage();
}

static const std::vector<stringVec>& outputNamesStorage()
{
    static const std::vector<stringVec> names{
        {"output", "o0", "out0", "output0", "out", "o", "value"},
        {"output1", "o1", "out1"},
        {"output2", "o2", "out2"},
        {"output3", "o3", "out3"},
        {"output4", "o4", "out4"},
        {"output5", "o5", "out5"},
        {"output6", "o6", "out6"},
        {"output7", "o7", "out7"},
        {"output8", "o8", "out8"},
        {"output9", "o9", "out9"},
        {"output10", "o10", "out10"},
        {"output11", "o11", "out11"},
    };
    return names;
}

const std::vector<stringVec>& GridComponent::outputNames() const
{
    return outputNamesStorage();
}

units::unit GridComponent::inputUnits(index_t /*inputNum*/) const
{  // just return the default unit
    return units::defunit;
}

units::unit GridComponent::outputUnits(index_t /*outputNum*/) const
{  // just return the default unit
    return units::defunit;
}

// NOLINTNEXTLINE(misc-no-recursion)
index_t GridComponent::findIndex(std::string_view field, const SolverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (field.starts_with("state")) {
        auto num = static_cast<index_t>(
            gmlc::utilities::stringOps::trailingStringInt(std::string{field}, 0));
        if (stateSize(sMode) > num) {
            if (solverOffsetsValue.algOffset != kNullLocation) {
                return solverOffsetsValue.algOffset + num;
            }
            return kNullLocation;
        }
        return kInvalidLocation;
    }
    if (field.starts_with("alg")) {
        auto num = static_cast<index_t>(
            gmlc::utilities::stringOps::trailingStringInt(std::string{field}, 0));
        if (solverOffsetsValue.total.algSize > num) {
            if (solverOffsetsValue.algOffset != kNullLocation) {
                return solverOffsetsValue.algOffset + num;
            }
            return kNullLocation;
        }
        if (!opFlags[DYN_INITIALIZED]) {
            return kNullLocation;
        }
        return kInvalidLocation;
    }
    if (field.starts_with("diff")) {
        auto num = static_cast<index_t>(
            gmlc::utilities::stringOps::trailingStringInt(std::string{field}, 0));
        if (solverOffsetsValue.total.diffSize > num) {
            if (solverOffsetsValue.diffOffset != kNullLocation) {
                return solverOffsetsValue.diffOffset + num;
            }
            return kNullLocation;
        }
        if (!opFlags[DYN_INITIALIZED]) {
            return kNullLocation;
        }
        return kInvalidLocation;
    }
    auto stateNames = localStateNames();
    for (index_t nn = 0; std::cmp_less(nn, stateNames.size()); ++nn) {
        if (field == stateNames[nn]) {
            const auto& localOffsetsValue = offsets.local();
            if (nn < localOffsetsValue.local.algSize) {
                if (solverOffsetsValue.algOffset != kNullLocation) {
                    return solverOffsetsValue.algOffset + nn;
                }
                return kNullLocation;
            }
            if (nn - localOffsetsValue.local.algSize < localOffsetsValue.local.diffSize) {
                if (solverOffsetsValue.diffOffset != kNullLocation) {
                    return solverOffsetsValue.diffOffset + nn - localOffsetsValue.local.algSize;
                }
                return kNullLocation;
            }
            if (!opFlags[DYN_INITIALIZED]) {
                return kNullLocation;
            }
            return kInvalidLocation;
        }
    }
    for (auto* subobj : subObjectList) {
        auto ret = subobj->findIndex(field, sMode);
        if (ret != kInvalidLocation) {
            return ret;
        }
    }
    return kInvalidLocation;
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::getStateName(stringVec& stNames,
                                 const SolverMode& sMode,
                                 const std::string& prefix) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    auto mxsize = offsets.maxIndex(sMode);
    const std::string prefix2 = prefix + getName() + ':';

    if ((mxsize > 0) && (solverOffsetsValue.stateLoaded)) {
        ensureSizeAtLeast(stNames, mxsize + 1);
    } else {
        return;
    }
    auto stateNames = localStateNames();
    auto stsize = static_cast<index_t>(stateNames.size());
    decltype(mxsize) stateNameIndex = 0;
    if (hasAlgebraic(sMode)) {
        for (index_t kk = 0; kk < solverOffsetsValue.local.vSize; kk++) {
            if (!stNames[solverOffsetsValue.vOffset + kk].empty()) {
                continue;
            }
            if (stsize > stateNameIndex) {
                stNames[solverOffsetsValue.vOffset + kk] = prefix2 + stateNames[stateNameIndex];
                ++stateNameIndex;
            } else {
                stNames[solverOffsetsValue.vOffset + kk] =
                    prefix2 + "voltage_state_" + std::to_string(kk);
            }
        }
        stateNameIndex = offsets.local().local.vSize;
        for (index_t kk = 0; kk < solverOffsetsValue.local.aSize; kk++) {
            if (!stNames[solverOffsetsValue.aOffset + kk].empty()) {
                continue;
            }
            if (stsize > stateNameIndex) {
                stNames[solverOffsetsValue.aOffset + kk] = prefix2 + stateNames[stateNameIndex];
                ++stateNameIndex;
            } else {
                stNames[solverOffsetsValue.aOffset + kk] =
                    prefix2 + "angle_state_" + std::to_string(kk);
            }
        }
        stateNameIndex = offsets.local().local.vSize + offsets.local().local.aSize;
        for (index_t kk = 0; kk < solverOffsetsValue.local.algSize; kk++) {
            if (!stNames[solverOffsetsValue.algOffset + kk].empty()) {
                continue;
            }
            if (stsize > stateNameIndex) {
                stNames[solverOffsetsValue.algOffset + kk] = prefix2 + stateNames[stateNameIndex];
                ++stateNameIndex;
            } else {
                stNames[solverOffsetsValue.algOffset + kk] =
                    prefix2 + "alg_state_" + std::to_string(kk);
            }
        }
    }
    if (!isAlgebraicOnly(sMode)) {
        if (solverOffsetsValue.local.diffSize > 0) {
            stateNameIndex = offsets.local().local.algSize + offsets.local().local.vSize +
                offsets.local().local.aSize;
            for (index_t kk = 0; kk < solverOffsetsValue.local.diffSize; kk++) {
                if (!stNames[solverOffsetsValue.diffOffset + kk].empty()) {
                    continue;
                }
                if (stsize > stateNameIndex) {
                    stNames[solverOffsetsValue.diffOffset + kk] =
                        prefix2 + stateNames[stateNameIndex];
                    ++stateNameIndex;
                } else {
                    stNames[solverOffsetsValue.diffOffset + kk] =
                        prefix2 + "diff_state_" + std::to_string(kk);
                }
            }
        }
    }

    for (auto* subobj : subObjectList) {
        subobj->getStateName(stNames, sMode, prefix2 + ':');
    }
}

void GridComponent::updateFlags(bool dynamicsFlags)
{
    for (auto& subobj : subObjectList) {
        if (subobj->isEnabled()) {
            opFlags |= subobj->cascadingFlags();
        }
    }
    if (opFlags[DYN_INITIALIZED] && dynamicsFlags) {
        setupDynFlags();
    } else {
        setupPFlowFlags();
    }

    opFlags.reset(FLAG_UPDATE_REQUIRED);
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::updateLocalCache(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     const SolverMode& sMode)
{
    for (auto& sub : subObjectList) {
        sub->updateLocalCache(inputs, stateDataValue, sMode);
    }
}

CoreObject* GridComponent::find(std::string_view object) const
{
    auto foundobj =
        std::find_if(subObjectList.begin(), subObjectList.end(), [object](GridComponent* comp) {
            return (object == comp->getName());
        });
    if (foundobj != subObjectList.end()) {
        return *foundobj;
    }
    // return nullptr if this is an indexed name
    auto rlc2 = object.find_last_of("#$!");
    if (rlc2 != std::string_view::npos) {
        return nullptr;
    }
    return CoreObject::find(object);
}

CoreObject* GridComponent::getSubObject(std::string_view typeName, index_t objectNum) const
{
    if ((typeName == "sub") || (typeName == "subobject") || (typeName == "object")) {
        if (isValidIndex(objectNum, subObjectList)) {
            return subObjectList[objectNum];
        }
    }
    return nullptr;
}

CoreObject* GridComponent::findByUserID(std::string_view typeName, index_t searchID) const
{
    if ((typeName == "sub") || (typeName == "subobject") || (typeName == "object")) {
        auto foundobj = std::find_if(subObjectList.begin(),
                                     subObjectList.end(),
                                     [searchID](GridComponent* comp) {
                                         return (comp->getUserID() == searchID);
                                     });
        if (foundobj == subObjectList.end()) {
            return nullptr;
        }
        return *foundobj;
    }
    return CoreObject::findByUserID(typeName, searchID);
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    prevTime = time;

    for (auto& subobj : subObjectList) {
        if (subobj->currentTime() < time) {
            if (!subobj->checkFlag(SEPARATE_PROCESSING)) {
                subobj->timestep(time, inputs, sMode);
            }
        }
    }
}

void GridComponent::ioPartialDerivatives(const IOdata& /*inputs*/,
                                         const StateData& /*sD*/,
                                         MatrixData<double>& /*md*/,
                                         const IOlocs& /*inputLocs*/,
                                         const SolverMode& /*sMode*/)
{
    /* there is no way to determine partial derivatives of the output with respect to input in a
    default manner therefore the default is no dependencies
    */
}

void GridComponent::outputPartialDerivatives(const IOdata& /*inputs*/,
                                             const StateData& /*stateDataValue*/,
                                             MatrixData<double>& matrixDataValue,
                                             const SolverMode& sMode)
{
    /* assume the output is a state and compute accordingly*/
    for (index_t kk = 0; kk < m_outputSize; ++kk) {
        const index_t outputLoc = getOutputLoc(sMode, kk);
        matrixDataValue.assignCheckCol(kk, outputLoc, 1.0);
    }
}

count_t GridComponent::outputDependencyCount(index_t outputNum, const SolverMode& sMode) const
{
    /* assume the output is a state and act accordingly*/

    const index_t outputLoc = getOutputLoc(sMode, outputNum);
    return (outputLoc == kInvalidLocation) ? 0 : 1;
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::preEx(const IOdata& inputs,
                          const StateData& stateDataValue,
                          const SolverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(PRE_EX_REQUESTED))) {
            continue;
        }
        if (!subobj->checkFlag(SEPARATE_PROCESSING)) {
            subobj->preEx(inputs, stateDataValue, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::residual(const IOdata& inputs,
                             const StateData& stateDataValue,
                             double resid[],
                             const SolverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(SEPARATE_PROCESSING)) {
            if (sub->stateSize(sMode) > 0) {
                sub->residual(inputs, stateDataValue, resid, sMode);
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::derivative(const IOdata& inputs,
                               const StateData& stateDataValue,
                               double deriv[],
                               const SolverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(SEPARATE_PROCESSING)) {
            if (sub->diffSize(sMode) > 0) {
                sub->derivative(inputs, stateDataValue, deriv, sMode);
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateDataValue,
                                    double update[],
                                    const SolverMode& sMode,
                                    double alpha)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(SEPARATE_PROCESSING)) {
            if (sub->algSize(sMode) > 0) {
                sub->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::jacobianElements(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     MatrixData<double>& matrixDataValue,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(SEPARATE_PROCESSING)) {
            if (sub->stateSize(sMode) > 0) {
                sub->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
            }
        }
    }
}
// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::rootTest(const IOdata& inputs,
                             const StateData& stateDataValue,
                             double roots[],
                             const SolverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!subobj->checkFlag(SEPARATE_PROCESSING)) {
            if (!(subobj->checkFlag(HAS_ROOTS))) {
                continue;
            }
            subobj->rootTest(inputs, stateDataValue, roots, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void GridComponent::rootTrigger(CoreTime time,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(HAS_ROOTS))) {
            continue;
        }
        if (!subobj->checkFlag(SEPARATE_PROCESSING)) {
            subobj->rootTrigger(time, inputs, rootMask, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
ChangeCode GridComponent::rootCheck(const IOdata& inputs,
                                    const StateData& stateDataValue,
                                    const SolverMode& sMode,
                                    CheckLevel level)
{
    auto ret = ChangeCode::NO_CHANGE;

    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(HAS_ROOTS))) {
            continue;
        }
        if (!subobj->checkFlag(SEPARATE_PROCESSING)) {
            ret = std::max(subobj->rootCheck(inputs, stateDataValue, sMode, level), ret);
        }
    }
    return ret;
}

index_t GridComponent::lookupOutputIndex(std::string_view outputName) const
{
    const auto& outputStr = outputNames();
    index_t outputsize = (std::min)(static_cast<index_t>(outputStr.size()), m_outputSize);
    for (index_t kk = 0; kk < outputsize; ++kk) {
        for (const auto& onm : outputStr[kk]) {
            if (outputName == onm) {
                return kk;
            }
        }
    }
    // didn't find it so lookup the default output names
    const auto& defOutputStr = GridComponent::outputNames();
    outputsize = m_outputSize;
    for (index_t kk = 0; kk < outputsize; ++kk) {
        for (const auto& onm : defOutputStr[kk]) {
            if (outputName == onm) {
                return kk;
            }
        }
    }
    return kNullLocation;
}

double GridComponent::getOutput(const IOdata& /*inputs*/,
                                const StateData& stateDataValue,
                                const SolverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullVal;
    }
    auto locations = offsets.getLocations(stateDataValue, sMode, this);
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        if (locations.diffSize > outputNum) {
            assert(locations.diffStateLoc != nullptr);
            return locations.diffStateLoc[outputNum];
        }
        return kNullVal;
    }
    // if differential flag was not specified try algebraic state values then differential

    if (locations.algSize > outputNum) {
        assert(locations.algStateLoc != nullptr);
        return locations.algStateLoc[outputNum];
    }
    if (locations.diffSize + locations.algSize > outputNum) {
        assert(locations.diffStateLoc != nullptr);
        return locations.diffStateLoc[outputNum - locations.algSize];
    }
    return std::cmp_greater(m_state.size(), outputNum) ? m_state[outputNum] : kNullVal;
}

double GridComponent::getOutput(index_t outputNum) const
{
    return getOutput(noInputs, emptyStateData, cLocalSolverMode, outputNum);
}

IOdata GridComponent::getOutputs(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode) const
{
    IOdata mout(m_outputSize);
    for (count_t pp = 0; pp < m_outputSize; ++pp) {
        mout[pp] = getOutput(inputs, stateDataValue, sMode, pp);
    }
    return mout;
}

// static IOdata kNullVec;

double GridComponent::getDoutdt(const IOdata& /*inputs*/,
                                const StateData& stateDataValue,
                                const SolverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullVal;
    }
    auto locations = offsets.getLocations(stateDataValue, sMode, this);
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        assert(locations.dstateLoc != nullptr);
        return locations.dstateLoc[outputNum];
    }

    if (locations.algSize > outputNum) {
        return 0.0;
    }
    if (locations.diffSize + locations.algSize > outputNum) {
        assert(locations.dstateLoc != nullptr);
        return locations.dstateLoc[outputNum - locations.algSize];
    }
    return 0.0;
}

index_t GridComponent::getOutputLoc(const SolverMode& sMode, index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullLocation;
    }

    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        if (outputNum < diffSize(sMode)) {
            return offsets.getDiffOffset(sMode) + outputNum;
        }
        outputNum -= diffSize(sMode);
        return offsets.getAlgOffset(sMode) + outputNum - diffSize(sMode);
    }

    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (solverOffsetsValue.total.algSize > outputNum) {
        return solverOffsetsValue.algOffset + outputNum;
    }
    if (solverOffsetsValue.total.diffSize + solverOffsetsValue.total.algSize > outputNum) {
        return solverOffsetsValue.diffOffset - solverOffsetsValue.total.algSize + outputNum;
    }

    return kNullLocation;
}

IOlocs GridComponent::getOutputLocs(const SolverMode& sMode) const
{
    IOlocs oloc(m_outputSize);

    if (!isLocal(sMode)) {
        for (count_t pp = 0; pp < m_outputSize; ++pp) {
            oloc[pp] = getOutputLoc(sMode, pp);
        }
    } else {
        for (count_t pp = 0; pp < m_outputSize; ++pp) {
            oloc[pp] = kNullLocation;
        }
    }
    return oloc;
}

void GridComponent::setParameter(index_t param, double /*value*/)
{
    throw(UnrecognizedParameter("param" + std::to_string(param)));
}
double GridComponent::getParameter(index_t param) const
{
    throw(UnrecognizedParameter("param" + std::to_string(param)));
}
void GridComponent::parameterPartialDerivatives(index_t param,
                                                double /*val*/,
                                                const IOdata& /*inputs*/,
                                                const StateData& /*sD*/,
                                                MatrixData<double>& /*md*/,
                                                const SolverMode& /*sMode*/)
{
    throw(UnrecognizedParameter("param" + std::to_string(param)));
}

double GridComponent::parameterOutputPartialDerivatives(index_t param,
                                                        double /*val*/,
                                                        index_t /*outputNum*/,
                                                        const IOdata& /*inputs*/,
                                                        const StateData& /*sD*/,
                                                        const SolverMode& /*sMode*/)
{
    throw(UnrecognizedParameter("param" + std::to_string(param)));
}

void printStateNames(const GridComponent* comp, const SolverMode& sMode)
{
    auto ssize = comp->stateSize(sMode);
    std::vector<std::string> sNames(ssize);
    comp->getStateName(sNames, sMode);
    int stateIndex = 0;
    for (auto& stateName : sNames) {
        std::cout << stateIndex++ << ' ' << stateName << '\n';
        if (stateIndex >= ssize) {
            break;
        }
    }
}

}  // namespace griddyn
