/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridOptObjects.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn {
gridOptObject::gridOptObject(const std::string& objName): coreObject(objName) {}

coreObject* gridOptObject::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<gridOptObject, coreObject>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->numParams = numParams;
    nobj->optFlags = optFlags;
    return nobj;
}

// size getter functions
count_t gridOptObject::objSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.genSize + offsetData.total.vSize + offsetData.total.aSize +
            offsetData.total.qSize + offsetData.total.contSize + offsetData.total.intSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.genSize + offsetData.total.qSize + offsetData.total.vSize +
            offsetData.total.aSize + offsetData.total.contSize + offsetData.total.intSize;
    }
    return size;
}

count_t gridOptObject::contObjSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.genSize + offsetData.total.qSize + offsetData.total.vSize +
            offsetData.total.aSize + offsetData.total.contSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.genSize + offsetData.total.qSize + offsetData.total.vSize +
            offsetData.total.aSize + offsetData.total.contSize;
    }
    return size;
}

count_t gridOptObject::intObjSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.intSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.intSize;
    }
    return size;
}

count_t gridOptObject::genSize(const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    if (!offsetData.loaded) {
        loadSizes(oMode);
    }
    return offsetData.total.genSize;
}

count_t gridOptObject::qSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.qSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.qSize;
    }
    return size;
}

count_t gridOptObject::vSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.vSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.vSize;
    }
    return size;
}

count_t gridOptObject::aSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.aSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.aSize;
    }
    return size;
}

count_t gridOptObject::constraintSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    auto& offsetData = offsets.getOffsets(oMode);
    if (offsetData.loaded) {
        size = offsetData.total.constraintsSize;
    } else {
        loadSizes(oMode);
        size = offsetData.total.constraintsSize;
    }
    return size;
}

// size getter functions
count_t gridOptObject::objSize(const OptimizationMode& oMode) const
{
    auto& offsetData = offsets.getOffsets(oMode);
    return (offsetData.total.genSize + offsetData.total.vSize + offsetData.total.aSize +
            offsetData.total.qSize + offsetData.total.contSize + offsetData.total.intSize);
}

count_t gridOptObject::contObjSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return (offsetData.total.genSize + offsetData.total.qSize + offsetData.total.vSize +
            offsetData.total.aSize + offsetData.total.contSize);
}

count_t gridOptObject::intObjSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.intSize;
}

count_t gridOptObject::genSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.genSize;
}

count_t gridOptObject::qSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.qSize;
}

count_t gridOptObject::vSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.vSize;
}

count_t gridOptObject::aSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.aSize;
}

count_t gridOptObject::constraintSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);

    return offsetData.total.constraintsSize;
}

void gridOptObject::dynInitializeA(std::uint32_t flags)
{
    dynObjectInitializeA(flags);
}

void gridOptObject::dynInitializeB(std::uint32_t flags)
{
    dynObjectInitializeB(flags);
    optFlags.set(OPT_INITIALIZED);
}

void gridOptObject::setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    if (!offsetData.loaded) {
        loadSizes(oMode);
    }
    offsetData.setOffsets(newOffsets);
}

void gridOptObject::setOffset(index_t offset,
                              index_t constraintOffset,
                              const OptimizationMode& oMode)
{
    offsets.setOffset(offset, oMode);
    offsets.setConstraintOffset(constraintOffset, oMode);
}

void gridOptObject::set(std::string_view param, std::string_view val)
{
    if (param == "status") {
        auto v2 = gmlc::utilities::convertToLowerCase(val);
        if (val == "out") {
            if (isEnabled()) {
                disable();
            }
        } else if (val == "in") {
            if (!isEnabled()) {
                enable();
            }
        } else {
            coreObject::set(param, val);
        }
    } else {
        coreObject::set(param, val);
    }
}

void gridOptObject::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "#") {
    } else {
        coreObject::set(param, val, unitType);
    }
}

void gridOptObject::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    if (offsets.isLoaded(oMode)) {
        auto integerIndex = offsets.getIntOffset(oMode);
        auto integerSize = intObjSize(oMode);
        for (index_t variableIndex = 0; variableIndex < integerSize; ++variableIndex) {
            sdata[integerIndex + variableIndex] = INTEGER_OBJECTIVE_VARIABLE;
        }
    }
}

void gridOptObject::getObjName(stringVec& objNames,
                               const OptimizationMode& oMode,
                               const std::string& prefix)
{
    auto& offsetSet = offsets.getOffsets(oMode);
    auto ensureSize = [&objNames](count_t offset, count_t count) {
        const auto requiredSize = static_cast<size_t>(offset + count);
        if (objNames.size() < requiredSize) {
            objNames.resize(requiredSize);
        }
    };
    // angle variables
    ensureSize(offsetSet.aOffset, offsetSet.total.aSize);
    for (index_t variableIndex = 0; variableIndex < offsetSet.total.aSize; ++variableIndex) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(offsetSet.aOffset) + variableIndex] =
                getName() + ":angle_" + std::to_string(variableIndex);
        } else {
            objNames[static_cast<size_t>(offsetSet.aOffset) + variableIndex] =
                prefix + "::" + getName() + ":angle_" + std::to_string(variableIndex);
        }
    }
    // voltage variables
    ensureSize(offsetSet.vOffset, offsetSet.total.vSize);
    for (index_t variableIndex = 0; variableIndex < offsetSet.total.vSize; ++variableIndex) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(offsetSet.vOffset) + variableIndex] =
                getName() + ":voltage_" + std::to_string(variableIndex);
        } else {
            objNames[static_cast<size_t>(offsetSet.vOffset) + variableIndex] =
                prefix + "::" + getName() + ":voltage_" + std::to_string(variableIndex);
        }
    }
    // real power variables
    ensureSize(offsetSet.gOffset, offsetSet.total.genSize);
    for (index_t variableIndex = 0; variableIndex < offsetSet.total.genSize; ++variableIndex) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(offsetSet.gOffset) + variableIndex] =
                getName() + ":power_" + std::to_string(variableIndex);
        } else {
            objNames[static_cast<size_t>(offsetSet.gOffset) + variableIndex] =
                prefix + "::" + getName() + ":power_" + std::to_string(variableIndex);
        }
    }
    // angle variables
    ensureSize(offsetSet.qOffset, offsetSet.total.qSize);
    for (index_t variableIndex = 0; variableIndex < offsetSet.total.qSize; ++variableIndex) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(offsetSet.qOffset) + variableIndex] =
                getName() + ":reactive_power_" + std::to_string(variableIndex);
        } else {
            objNames[static_cast<size_t>(offsetSet.qOffset) + variableIndex] =
                prefix + "::" + getName() + ":reactive_power_" + std::to_string(variableIndex);
        }
    }
    // other continuous variables
    ensureSize(offsetSet.contOffset, offsetSet.total.contSize);
    for (index_t variableIndex = 0; variableIndex < offsetSet.total.contSize; ++variableIndex) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(offsetSet.contOffset) + variableIndex] =
                getName() + ":continuous_" + std::to_string(variableIndex);
        } else {
            objNames[static_cast<size_t>(offsetSet.contOffset) + variableIndex] =
                prefix + "::" + getName() + ":continuous_" + std::to_string(variableIndex);
        }
    }
    // integer variables
    ensureSize(offsetSet.intOffset, offsetSet.total.intSize);
    for (index_t variableIndex = 0; variableIndex < offsetSet.total.intSize; ++variableIndex) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(offsetSet.intOffset) + variableIndex] =
                getName() + ":continuous_" + std::to_string(variableIndex);
        } else {
            objNames[static_cast<size_t>(offsetSet.intOffset) + variableIndex] =
                prefix + "::" + getName() + ":continuous_" + std::to_string(variableIndex);
        }
    }
}

void gridOptObject::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void gridOptObject::dynObjectInitializeB(std::uint32_t /*flags*/) {}

void gridOptObject::loadSizes(const OptimizationMode& /*unused*/) {}

void gridOptObject::setValues(const OptimizationData& /*unused*/,
                              const OptimizationMode& /*unused*/)
{
}

void gridOptObject::guessState(double /*time*/,
                               double /*val*/[],
                               const OptimizationMode& /*unused*/)
{
}

void gridOptObject::getTols(double /*tols*/[], const OptimizationMode& /*unused*/) {}

void gridOptObject::valueBounds(double /*time*/,
                                double /*upLimit*/[],
                                double /*lowerLimit*/[],
                                const OptimizationMode& /*unused*/)
{
}

void gridOptObject::linearObj(const OptimizationData& /*unused*/,
                              vectData<double>& /*linObj*/,
                              const OptimizationMode& /*unused*/)
{
}

void gridOptObject::quadraticObj(const OptimizationData& /*unused*/,
                                 vectData<double>& /*linObj*/,
                                 vectData<double>& /*quadObj*/,
                                 const OptimizationMode& /*unused*/)
{
}

double gridOptObject::objValue(const OptimizationData& /*unused*/,
                               const OptimizationMode& /*unused*/)
{
    return 0;
}

void gridOptObject::gradient(const OptimizationData& /*unused*/,
                             double /*grad*/[],
                             const OptimizationMode& /*unused*/)
{
}

void gridOptObject::jacobianElements(const OptimizationData& /*unused*/,
                                     matrixData<double>& /*md*/,
                                     const OptimizationMode& /*unused*/)
{
}

void gridOptObject::getConstraints(const OptimizationData& /*unused*/,
                                   matrixData<double>& /*cons*/,
                                   double /*upperLimit*/[],
                                   double /*lowerLimit*/[],
                                   const OptimizationMode& /*unused*/)
{
}

void gridOptObject::constraintValue(const OptimizationData& /*unused*/,
                                    double /*cVals*/[],
                                    const OptimizationMode& /*unused*/)
{
}

void gridOptObject::constraintJacobianElements(const OptimizationData& /*unused*/,
                                               matrixData<double>& /*md*/,
                                               const OptimizationMode& /*unused*/)
{
}

void gridOptObject::hessianElements(const OptimizationData& /*unused*/,
                                    matrixData<double>& /*md*/,
                                    const OptimizationMode& /*unused*/)
{
}

gridOptObject* gridOptObject::getBus(index_t /*index*/) const
{
    return nullptr;
}

gridOptObject* gridOptObject::getArea(index_t /*index*/) const
{
    return nullptr;
}

gridOptObject* gridOptObject::getLink(index_t /*index*/) const
{
    return nullptr;
}

gridOptObject* gridOptObject::getRelay(index_t /*index*/) const
{
    return nullptr;
}

void printObjStateNames(gridOptObject* obj, const OptimizationMode& oMode)
{
    std::vector<std::string> sNames;
    obj->getObjName(sNames, oMode);
    int nameIndex = 0;
    for (auto& stateName : sNames) {
        std::cout << nameIndex++ << " " << stateName << '\n';
    }
}

}  // namespace griddyn
