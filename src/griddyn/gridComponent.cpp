/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridComponent.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::ensureSizeAtLeast;

gridComponent::gridComponent(const std::string& objName): coreObject(objName)
{
    offsets.setAlgOffset(0, cLocalSolverMode);
}

gridComponent::~gridComponent()
{
    for (auto& subObject : subObjectList) {
        removeReference(subObject, this);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
coreObject* gridComponent::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<gridComponent, coreObject>(this, obj);
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
            catch (const unrecognizedObjectException&) {
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
                    catch (const unrecognizedObjectException&) {
                        continue;
                    }
                }
            } else {
                if (ii >= csz) {
                    try {
                        nobj->add(subObjectList[ii]->clone());
                    }
                    catch (const unrecognizedObjectException&) {
                        continue;
                    }
                } else {
                    if (typeid(subObjectList[ii]) == typeid(nobj->subObjectList[ii])) {
                        subObjectList[ii]->clone(nobj->subObjectList[ii]);
                    } else {
                        try {
                            nobj->add(subObjectList[ii]->clone());
                        }
                        catch (const unrecognizedObjectException&) {
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
void gridComponent::updateObjectLinkages(coreObject* newRoot)
{
    for (auto* subobj : getSubObjects()) {
        subobj->updateObjectLinkages(newRoot);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::pFlowInitializeA(coreTime time0, std::uint32_t flags)
{
    if (localBaseVoltage == kNullVal) {
        if (isRoot()) {  // NOLINT
            localBaseVoltage = 120000.0;
        } else if (dynamic_cast<gridComponent*>(getParent()) != nullptr) {
            localBaseVoltage = static_cast<gridComponent*>(getParent())->localBaseVoltage;
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
void gridComponent::pFlowInitializeB()
{
    if (isEnabled()) {
        pFlowObjectInitializeB();
        opFlags.set(pFlow_initialized);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::dynInitializeA(coreTime time0, std::uint32_t flags)
{
    if (isEnabled()) {
        dynObjectInitializeA(time0, flags);
        prevTime = time0;
        updateFlags(true);
        setupDynFlags();
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::dynInitializeB(const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    if (isEnabled()) {
        dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        if (updatePeriod < maxTime) {
            setUpdateTime(prevTime + updatePeriod);
            enable_updates();
        }
        opFlags.set(dyn_initialized);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& subobj : subObjectList) {
        subobj->pFlowInitializeA(time0, flags);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::pFlowObjectInitializeB()
{
    for (auto& subobj : subObjectList) {
        subobj->pFlowInitializeB();
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& subobj : subObjectList) {
        subobj->dynInitializeA(time0, flags);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    for (auto& subobj : subObjectList) {
        if (!subobj->checkFlag(separate_processing)) {
            subobj->dynInitializeB(inputs, desiredOutput, fieldSet);
        }
    }
}

count_t gridComponent::stateSize(const solverMode& sMode)
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    count_t ssize =
        (hasAlgebraic(sMode)) ?
            (solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
             solverOffsetsValue.total.aSize) :
            0;
    if (hasDifferential(sMode)) {
        ssize += solverOffsetsValue.total.diffSize;
    }
    return ssize;
}

count_t gridComponent::stateSize(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    count_t ssize =
        (hasAlgebraic(sMode)) ?
            (solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
             solverOffsetsValue.total.aSize) :
            0;
    if (hasDifferential(sMode)) {
        ssize += solverOffsetsValue.total.diffSize;
    }
    return ssize;
}

count_t gridComponent::totalAlgSize(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
        solverOffsetsValue.total.aSize;
}

count_t gridComponent::totalAlgSize(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return (solverOffsetsValue.total.algSize + solverOffsetsValue.total.vSize +
            solverOffsetsValue.total.aSize);
}

count_t gridComponent::algSize(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.algSize;
}

count_t gridComponent::algSize(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.algSize;
}

count_t gridComponent::diffSize(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.diffSize;
}

count_t gridComponent::diffSize(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.diffSize;
}

count_t gridComponent::rootSize(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.rootsLoaded)) {
        loadRootSizes(sMode);
    }
    return solverOffsetsValue.total.algRoots + solverOffsetsValue.total.diffRoots;
}

count_t gridComponent::rootSize(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);

    return solverOffsetsValue.total.algRoots + solverOffsetsValue.total.diffRoots;
}

count_t gridComponent::jacSize(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.jacobianLoaded)) {
        loadJacobianSizes(sMode);
    }
    return solverOffsetsValue.total.jacSize;
}

count_t gridComponent::jacSize(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.jacSize;
}

count_t gridComponent::voltageStateCount(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.vSize;
}

count_t gridComponent::voltageStateCount(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.vSize;
}

count_t gridComponent::angleStateCount(const solverMode& sMode)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    if (!(solverOffsetsValue.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return solverOffsetsValue.total.aSize;
}

count_t gridComponent::angleStateCount(const solverMode& sMode) const
{
    const auto& solverOffsetsValue = offsets.getOffsets(sMode);
    return solverOffsetsValue.total.aSize;
}

const solverOffsets& gridComponent::getOffsets(const solverMode& sMode) const
{
    return offsets.getOffsets(sMode);
}
// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::setOffsets(const solverOffsets& newOffsets, const solverMode& sMode)
{
    offsets.setOffsets(newOffsets, sMode);

    if (!subObjectList.empty()) {
        solverOffsets newSubOffsets(newOffsets);
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
void gridComponent::setOffset(index_t newOffset, const solverMode& sMode)
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

bool gridComponent::isStateCountLoaded(const solverMode& sMode) const
{
    return offsets.isStateCountLoaded(sMode);
}

bool gridComponent::isJacobianCountLoaded(const solverMode& sMode) const
{
    return offsets.isJacobianCountLoaded(sMode);
}

bool gridComponent::isRootCountLoaded(const solverMode& sMode) const
{
    return offsets.isRootCountLoaded(sMode);
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string_view, operation_flags, std::less<>> user_settable_flags{
    {"use_bus_frequency", uses_bus_frequency},
    {"late_b_initialize", late_b_initialize},
    {"error", error_flag},
    {"no_gridcomponent_set", no_gridcomponent_set},
    {"disable_flag_update", disable_flag_updates},
    {"flag_update_required", flag_update_required},
    {"pflow_init_required", pflow_init_required},
    {"sampled_only", no_dynamics},
};

// there isn't that many flags that we want to be user settable, most are controlled by the model so
// allowing them to be set by an external function might not be the best thing
void gridComponent::setFlag(std::string_view flag, bool val)
{
    auto ffind = user_settable_flags.find(flag);
    if (ffind != user_settable_flags.end()) {
        opFlags.set(ffind->second, val);
        if (flag == "sampled_only") {
            if (opFlags[pFlow_initialized]) {
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
        coreObject::setFlag(flag, val);
    }
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::vector<index_t> parentSettableFlags{sampled_only,
                                                      no_gridcomponent_set,
                                                      separate_processing};

void gridComponent::parentSetFlag(index_t flagID, bool val, coreObject* checkParent)
{
    if (isSameObject(getParent(), checkParent)) {
        if (std::binary_search(parentSettableFlags.begin(), parentSettableFlags.end(), flagID)) {
            opFlags[flagID] = val;
        }
    }
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string_view, operation_flags, std::less<>> flagmap{
    {"constraints", has_constraints},
    {"roots", has_roots},
    {"alg_roots", has_alg_roots},
    {"voltage_adjustments", has_powerflow_adjustments},
    {"preex", preEx_requested},
    {"use_bus_frequency", uses_bus_frequency},
    {"pflow_states", has_pflow_states},
    {"dyn_states", has_dyn_states},
    {"differential_states", has_differential_states},
    {"not_cloneable", not_cloneable},
    {"remote_voltage_control", remote_voltage_control},
    {"local_voltage_control", local_voltage_control},
    {"indirect_voltage_control", indirect_voltage_control},
    {"remote_power_control", remote_voltage_control},
    {"local_power_control", local_power_control},
    {"indirect_power_control", indirect_power_control},
    {"pflow_initialized", pFlow_initialized},
    {"dyn_initialized", dyn_initialized},
    {"armed", object_armed_flag},
    {"late_b_initialize", late_b_initialize},
    {"object_flag1", object_flag1},
    {"object_flag2", object_flag2},
    {"object_flag3", object_flag3},
    {"object_flag4", object_flag4},
    {"object_flag5", object_flag5},
    {"object_flag6", object_flag6},
    {"object_flag7", object_flag7},
    {"object_flag8", object_flag8},
    {"object_flag9", object_flag9},
    {"object_flag10", object_flag10},
    {"object_flag11", object_flag11},
    {"object_flag12", object_flag12},
    {"state_change", state_change_flag},
    {"object_change", object_change_flag},
    {"constraint_change", constraint_change_flag},
    {"root_change", root_change_flag},
    {"jacobian_count_change", jacobian_count_change_flag},
    {"slack_bus_change", slack_bus_change},
    {"voltage_control_change", voltage_control_change},
    {"error", error_flag},
    {"connectivity_change", connectivity_change_flag},
    {"no_powerflow_operations", no_powerflow_operations},
    {"disconnected", disconnected},
    {"no_dynamics", no_dynamics},
    {"sampled_only", no_dynamics},
    {"disable_flag_update", disable_flag_updates},
    {"flag_update_required", flag_update_required},
    {"differential_output", differential_output},
    {"multipart_calculation_capable", multipart_calculation_capable},
    {"pflow_init_required", pflow_init_required},
    {"dc_only", dc_only},
    {"dc_capable", dc_capable},
    {"dc_terminal2", dc_terminal2},
    {"separate_processing", separate_processing},
    {"three_phase_only", three_phase_only},
    {"three_phase_capable", three_phase_capable},
    {"three_phase_terminal2", three_phase_terminal2}};

bool gridComponent::getFlag(std::string_view flag) const
{
    auto flagfind = flagmap.find(flag);
    if (flagfind != flagmap.end()) {
        return opFlags[flagfind->second];
    }
    return coreObject::getFlag(flag);
}

bool gridComponent::checkFlag(index_t flagID) const
{
    return opFlags.test(flagID);
}
bool gridComponent::hasStates(const solverMode& sMode) const
{
    return (stateSize(sMode) > 0);
}
bool gridComponent::isArmed() const
{
    return opFlags[object_armed_flag];
}
bool gridComponent::isCloneable() const
{
    return !opFlags[not_cloneable];
}
bool gridComponent::isConnected() const
{
    return !(opFlags[disconnected]);
}
void gridComponent::reconnect()
{
    opFlags.set(disconnected, false);
}
void gridComponent::disconnect()
{
    opFlags.set(disconnected);
}
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const stringVec locNumStrings{"status", "basefrequency", "basepower"};
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const stringVec locStrStrings{"status"};

void gridComponent::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<gridComponent, coreObject>(this, pstr, locNumStrings, locStrStrings, {}, pstype);
}

void gridComponent::set(std::string_view param, std::string_view val)
{
    if (opFlags[no_gridcomponent_set]) {
        throw(unrecognizedParameter(param));
    }

    if (param == "status") {
        auto valueLower = convertToLowerCase(std::string{val});
        if ((valueLower == "on") || (valueLower == "in") || (valueLower == "enabled")) {
            if (!isEnabled()) {
                enable();
                if ((opFlags[has_pflow_states]) || (opFlags[has_dyn_states])) {
                    alert(this, STATE_COUNT_CHANGE);
                }
            }
        } else if ((valueLower == "off") || (valueLower == "out") || (valueLower == "disabled")) {
            if (isEnabled()) {
                if ((opFlags[has_pflow_states]) || (opFlags[has_dyn_states])) {
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
        coreObject::set(param, val);
    }
}

static auto hasParameterPath(std::string_view param)
{
    return (param.find_last_of(":?") != std::string_view::npos);
}
bool gridComponent::subObjectSet(std::string_view param, double val, units::unit unitType)
{
    if (hasParameterPath(param)) {
        const objInfo pinfo(std::string{param}, this);
        if (pinfo.m_obj != nullptr) {
            if (pinfo.m_unitType != units::defunit) {
                pinfo.m_obj->set(pinfo.m_field, val, pinfo.m_unitType);
            } else {
                pinfo.m_obj->set(pinfo.m_field, val, unitType);
            }
            return true;
        }
        throw(unrecognizedParameter(param));
    }
    return false;
}

bool gridComponent::subObjectSet(std::string_view param, std::string_view val)
{
    if (hasParameterPath(param)) {
        const objInfo pinfo(std::string{param}, this);
        if (pinfo.m_obj != nullptr) {
            pinfo.m_obj->set(pinfo.m_field, val);
        } else {
            throw(unrecognizedParameter(param));
        }
        return true;
    }
    return false;
}

bool gridComponent::subObjectSet(std::string_view flag, bool val)
{
    if (hasParameterPath(flag)) {
        const objInfo pinfo(std::string{flag}, this);
        if (pinfo.m_obj != nullptr) {
            pinfo.m_obj->setFlag(pinfo.m_field, val);
        } else {
            throw(unrecognizedParameter(flag));
        }
        return true;
    }
    return false;
}

double gridComponent::subObjectGet(std::string_view param, units::unit unitType) const
{
    if (hasParameterPath(param)) {
        const objInfo pinfo(std::string{param}, this);
        if (pinfo.m_obj != nullptr) {
            if (pinfo.m_unitType != units::defunit) {
                return pinfo.m_obj->get(pinfo.m_field, pinfo.m_unitType);
            }
            return pinfo.m_obj->get(pinfo.m_field, unitType);
        }
        throw(unrecognizedParameter(param));
    }
    return kNullVal;
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::set(std::string_view param, double val, units::unit unitType)
{
    if (opFlags[no_gridcomponent_set]) {
        throw(unrecognizedParameter(param));
    }

    if ((param == "enabled") || (param == "status")) {
        if (val > 0.1) {
            if (!isEnabled()) {
                enable();
                if (opFlags[has_dyn_states]) {
                    alert(this, STATE_COUNT_CHANGE);
                }
            }
        } else {
            if (isEnabled()) {
                if (opFlags[has_dyn_states]) {
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
        coreObject::set(param, val, unitType);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::setAll(std::string_view type,
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

double gridComponent::get(std::string_view param, units::unit unitType) const
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
        if (opFlags[dyn_initialized]) {
            out = jacSize(cDaeSolverMode);
        } else {
            out = jacSize(cPflowSolverMode);
        }
    } else if (param == "statesize") {
        if (opFlags[dyn_initialized]) {
            out = stateSize(cDaeSolverMode);
        } else {
            out = stateSize(cPflowSolverMode);
        }
    } else if (param == "algsize") {
        if (opFlags[dyn_initialized]) {
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
            out = coreObject::get(param, unitType);
        }
    }
    return out;
}

void gridComponent::addSubObject(gridComponent* comp)
{
    if (comp == nullptr) {
        return;
    }
    if (std::any_of(subObjectList.begin(),
                    subObjectList.end(),
                    [comp](const coreObject* subObject) {
                        return isSameObject(subObject, comp);
        })) {
        return;
    }
    comp->setParent(this);
    comp->addOwningReference();
    comp->systemBaseFrequency = systemBaseFrequency;
    comp->systemBasePower = systemBasePower;
    subObjectList.push_back(comp);
    if (opFlags[pFlow_initialized]) {
        offsets.unload(true);
        alert(this, OBJECT_COUNT_INCREASE);
        opFlags[dyn_initialized] = false;
        opFlags[pFlow_initialized] = false;
    }
}

void gridComponent::removeSubObject(gridComponent* obj)
{
    if (!subObjectList.empty()) {
        auto rmobj = std::find_if(subObjectList.begin(),
                                  subObjectList.end(),
                                  [obj](coreObject* subObject) {
                                      return isSameObject(subObject, obj);
                                  });
        if (rmobj != subObjectList.end()) {
            removeReference(*rmobj, this);
            subObjectList.erase(rmobj);
            if (opFlags[pFlow_initialized]) {
                offsets.unload(true);
                alert(this, OBJECT_COUNT_DECREASE);
            }
        }
    }
}

void gridComponent::replaceSubObject(gridComponent* newObj, gridComponent* oldObj)
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
                               [oldObj](const coreObject* subObject) {
                                   return isSameObject(subObject, oldObj);
                               });
    if (repobj != subObjectList.end()) {
        removeReference(*repobj, this);
        newObj->setParent(this);
        newObj->addOwningReference();
        newObj->systemBaseFrequency = systemBaseFrequency;
        newObj->systemBasePower = systemBasePower;
        *repobj = newObj;
        if (opFlags[pFlow_initialized]) {
            offsets.unload(true);
            alert(this, OBJECT_COUNT_CHANGE);
            opFlags[dyn_initialized] = false;
            opFlags[pFlow_initialized] = false;
        }
    } else {
        addSubObject(newObj);
        return;
    }
}
void gridComponent::remove(coreObject* obj)
{
    if (dynamic_cast<gridComponent*>(obj) != nullptr) {
        removeSubObject(static_cast<gridComponent*>(obj));
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::reset(reset_levels level)
{
    for (auto& subobj : subObjectList) {
        subobj->reset(level);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
change_code
    gridComponent::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level)
{
    auto ret = change_code::no_change;

    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(has_powerflow_adjustments))) {
            continue;
        }
        const auto iret = subobj->powerFlowAdjust(inputs, flags, level);
        ret = std::max(iret, ret);
    }
    return ret;
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::setState(coreTime time,
                             const double state[],
                             const double dstate_dt[],
                             const solverMode& sMode)
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
            std::copy(dstate_dt + solverOffsetsValue.diffOffset,
                      dstate_dt + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_dstate_dt.data() + algSize(cLocalSolverMode));
        } else {
            std::copy(state + solverOffsetsValue.diffOffset,
                      state + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_state.data() + localStates.algSize);
            std::copy(dstate_dt + solverOffsetsValue.diffOffset,
                      dstate_dt + solverOffsetsValue.diffOffset + localStates.diffSize,
                      m_dstate_dt.data() + localStates.algSize);
        }
    }

    for (auto& sub : subObjectList) {
        sub->setState(time, state, dstate_dt, sMode);
    }
}
// for saving the state
// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::guessState(coreTime time,
                               double state[],
                               double dstate_dt[],
                               const solverMode& sMode)
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
                      dstate_dt + solverOffsetsValue.diffOffset);
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
                      dstate_dt + solverOffsetsValue.diffOffset);
        }
    }

    for (auto& sub : subObjectList) {
        sub->guessState(time, state, dstate_dt, sMode);
    }
}

void gridComponent::setupPFlowFlags()
{
    auto stateCount = stateSize(cPflowSolverMode);
    opFlags.set(has_pflow_states, (stateCount > 0));
    // load the subobject pflow states;
    for (auto& sub : subObjectList) {
        if (sub->checkFlag(has_pflow_states)) {
            opFlags.set(has_subobject_pflow_states);
            return;
        }
    }
}

void gridComponent::setupDynFlags()
{
    auto stateCount = stateSize(cDaeSolverMode);

    opFlags.set(has_dyn_states, (stateCount > 0));
    const auto& solverOffsetsValue = offsets.getOffsets(cDaeSolverMode);
    if (solverOffsetsValue.total.algRoots > 0) {
        opFlags.set(has_alg_roots);
        opFlags.set(has_roots);
    } else if (solverOffsetsValue.total.diffRoots > 0) {
        opFlags.reset(has_alg_roots);
        opFlags.set(has_roots);
    } else {
        opFlags.reset(has_alg_roots);
        opFlags.reset(has_roots);
    }
}

double gridComponent::getState(index_t offset) const
{
    if (isValidIndex(offset, m_state)) {
        return m_state[offset];
    }
    return kNullVal;
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::loadSizesSub(const solverMode& sMode, sizeCategory category)
{
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    switch (category) {
        case sizeCategory::state_size_update:
            solverOffsetsValue.localStateLoad(false);
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isStateCountLoaded(sMode))) {
                        sub->loadStateSizes(sMode);
                    }
                    if (sub->checkFlag(sampled_only)) {
                        continue;
                    }
                    solverOffsetsValue.addStateSizes(sub->offsets.getOffsets(sMode));
                }
            }
            solverOffsetsValue.stateLoaded = true;
            break;
        case sizeCategory::jacobian_size_update:
            solverOffsetsValue.total.jacSize = solverOffsetsValue.local.jacSize;
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isJacobianCountLoaded(sMode))) {
                        sub->loadJacobianSizes(sMode);
                    }
                    if (sub->checkFlag(sampled_only)) {
                        continue;
                    }
                    solverOffsetsValue.addJacobianSizes(sub->offsets.getOffsets(sMode));
                }
            }
            solverOffsetsValue.jacobianLoaded = true;
            break;
        case sizeCategory::root_size_update:
            solverOffsetsValue.total.algRoots = solverOffsetsValue.local.algRoots;
            solverOffsetsValue.total.diffRoots = solverOffsetsValue.local.diffRoots;
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isRootCountLoaded(sMode))) {
                        sub->loadRootSizes(sMode);
                    }
                    if (sub->checkFlag(sampled_only)) {
                        continue;
                    }
                    solverOffsetsValue.addRootSizes(sub->offsets.getOffsets(sMode));
                }
            }
            solverOffsetsValue.rootsLoaded = true;
            break;
    }
}

stateSizes gridComponent::LocalStateSizes(const solverMode& /*sMode*/) const
{
    return offsets.local().local;
}

count_t gridComponent::LocalJacobianCount(const solverMode& /*sMode*/) const
{
    return offsets.local().local.jacSize;
}

std::pair<count_t, count_t> gridComponent::LocalRootCount(const solverMode& /*sMode*/) const
{
    const auto& localCounts = offsets.local().local;
    return std::make_pair(localCounts.algRoots, localCounts.diffRoots);
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::loadStateSizes(const solverMode& sMode)
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
    if ((!isDynamic(sMode)) && (opFlags[no_powerflow_operations])) {
        solverOffsetsValue.stateReset();
        solverOffsetsValue.stateLoaded = true;
        return;
    }
    if ((isDynamic(sMode)) && (opFlags[no_dynamics])) {
        solverOffsetsValue.stateReset();
        solverOffsetsValue.stateLoaded = true;
    }

    if (!(solverOffsetsValue.stateLoaded)) {
        if (!isLocal(sMode))  // don't reset if it is the local offsets
        {
            solverOffsetsValue.stateReset();
        }
        auto selfSizes = LocalStateSizes(sMode);
        if (hasAlgebraic(sMode)) {
            solverOffsetsValue.local.aSize = selfSizes.aSize;
            solverOffsetsValue.local.vSize = selfSizes.vSize;
            solverOffsetsValue.local.algSize = selfSizes.algSize;
        }
        if (hasDifferential(sMode)) {
            solverOffsetsValue.local.diffSize = selfSizes.diffSize;
        }
    }

    if (opFlags[sampled_only])  // no states
    {
        if (sMode == cLocalSolverMode) {
            for (auto& sub : subObjectList) {
                sub->setFlag("sampled_only");
            }
        } else {
            solverOffsetsValue.local.reset();
            solverOffsetsValue.total.reset();
        }
    }
    if (subObjectList.empty()) {
        solverOffsetsValue.localStateLoad(true);
    } else {
        loadSizesSub(sMode, sizeCategory::state_size_update);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::loadRootSizes(const solverMode& sMode)
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
        loadSizesSub(sMode, sizeCategory::root_size_update);
    }
    if ((solverOffsetsValue.total.diffRoots > 0) || (solverOffsetsValue.total.algRoots > 0)) {
        opFlags.set(has_roots);
        if (solverOffsetsValue.total.algRoots > 0) {
            opFlags.set(has_alg_roots);
        }
    } else {
        opFlags.reset(has_roots);
        opFlags.reset(has_alg_roots);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::loadJacobianSizes(const solverMode& sMode)
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

    auto selfJacCount = LocalJacobianCount(sMode);

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        solverOffsetsValue.JacobianCountReset();
    }

    if (!(solverOffsetsValue.jacobianLoaded)) {
        solverOffsetsValue.local.jacSize = selfJacCount;
    }

    if (subObjectList.empty()) {
        solverOffsetsValue.total.jacSize = solverOffsetsValue.local.jacSize;
        solverOffsetsValue.jacobianLoaded = true;
    } else {
        loadSizesSub(sMode, sizeCategory::jacobian_size_update);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::getTols(double tols[], const solverMode& sMode)
{
    for (auto& subObj : subObjectList) {
        if (subObj->isEnabled()) {
            subObj->getTols(tols, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::getVariableType(double sdata[], const solverMode& sMode)
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

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<int, int> alertFlags{
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

void gridComponent::alert(coreObject* object, int code)
{
    if ((code >= MIN_CHANGE_ALERT) && (code <= MAX_CHANGE_ALERT)) {
        auto res = alertFlags.find(code);
        if (res != alertFlags.end()) {
            if (!opFlags[disable_flag_updates]) {
                updateFlags();
            } else {
                opFlags.set(flag_update_required);
            }
            switch (res->second) {
                case 3:
                    offsets.stateUnload();
                    offsets.JacobianUnload(true);
                    break;
                case 2:
                    offsets.rootUnload(true);
                    break;
                case 4:
                    offsets.JacobianUnload(true);
                    break;
                case 5:
                    offsets.stateUnload();
                    offsets.JacobianUnload(true);
                    offsets.rootUnload(true);
                    break;
                default:
                    break;
            }
        }
    }
    coreObject::alert(object, code);
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::getConstraints(double constraints[], const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if ((subobj->isEnabled()) && (subobj->checkFlag(has_constraints))) {
            subobj->getConstraints(constraints, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::setRootOffset(index_t newRootOffset, const solverMode& sMode)
{
    offsets.setRootOffset(newRootOffset, sMode);
    auto& solverOffsetsValue = offsets.getOffsets(sMode);
    auto nextRootOffset = solverOffsetsValue.local.algRoots + solverOffsetsValue.local.diffRoots;
    for (auto& rootObject : subObjectList) {
        rootObject->setRootOffset(newRootOffset + nextRootOffset, sMode);
        nextRootOffset += rootObject->rootSize(sMode);
    }
}

static const stringVec emptyStr{};

stringVec gridComponent::localStateNames() const
{
    return emptyStr;
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::vector<stringVec> inputNamesStr{
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

const std::vector<stringVec>& gridComponent::inputNames() const
{
    return inputNamesStr;
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::vector<stringVec> outputNamesStr{
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

const std::vector<stringVec>& gridComponent::outputNames() const
{
    return outputNamesStr;
}

units::unit gridComponent::inputUnits(index_t /*inputNum*/) const
{  // just return the default unit
    return units::defunit;
}

units::unit gridComponent::outputUnits(index_t /*outputNum*/) const
{  // just return the default unit
    return units::defunit;
}

// NOLINTNEXTLINE(misc-no-recursion)
index_t gridComponent::findIndex(std::string_view field, const solverMode& sMode) const
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
        if (!opFlags[dyn_initialized]) {
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
        if (!opFlags[dyn_initialized]) {
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
            if (!opFlags[dyn_initialized]) {
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
void gridComponent::getStateName(stringVec& stNames,
                                 const solverMode& sMode,
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
                stNames[solverOffsetsValue.algOffset + kk] =
                    prefix2 + stateNames[stateNameIndex];
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

void gridComponent::updateFlags(bool dynamicsFlags)
{
    for (auto& subobj : subObjectList) {
        if (subobj->isEnabled()) {
            opFlags |= subobj->cascadingFlags();
        }
    }
    if (opFlags[dyn_initialized] && dynamicsFlags) {
        setupDynFlags();
    } else {
        setupPFlowFlags();
    }

    opFlags.reset(flag_update_required);
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::updateLocalCache(const IOdata& inputs,
                                     const stateData& stateDataValue,
                                     const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        sub->updateLocalCache(inputs, stateDataValue, sMode);
    }
}

coreObject* gridComponent::find(std::string_view object) const
{
    auto foundobj =
        std::find_if(subObjectList.begin(), subObjectList.end(), [object](gridComponent* comp) {
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
    return coreObject::find(object);
}

coreObject* gridComponent::getSubObject(std::string_view typeName, index_t objectNum) const
{
    if ((typeName == "sub") || (typeName == "subobject") || (typeName == "object")) {
        if (isValidIndex(objectNum, subObjectList)) {
            return subObjectList[objectNum];
        }
    }
    return nullptr;
}

coreObject* gridComponent::findByUserID(std::string_view typeName, index_t searchID) const
{
    if ((typeName == "sub") || (typeName == "subobject") || (typeName == "object")) {
        auto foundobj = std::find_if(subObjectList.begin(),
                                     subObjectList.end(),
                                     [searchID](gridComponent* comp) {
                                         return (comp->getUserID() == searchID);
                                     });
        if (foundobj == subObjectList.end()) {
            return nullptr;
        }
        return *foundobj;
    }
    return coreObject::findByUserID(typeName, searchID);
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    prevTime = time;

    for (auto& subobj : subObjectList) {
        if (subobj->currentTime() < time) {
            if (!subobj->checkFlag(separate_processing)) {
                subobj->timestep(time, inputs, sMode);
            }
        }
    }
}

void gridComponent::ioPartialDerivatives(const IOdata& /*inputs*/,
                                         const stateData& /*sD*/,
                                         matrixData<double>& /*md*/,
                                         const IOlocs& /*inputLocs*/,
                                         const solverMode& /*sMode*/)
{
    /* there is no way to determine partial derivatives of the output with respect to input in a
    default manner therefore the default is no dependencies
    */
}

void gridComponent::outputPartialDerivatives(const IOdata& /*inputs*/,
                                             const stateData& /*sD*/,
                                             matrixData<double>& matrixDataValue,
                                             const solverMode& sMode)
{
    /* assume the output is a state and compute accordingly*/
    for (index_t kk = 0; kk < m_outputSize; ++kk) {
        const index_t outputLoc = getOutputLoc(sMode, kk);
        matrixDataValue.assignCheckCol(kk, outputLoc, 1.0);
    }
}

count_t gridComponent::outputDependencyCount(index_t outputNum, const solverMode& sMode) const
{
    /* assume the output is a state and act accordingly*/

    const index_t outputLoc = getOutputLoc(sMode, outputNum);
    return (outputLoc == kInvalidLocation) ? 0 : 1;
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::preEx(const IOdata& inputs,
                          const stateData& stateDataValue,
                          const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(preEx_requested))) {
            continue;
        }
        if (!subobj->checkFlag(separate_processing)) {
            subobj->preEx(inputs, stateDataValue, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::residual(const IOdata& inputs,
                             const stateData& stateDataValue,
                             double resid[],
                             const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->stateSize(sMode) > 0) {
                sub->residual(inputs, stateDataValue, resid, sMode);
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::derivative(const IOdata& inputs,
                               const stateData& stateDataValue,
                               double deriv[],
                               const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->diffSize(sMode) > 0) {
                sub->derivative(inputs, stateDataValue, deriv, sMode);
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::algebraicUpdate(const IOdata& inputs,
                                    const stateData& stateDataValue,
                                    double update[],
                                    const solverMode& sMode,
                                    double alpha)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->algSize(sMode) > 0) {
                sub->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::jacobianElements(const IOdata& inputs,
                                     const stateData& stateDataValue,
                                     matrixData<double>& matrixDataValue,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->stateSize(sMode) > 0) {
                sub->jacobianElements(
                    inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
            }
        }
    }
}
// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::rootTest(const IOdata& inputs,
                             const stateData& stateDataValue,
                             double roots[],
                             const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!subobj->checkFlag(separate_processing)) {
            if (!(subobj->checkFlag(has_roots))) {
                continue;
            }
            subobj->rootTest(inputs, stateDataValue, roots, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void gridComponent::rootTrigger(coreTime time,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(has_roots))) {
            continue;
        }
        if (!subobj->checkFlag(separate_processing)) {
            subobj->rootTrigger(time, inputs, rootMask, sMode);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
change_code gridComponent::rootCheck(const IOdata& inputs,
                                     const stateData& stateDataValue,
                                     const solverMode& sMode,
                                     check_level_t level)
{
    auto ret = change_code::no_change;

    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(has_roots))) {
            continue;
        }
        if (!subobj->checkFlag(separate_processing)) {
            ret = std::max(subobj->rootCheck(inputs, stateDataValue, sMode, level), ret);
        }
    }
    return ret;
}

index_t gridComponent::lookupOutputIndex(std::string_view outputName) const
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
    const auto& defOutputStr = gridComponent::outputNames();
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

double gridComponent::getOutput(const IOdata& /*inputs*/,
                                const stateData& stateDataValue,
                                const solverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullVal;
    }
    auto locations = offsets.getLocations(stateDataValue, sMode, this);
    if (opFlags[differential_output]) {
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

double gridComponent::getOutput(index_t outputNum) const
{
    return getOutput(noInputs, emptyStateData, cLocalSolverMode, outputNum);
}

IOdata gridComponent::getOutputs(const IOdata& inputs,
                                 const stateData& stateDataValue,
                                 const solverMode& sMode) const
{
    IOdata mout(m_outputSize);
    for (count_t pp = 0; pp < m_outputSize; ++pp) {
        mout[pp] = getOutput(inputs, stateDataValue, sMode, pp);
    }
    return mout;
}

// static IOdata kNullVec;

double gridComponent::getDoutdt(const IOdata& /*inputs*/,
                                const stateData& stateDataValue,
                                const solverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullVal;
    }
    auto locations = offsets.getLocations(stateDataValue, sMode, this);
    if (opFlags[differential_output]) {
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

index_t gridComponent::getOutputLoc(const solverMode& sMode, index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullLocation;
    }

    if (opFlags[differential_output]) {
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

IOlocs gridComponent::getOutputLocs(const solverMode& sMode) const
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

void gridComponent::setParameter(index_t param, double /*value*/)
{
    throw(unrecognizedParameter("param" + std::to_string(param)));
}
double gridComponent::getParameter(index_t param) const
{
    throw(unrecognizedParameter("param" + std::to_string(param)));
}
void gridComponent::parameterPartialDerivatives(index_t param,
                                                double /*val*/,
                                                const IOdata& /*inputs*/,
                                                const stateData& /*sD*/,
                                                matrixData<double>& /*md*/,
                                                const solverMode& /*sMode*/)
{
    throw(unrecognizedParameter("param" + std::to_string(param)));
}

double gridComponent::parameterOutputPartialDerivatives(index_t param,
                                                        double /*val*/,
                                                        index_t /*outputNum*/,
                                                        const IOdata& /*inputs*/,
                                                        const stateData& /*sD*/,
                                                        const solverMode& /*sMode*/)
{
    throw(unrecognizedParameter("param" + std::to_string(param)));
}

void printStateNames(const gridComponent* comp, const solverMode& sMode)
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
