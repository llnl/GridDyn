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
    for (auto& so : subObjectList) {
        removeReference(so, this);
    }
}

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
                // this likely means that the parent will take care of it itself
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
                        // this likely means that the derived class will take care of it itself
                    }
                }
            } else {
                if (ii >= csz) {
                    try {
                        nobj->add(subObjectList[ii]->clone());
                    }
                    catch (const unrecognizedObjectException&) {
                        // this likely means that the parent will take care of it itself
                    }
                } else {
                    if (typeid(subObjectList[ii]) == typeid(nobj->subObjectList[ii])) {
                        subObjectList[ii]->clone(nobj->subObjectList[ii]);
                    } else {
                        try {
                            nobj->add(subObjectList[ii]->clone());
                        }
                        catch (const unrecognizedObjectException&) {
                            // this likely means that the parent will take care of it itself
                        }
                    }
                }
            }
        }
    }

    return nobj;
}

void gridComponent::updateObjectLinkages(coreObject* newRoot)
{
    for (auto* subobj : getSubObjects()) {
        subobj->updateObjectLinkages(newRoot);
    }
}

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

void gridComponent::pFlowInitializeB()
{
    if (isEnabled()) {
        pFlowObjectInitializeB();
        opFlags.set(pFlow_initialized);
    }
}

void gridComponent::dynInitializeA(coreTime time0, std::uint32_t flags)
{
    if (isEnabled()) {
        dynObjectInitializeA(time0, flags);
        prevTime = time0;
        updateFlags(true);
        setupDynFlags();
    }
}

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

void gridComponent::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& subobj : subObjectList) {
        subobj->pFlowInitializeA(time0, flags);
    }
}

void gridComponent::pFlowObjectInitializeB()
{
    for (auto& subobj : subObjectList) {
        subobj->pFlowInitializeB();
    }
}

void gridComponent::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& subobj : subObjectList) {
        subobj->dynInitializeA(time0, flags);
    }
}

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
    const auto& so = offsets.getOffsets(sMode);
    if (!(so.stateLoaded)) {
        loadStateSizes(sMode);
    }
    count_t ssize =
        (hasAlgebraic(sMode)) ? (so.total.algSize + so.total.vSize + so.total.aSize) : 0;
    if (hasDifferential(sMode)) {
        ssize += so.total.diffSize;
    }
    return ssize;
}

count_t gridComponent::stateSize(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    count_t ssize =
        (hasAlgebraic(sMode)) ? (so.total.algSize + so.total.vSize + so.total.aSize) : 0;
    if (hasDifferential(sMode)) {
        ssize += so.total.diffSize;
    }
    return ssize;
}

count_t gridComponent::totalAlgSize(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return so.total.algSize + so.total.vSize + so.total.aSize;
}

count_t gridComponent::totalAlgSize(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    return (so.total.algSize + so.total.vSize + so.total.aSize);
}

count_t gridComponent::algSize(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return so.total.algSize;
}

count_t gridComponent::algSize(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    return so.total.algSize;
}

count_t gridComponent::diffSize(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return so.total.diffSize;
}

count_t gridComponent::diffSize(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    return so.total.diffSize;
}

count_t gridComponent::rootSize(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.rootsLoaded)) {
        loadRootSizes(sMode);
    }
    return so.total.algRoots + so.total.diffRoots;
}

count_t gridComponent::rootSize(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);

    return so.total.algRoots + so.total.diffRoots;
}

count_t gridComponent::jacSize(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.jacobianLoaded)) {
        loadJacobianSizes(sMode);
    }
    return so.total.jacSize;
}

count_t gridComponent::jacSize(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    return so.total.jacSize;
}

count_t gridComponent::voltageStateCount(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return so.total.vSize;
}

count_t gridComponent::voltageStateCount(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    return so.total.vSize;
}

count_t gridComponent::angleStateCount(const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (!(so.stateLoaded)) {
        loadStateSizes(sMode);
    }
    return so.total.aSize;
}

count_t gridComponent::angleStateCount(const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    return so.total.aSize;
}

const solverOffsets& gridComponent::getOffsets(const solverMode& sMode) const
{
    return offsets.getOffsets(sMode);
}
void gridComponent::setOffsets(const solverOffsets& newOffsets, const solverMode& sMode)
{
    offsets.setOffsets(newOffsets, sMode);

    if (!subObjectList.empty()) {
        solverOffsets no(newOffsets);
        no.localIncrement(offsets.getOffsets(sMode));
        for (auto& subobj : subObjectList) {
            if (subobj->isEnabled()) {
                subobj->setOffsets(no, sMode);
                no.increment(subobj->offsets.getOffsets(sMode));
            }
        }
    }
}

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

static const std::map<std::string, operation_flags> user_settable_flags{
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
    auto ffind = user_settable_flags.find(std::string{flag});
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

static const std::map<std::string, operation_flags> flagmap{
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
    auto flagfind = flagmap.find(std::string{flag});
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
static const stringVec locNumStrings{"status", "basefrequency", "basepower"};
static const stringVec locStrStrings{"status"};

void gridComponent::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<gridComponent, coreObject>(this, pstr, locNumStrings, locStrStrings, {}, pstype);
}

void gridComponent::set(std::string_view param, std::string_view val)
{
    if (opFlags[no_gridcomponent_set]) {
        throw(unrecognizedParameter(std::string{param}));
    }

    if (param == "status") {
        auto v2 = convertToLowerCase(std::string{val});
        if ((v2 == "on") || (v2 == "in") || (v2 == "enabled")) {
            if (!isEnabled()) {
                enable();
                if ((opFlags[has_pflow_states]) || (opFlags[has_dyn_states])) {
                    alert(this, STATE_COUNT_CHANGE);
                }
            }
        } else if ((v2 == "off") || (v2 == "out") || (v2 == "disabled")) {
            if (isEnabled()) {
                if ((opFlags[has_pflow_states]) || (opFlags[has_dyn_states])) {
                    alert(this, STATE_COUNT_CHANGE);
                }
                disable();
            }
        } else if (v2 == "connected") {
            if (!isConnected()) {
                reconnect();
            }
        } else if (v2 == "disconnected") {
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

auto hasParameterPath(std::string_view param)
{
    return (param.find_last_of(":?") != std::string_view::npos);
}
bool gridComponent::subObjectSet(std::string_view param, double val, units::unit unitType)
{
    if (hasParameterPath(param)) {
        objInfo pinfo(std::string{param}, this);
        if (pinfo.m_obj != nullptr) {
            if (pinfo.m_unitType != units::defunit) {
                pinfo.m_obj->set(pinfo.m_field, val, pinfo.m_unitType);
            } else {
                pinfo.m_obj->set(pinfo.m_field, val, unitType);
            }
            return true;
        }
        throw(unrecognizedParameter(std::string{param}));
    }
    return false;
}

bool gridComponent::subObjectSet(std::string_view param, std::string_view val)
{
    if (hasParameterPath(param)) {
        objInfo pinfo(std::string{param}, this);
        if (pinfo.m_obj != nullptr) {
            pinfo.m_obj->set(pinfo.m_field, val);
        } else {
            throw(unrecognizedParameter(std::string{param}));
        }
        return true;
    }
    return false;
}

bool gridComponent::subObjectSet(std::string_view flag, bool val)
{
    if (hasParameterPath(flag)) {
        objInfo pinfo(std::string{flag}, this);
        if (pinfo.m_obj != nullptr) {
            pinfo.m_obj->setFlag(pinfo.m_field, val);
        } else {
            throw(unrecognizedParameter(std::string{flag}));
        }
        return true;
    }
    return false;
}

double gridComponent::subObjectGet(std::string_view param, units::unit unitType) const
{
    if (hasParameterPath(param)) {
        objInfo pinfo(std::string{param}, this);
        if (pinfo.m_obj != nullptr) {
            if (pinfo.m_unitType != units::defunit) {
                return pinfo.m_obj->get(pinfo.m_field, pinfo.m_unitType);
            }
            return pinfo.m_obj->get(pinfo.m_field, unitType);
        }
        throw(unrecognizedParameter(std::string{param}));
    }
    return kNullVal;
}

void gridComponent::set(std::string_view param, double val, units::unit unitType)
{
    if (opFlags[no_gridcomponent_set]) {
        throw(unrecognizedParameter(std::string{param}));
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
    if (std::any_of(subObjectList.begin(), subObjectList.end(), [comp](const coreObject* so) {
            return isSameObject(so, comp);
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
                                  [obj](coreObject* so) { return isSameObject(so, obj); });
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
        return addSubObject(newObj);
    }
    if (newObj == nullptr) {
        return removeSubObject(oldObj);
    }
    auto repobj = std::find_if(subObjectList.begin(),
                               subObjectList.end(),
                               [oldObj](const coreObject* so) { return isSameObject(so, oldObj); });
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
        return addSubObject(newObj);
    }
}
void gridComponent::remove(coreObject* obj)
{
    if (dynamic_cast<gridComponent*>(obj) != nullptr) {
        removeSubObject(static_cast<gridComponent*>(obj));
    }
}

void gridComponent::reset(reset_levels level)
{
    for (auto& subobj : subObjectList) {
        subobj->reset(level);
    }
}

change_code
    gridComponent::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level)
{
    auto ret = change_code::no_change;

    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(has_powerflow_adjustments))) {
            continue;
        }
        auto iret = subobj->powerFlowAdjust(inputs, flags, level);
        if (iret > ret) {
            ret = iret;
        }
    }
    return ret;
}

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
    const auto& so = offsets.getOffsets(sMode);
    const auto& localStates = (subObjectList.empty()) ? (so.total) : (so.local);

    if (hasAlgebraic(sMode)) {
        if (localStates.algSize > 0) {
            std::copy(state + so.algOffset,
                      state + so.algOffset + localStates.algSize,
                      m_state.data());
        }
    }
    if (localStates.diffSize > 0) {
        if (isDifferentialOnly(sMode)) {
            std::copy(state + so.diffOffset,
                      state + so.diffOffset + localStates.diffSize,
                      m_state.data() + algSize(cLocalSolverMode));
            std::copy(dstate_dt + so.diffOffset,
                      dstate_dt + so.diffOffset + localStates.diffSize,
                      m_dstate_dt.data() + algSize(cLocalSolverMode));
        } else {
            std::copy(state + so.diffOffset,
                      state + so.diffOffset + localStates.diffSize,
                      m_state.data() + localStates.algSize);
            std::copy(dstate_dt + so.diffOffset,
                      dstate_dt + so.diffOffset + localStates.diffSize,
                      m_dstate_dt.data() + localStates.algSize);
        }
    }

    for (auto& sub : subObjectList) {
        sub->setState(time, state, dstate_dt, sMode);
    }
}
// for saving the state
void gridComponent::guessState(coreTime time,
                               double state[],
                               double dstate_dt[],
                               const solverMode& sMode)
{
    if (!hasStates(sMode)) {
        return;
    }
    const auto& so = offsets.getOffsets(sMode);
    const auto& localStates = (subObjectList.empty()) ? (so.total) : (so.local);

    if (hasAlgebraic(sMode)) {
        if (localStates.algSize > 0) {
            assert(so.algOffset != kNullLocation);
            std::copy(m_state.begin(), m_state.begin() + localStates.algSize, state + so.algOffset);
        }
    }
    if (localStates.diffSize > 0) {
        if (isDifferentialOnly(sMode)) {
            assert(so.diffOffset != kNullLocation);
            index_t localAlgSize = algSize(cLocalSolverMode);
            std::copy(m_state.begin() + localAlgSize,
                      m_state.begin() + localAlgSize + localStates.diffSize,
                      state + so.diffOffset);
            std::copy(m_dstate_dt.data() + localAlgSize,
                      m_dstate_dt.data() + localAlgSize + localStates.diffSize,
                      dstate_dt + so.diffOffset);
        } else {
            if (so.diffOffset == kNullLocation) {
                std::cout << std::format("{}::{} in mode {} {} ds={}, do={}\n",
                                         getParent()->getName(),
                                         getName(),
                                         static_cast<int>(isLocal(sMode)),
                                         static_cast<int>(isDAE(sMode)),
                                         static_cast<int>(so.total.diffSize),
                                         static_cast<int>(so.diffOffset));
                // printStackTrace ();
            }
            assert(so.diffOffset != kNullLocation);
            count_t stateCount = localStates.algSize + localStates.diffSize;
            std::copy(m_state.begin() + localStates.algSize,
                      m_state.begin() + stateCount,
                      state + so.diffOffset);
            std::copy(m_dstate_dt.data() + localStates.algSize,
                      m_dstate_dt.data() + stateCount,
                      dstate_dt + so.diffOffset);
        }
    }

    for (auto& sub : subObjectList) {
        sub->guessState(time, state, dstate_dt, sMode);
    }
}

void gridComponent::setupPFlowFlags()
{
    auto ss = stateSize(cPflowSolverMode);
    opFlags.set(has_pflow_states, (ss > 0));
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
    auto ss = stateSize(cDaeSolverMode);

    opFlags.set(has_dyn_states, (ss > 0));
    const auto& so = offsets.getOffsets(cDaeSolverMode);
    if (so.total.algRoots > 0) {
        opFlags.set(has_alg_roots);
        opFlags.set(has_roots);
    } else if (so.total.diffRoots > 0) {
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

void gridComponent::loadSizesSub(const solverMode& sMode, sizeCategory category)
{
    auto& so = offsets.getOffsets(sMode);
    switch (category) {
        case sizeCategory::state_size_update:
            so.localStateLoad(false);
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isStateCountLoaded(sMode))) {
                        sub->loadStateSizes(sMode);
                    }
                    if (sub->checkFlag(sampled_only)) {
                        continue;
                    }
                    so.addStateSizes(sub->offsets.getOffsets(sMode));
                }
            }
            so.stateLoaded = true;
            break;
        case sizeCategory::jacobian_size_update:
            so.total.jacSize = so.local.jacSize;
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isJacobianCountLoaded(sMode))) {
                        sub->loadJacobianSizes(sMode);
                    }
                    if (sub->checkFlag(sampled_only)) {
                        continue;
                    }
                    so.addJacobianSizes(sub->offsets.getOffsets(sMode));
                }
            }
            so.jacobianLoaded = true;
            break;
        case sizeCategory::root_size_update:
            so.total.algRoots = so.local.algRoots;
            so.total.diffRoots = so.local.diffRoots;
            for (auto& sub : subObjectList) {
                if (sub->isEnabled()) {
                    if (!(sub->isRootCountLoaded(sMode))) {
                        sub->loadRootSizes(sMode);
                    }
                    if (sub->checkFlag(sampled_only)) {
                        continue;
                    }
                    so.addRootSizes(sub->offsets.getOffsets(sMode));
                }
            }
            so.rootsLoaded = true;
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
    const auto& lc = offsets.local().local;
    return std::make_pair(lc.algRoots, lc.diffRoots);
}

void gridComponent::loadStateSizes(const solverMode& sMode)
{
    if (isStateCountLoaded(sMode)) {
        return;
    }
    auto& so = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        so.reset();
        so.setLoaded();
        return;
    }
    if ((!isDynamic(sMode)) && (opFlags[no_powerflow_operations])) {
        so.stateReset();
        so.stateLoaded = true;
        return;
    }
    if ((isDynamic(sMode)) && (opFlags[no_dynamics])) {
        so.stateReset();
        so.stateLoaded = true;
    }

    if (!(so.stateLoaded)) {
        if (!isLocal(sMode))  // don't reset if it is the local offsets
        {
            so.stateReset();
        }
        auto selfSizes = LocalStateSizes(sMode);
        if (hasAlgebraic(sMode)) {
            so.local.aSize = selfSizes.aSize;
            so.local.vSize = selfSizes.vSize;
            so.local.algSize = selfSizes.algSize;
        }
        if (hasDifferential(sMode)) {
            so.local.diffSize = selfSizes.diffSize;
        }
    }

    if (opFlags[sampled_only])  // no states
    {
        if (sMode == cLocalSolverMode) {
            for (auto& sub : subObjectList) {
                sub->setFlag("sampled_only");
            }
        } else {
            so.local.reset();
            so.total.reset();
        }
    }
    if (subObjectList.empty()) {
        so.localStateLoad(true);
    } else {
        loadSizesSub(sMode, sizeCategory::state_size_update);
    }
}

void gridComponent::loadRootSizes(const solverMode& sMode)
{
    if (isRootCountLoaded(sMode)) {
        return;
    }
    auto& so = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        so.reset();
        so.setLoaded();
        return;
    }
    if (!isDynamic(sMode)) {
        so.rootCountReset();
        so.rootsLoaded = true;
        return;
    }

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        so.rootCountReset();
    }
    auto selfSizes = LocalRootCount(sMode);
    if (!(so.rootsLoaded)) {
        so.local.algRoots = selfSizes.first;
        so.local.diffRoots = selfSizes.second;
    }

    if (subObjectList.empty()) {
        so.total.algRoots = so.local.algRoots;
        so.total.diffRoots = so.local.diffRoots;
        so.rootsLoaded = true;
    } else {
        loadSizesSub(sMode, sizeCategory::root_size_update);
    }
    if ((so.total.diffRoots > 0) || (so.total.algRoots > 0)) {
        opFlags.set(has_roots);
        if (so.total.algRoots > 0) {
            opFlags.set(has_alg_roots);
        }
    } else {
        opFlags.reset(has_roots);
        opFlags.reset(has_alg_roots);
    }
}

void gridComponent::loadJacobianSizes(const solverMode& sMode)
{
    if (isJacobianCountLoaded(sMode)) {
        return;
    }
    auto& so = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        so.reset();
        so.setLoaded();
        return;
    }

    auto selfJacCount = LocalJacobianCount(sMode);

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        so.JacobianCountReset();
    }

    if (!(so.jacobianLoaded)) {
        so.local.jacSize = selfJacCount;
    }

    if (subObjectList.empty()) {
        so.total.jacSize = so.local.jacSize;
        so.jacobianLoaded = true;
    } else {
        loadSizesSub(sMode, sizeCategory::jacobian_size_update);
    }
}

void gridComponent::getTols(double tols[], const solverMode& sMode)
{
    for (auto& subObj : subObjectList) {
        if (subObj->isEnabled()) {
            subObj->getTols(tols, sMode);
        }
    }
}

void gridComponent::getVariableType(double sdata[], const solverMode& sMode)
{
    auto& so = offsets.getOffsets(sMode);
    if (subObjectList.empty()) {
        if (so.total.algSize > 0) {
            auto offset = so.algOffset;
            for (index_t kk = 0; kk < so.total.algSize; ++kk) {
                sdata[offset + kk] = ALGEBRAIC_VARIABLE;
            }
        }
        if (so.total.diffSize > 0) {
            auto offset = so.diffOffset;
            for (index_t kk = 0; kk < so.total.diffSize; ++kk) {
                sdata[offset + kk] = DIFFERENTIAL_VARIABLE;
            }
        }
    } else {
        if (so.local.algSize > 0) {
            auto offset = so.algOffset;
            for (index_t kk = 0; kk < so.local.algSize; ++kk) {
                sdata[offset + kk] = ALGEBRAIC_VARIABLE;
            }
        }
        if (so.local.diffSize > 0) {
            auto offset = so.diffOffset;
            for (index_t kk = 0; kk < so.local.diffSize; ++kk) {
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

void gridComponent::getConstraints(double constraints[], const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if ((subobj->isEnabled()) && (subobj->checkFlag(has_constraints))) {
            subobj->getConstraints(constraints, sMode);
        }
    }
}

void gridComponent::setRootOffset(index_t newRootOffset, const solverMode& sMode)
{
    offsets.setRootOffset(newRootOffset, sMode);
    auto& so = offsets.getOffsets(sMode);
    auto nR = so.local.algRoots + so.local.diffRoots;
    for (auto& ro : subObjectList) {
        ro->setRootOffset(newRootOffset + nR, sMode);
        nR += ro->rootSize(sMode);
    }
}

static const stringVec emptyStr{};

stringVec gridComponent::localStateNames() const
{
    return emptyStr;
}

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

index_t gridComponent::findIndex(std::string_view field, const solverMode& sMode) const
{
    const auto& so = offsets.getOffsets(sMode);
    if (field.compare(0, 5, "state") == 0) {
        auto num = static_cast<index_t>(
            gmlc::utilities::stringOps::trailingStringInt(std::string{field}, 0));
        if (stateSize(sMode) > num) {
            if (so.algOffset != kNullLocation) {
                return so.algOffset + num;
            }
            return kNullLocation;
        }
        return kInvalidLocation;
    }
    if (field.compare(0, 3, "alg") == 0) {
        auto num = static_cast<index_t>(
            gmlc::utilities::stringOps::trailingStringInt(std::string{field}, 0));
        if (so.total.algSize > num) {
            if (so.algOffset != kNullLocation) {
                return so.algOffset + num;
            }
            return kNullLocation;
        }
        if (!opFlags[dyn_initialized]) {
            return kNullLocation;
        }
        return kInvalidLocation;
    }
    if (field.compare(0, 4, "diff") == 0) {
        auto num = static_cast<index_t>(
            gmlc::utilities::stringOps::trailingStringInt(std::string{field}, 0));
        if (so.total.diffSize > num) {
            if (so.diffOffset != kNullLocation) {
                return so.diffOffset + num;
            }
            return kNullLocation;
        }
        if (!opFlags[dyn_initialized]) {
            return kNullLocation;
        }
        return kInvalidLocation;
    }
    auto stateNames = localStateNames();
    for (index_t nn = 0; nn < static_cast<index_t>(stateNames.size()); ++nn) {
        if (field == stateNames[nn]) {
            const auto& lc = offsets.local();
            if (nn < lc.local.algSize) {
                if (so.algOffset != kNullLocation) {
                    return so.algOffset + nn;
                }
                return kNullLocation;
            }
            if (nn - lc.local.algSize < lc.local.diffSize) {
                if (so.diffOffset != kNullLocation) {
                    return so.diffOffset + nn - lc.local.algSize;
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

void gridComponent::getStateName(stringVec& stNames,
                                 const solverMode& sMode,
                                 const std::string& prefix) const
{
    const auto& so = offsets.getOffsets(sMode);
    auto mxsize = offsets.maxIndex(sMode);
    std::string prefix2 = prefix + getName() + ':';

    if ((mxsize > 0) && (so.stateLoaded)) {
        ensureSizeAtLeast(stNames, mxsize + 1);
    } else {
        return;
    }
    auto stateNames = localStateNames();
    auto stsize = static_cast<index_t>(stateNames.size());
    decltype(mxsize) ss = 0;
    if (hasAlgebraic(sMode)) {
        for (index_t kk = 0; kk < so.local.vSize; kk++) {
            if (!stNames[so.vOffset + kk].empty()) {
                continue;
            }
            if (stsize > ss) {
                stNames[so.vOffset + kk] = prefix2 + stateNames[ss];
                ++ss;
            } else {
                stNames[so.vOffset + kk] = prefix2 + "voltage_state_" + std::to_string(kk);
            }
        }
        ss = offsets.local().local.vSize;
        for (index_t kk = 0; kk < so.local.aSize; kk++) {
            if (!stNames[so.aOffset + kk].empty()) {
                continue;
            }
            if (stsize > ss) {
                stNames[so.aOffset + kk] = prefix2 + stateNames[ss];
                ++ss;
            } else {
                stNames[so.aOffset + kk] = prefix2 + "angle_state_" + std::to_string(kk);
            }
        }
        ss = offsets.local().local.vSize + offsets.local().local.aSize;
        for (index_t kk = 0; kk < so.local.algSize; kk++) {
            if (!stNames[so.algOffset + kk].empty()) {
                continue;
            }
            if (stsize > ss) {
                stNames[so.algOffset + kk] = prefix2 + stateNames[ss];
                ++ss;
            } else {
                stNames[so.algOffset + kk] = prefix2 + "alg_state_" + std::to_string(kk);
            }
        }
    }
    if (!isAlgebraicOnly(sMode)) {
        if (so.local.diffSize > 0) {
            ss = offsets.local().local.algSize + offsets.local().local.vSize +
                offsets.local().local.aSize;
            for (index_t kk = 0; kk < so.local.diffSize; kk++) {
                if (!stNames[so.diffOffset + kk].empty()) {
                    continue;
                }
                if (stsize > ss) {
                    stNames[so.diffOffset + kk] = prefix2 + stateNames[ss];
                    ++ss;
                } else {
                    stNames[so.diffOffset + kk] = prefix2 + "diff_state_" + std::to_string(kk);
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
    if ((opFlags[dyn_initialized]) && (dynamicsFlags)) {
        setupDynFlags();
    } else {
        setupPFlowFlags();
    }

    opFlags.reset(flag_update_required);
}

void gridComponent::updateLocalCache(const IOdata& inputs,
                                     const stateData& sD,
                                     const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        sub->updateLocalCache(inputs, sD, sMode);
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
                                             matrixData<double>& md,
                                             const solverMode& sMode)
{
    /* assume the output is a state and compute accordingly*/
    for (index_t kk = 0; kk < m_outputSize; ++kk) {
        index_t oLoc = getOutputLoc(sMode, kk);
        md.assignCheckCol(kk, oLoc, 1.0);
    }
}

count_t gridComponent::outputDependencyCount(index_t outputNum, const solverMode& sMode) const
{
    /* assume the output is a state and act accordingly*/

    index_t oLoc = getOutputLoc(sMode, outputNum);
    return (oLoc == kInvalidLocation) ? 0 : 1;
}

void gridComponent::preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(preEx_requested))) {
            continue;
        }
        if (!subobj->checkFlag(separate_processing)) {
            subobj->preEx(inputs, sD, sMode);
        }
    }
}

void gridComponent::residual(const IOdata& inputs,
                             const stateData& sD,
                             double resid[],
                             const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->stateSize(sMode) > 0) {
                sub->residual(inputs, sD, resid, sMode);
            }
        }
    }
}

void gridComponent::derivative(const IOdata& inputs,
                               const stateData& sD,
                               double deriv[],
                               const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->diffSize(sMode) > 0) {
                sub->derivative(inputs, sD, deriv, sMode);
            }
        }
    }
}

void gridComponent::algebraicUpdate(const IOdata& inputs,
                                    const stateData& sD,
                                    double update[],
                                    const solverMode& sMode,
                                    double alpha)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->algSize(sMode) > 0) {
                sub->algebraicUpdate(inputs, sD, update, sMode, alpha);
            }
        }
    }
}

void gridComponent::jacobianElements(const IOdata& inputs,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    for (auto& sub : subObjectList) {
        if (!sub->checkFlag(separate_processing)) {
            if (sub->stateSize(sMode) > 0) {
                sub->jacobianElements(inputs, sD, md, inputLocs, sMode);
            }
        }
    }
}
void gridComponent::rootTest(const IOdata& inputs,
                             const stateData& sD,
                             double roots[],
                             const solverMode& sMode)
{
    for (auto& subobj : subObjectList) {
        if (!subobj->checkFlag(separate_processing)) {
            if (!(subobj->checkFlag(has_roots))) {
                continue;
            }
            subobj->rootTest(inputs, sD, roots, sMode);
        }
    }
}

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

change_code gridComponent::rootCheck(const IOdata& inputs,
                                     const stateData& sD,
                                     const solverMode& sMode,
                                     check_level_t level)
{
    auto ret = change_code::no_change;

    for (auto& subobj : subObjectList) {
        if (!(subobj->checkFlag(has_roots))) {
            continue;
        }
        if (!subobj->checkFlag(separate_processing)) {
            ret = std::max(subobj->rootCheck(inputs, sD, sMode, level), ret);
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
                                const stateData& sD,
                                const solverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullVal;
    }
    auto Loc = offsets.getLocations(sD, sMode, this);
    if (opFlags[differential_output]) {
        if (Loc.diffSize > outputNum) {
            assert(Loc.diffStateLoc != nullptr);
            return Loc.diffStateLoc[outputNum];
        }
        return kNullVal;
    }
    // if differential flag was not specified try algebraic state values then differential

    if (Loc.algSize > outputNum) {
        assert(Loc.algStateLoc != nullptr);
        return Loc.algStateLoc[outputNum];
    }
    if (Loc.diffSize + Loc.algSize > outputNum) {
        assert(Loc.diffStateLoc != nullptr);
        return Loc.diffStateLoc[outputNum - Loc.algSize];
    }
    return (static_cast<index_t>(m_state.size()) > outputNum) ? m_state[outputNum] : kNullVal;
}

double gridComponent::getOutput(index_t outputNum) const
{
    return getOutput(noInputs, emptyStateData, cLocalSolverMode, outputNum);
}

IOdata gridComponent::getOutputs(const IOdata& inputs,
                                 const stateData& sD,
                                 const solverMode& sMode) const
{
    IOdata mout(m_outputSize);
    for (count_t pp = 0; pp < m_outputSize; ++pp) {
        mout[pp] = getOutput(inputs, sD, sMode, pp);
    }
    return mout;
}

// static IOdata kNullVec;

double gridComponent::getDoutdt(const IOdata& /*inputs*/,
                                const stateData& sD,
                                const solverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum >= m_outputSize) {
        return kNullVal;
    }
    auto Loc = offsets.getLocations(sD, sMode, this);
    if (opFlags[differential_output]) {
        assert(Loc.dstateLoc != nullptr);
        return Loc.dstateLoc[outputNum];
    }

    if (Loc.algSize > outputNum) {
        return 0.0;
    }
    if (Loc.diffSize + Loc.algSize > outputNum) {
        assert(Loc.dstateLoc != nullptr);
        return Loc.dstateLoc[outputNum - Loc.algSize];
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

    const auto& so = offsets.getOffsets(sMode);
    if (so.total.algSize > outputNum) {
        return so.algOffset + outputNum;
    }
    if (so.total.diffSize + so.total.algSize > outputNum) {
        return so.diffOffset - so.total.algSize + outputNum;
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
    int kk = 0;
    for (auto& sn : sNames) {
        std::cout << kk++ << ' ' << sn << '\n';
        if (kk >= ssize) {
            break;
        }
    }
}

}  // namespace griddyn
