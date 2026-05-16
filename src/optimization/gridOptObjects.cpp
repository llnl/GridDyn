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
GridOptObject::GridOptObject(const std::string& objName): coreObject(objName) {}

coreObject* GridOptObject::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<GridOptObject, coreObject>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->numParams = numParams;
    nobj->optFlags = optFlags;
    return nobj;
}

// size getter functions
count_t GridOptObject::objSize(const OptimizationMode& oMode)
{
    count_t size = 0;
    const auto& offsetData = offsets.getOffsets(oMode);
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

count_t GridOptObject::contObjSize(const OptimizationMode& oMode)
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

count_t GridOptObject::intObjSize(const OptimizationMode& oMode)
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

count_t GridOptObject::genSize(const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    if (!offsetData.loaded) {
        loadSizes(oMode);
    }
    return offsetData.total.genSize;
}

count_t GridOptObject::qSize(const OptimizationMode& oMode)
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

count_t GridOptObject::vSize(const OptimizationMode& oMode)
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

count_t GridOptObject::aSize(const OptimizationMode& oMode)
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

count_t GridOptObject::constraintSize(const OptimizationMode& oMode)
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
count_t GridOptObject::objSize(const OptimizationMode& oMode) const
{
    auto& offsetData = offsets.getOffsets(oMode);
    return (offsetData.total.genSize + offsetData.total.vSize + offsetData.total.aSize +
            offsetData.total.qSize + offsetData.total.contSize + offsetData.total.intSize);
}

count_t GridOptObject::contObjSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return (offsetData.total.genSize + offsetData.total.qSize + offsetData.total.vSize +
            offsetData.total.aSize + offsetData.total.contSize);
}

count_t GridOptObject::intObjSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.intSize;
}

count_t GridOptObject::genSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.genSize;
}

count_t GridOptObject::qSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.qSize;
}

count_t GridOptObject::vSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.vSize;
}

count_t GridOptObject::aSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.aSize;
}

count_t GridOptObject::constraintSize(const OptimizationMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);

    return offsetData.total.constraintsSize;
}

void GridOptObject::dynInitializeA(std::uint32_t flags)
{
    dynObjectInitializeA(flags);
}

void GridOptObject::dynInitializeB(std::uint32_t flags)
{
    dynObjectInitializeB(flags);
    optFlags.set(OPT_INITIALIZED);
}

void GridOptObject::setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    if (!offsetData.loaded) {
        loadSizes(oMode);
    }
    offsetData.setOffsets(newOffsets);
}

void GridOptObject::setOffset(index_t offset,
                              index_t constraintOffset,
                              const OptimizationMode& oMode)
{
    offsets.setOffset(offset, oMode);
    offsets.setConstraintOffset(constraintOffset, oMode);
}

void GridOptObject::set(std::string_view param, std::string_view val)
{
    if (param == "status") {
        const auto status = gmlc::utilities::convertToLowerCase(val);
        if (status == "out") {
            if (isEnabled()) {
                disable();
            }
        } else if (status == "in") {
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

void GridOptObject::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "#") {
    } else {
        coreObject::set(param, val, unitType);
    }
}

void GridOptObject::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    if (offsets.isLoaded(oMode)) {
        auto integerIndex = offsets.getIntOffset(oMode);
        auto integerSize = intObjSize(oMode);
        for (index_t variableIndex = 0; variableIndex < integerSize; ++variableIndex) {
            sdata[integerIndex + variableIndex] = INTEGER_OBJECTIVE_VARIABLE;
        }
    }
}

void GridOptObject::getObjName(stringVec& objNames,
                               const OptimizationMode& oMode,
                               const std::string& prefix)
{
    auto& offsetSet = offsets.getOffsets(oMode);
    auto ensureSize = [&objNames](count_t offset, count_t count) {
        const auto requiredSize = static_cast<size_t>(offset) + static_cast<size_t>(count);
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

void GridOptObject::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void GridOptObject::dynObjectInitializeB(std::uint32_t /*flags*/) {}

void GridOptObject::loadSizes(const OptimizationMode& /*unused*/) {}

void GridOptObject::setValues(const OptimizationData& /*unused*/,
                              const OptimizationMode& /*unused*/)
{
}

void GridOptObject::guessState(double /*time*/,
                               double /*val*/[],
                               const OptimizationMode& /*unused*/)
{
}

void GridOptObject::getTols(double /*tols*/[], const OptimizationMode& /*unused*/) {}

void GridOptObject::valueBounds(double /*time*/,
                                double /*upLimit*/[],
                                double /*lowerLimit*/[],
                                const OptimizationMode& /*unused*/)
{
}

void GridOptObject::linearObj(const OptimizationData& /*unused*/,
                              vectData<double>& /*linObj*/,
                              const OptimizationMode& /*unused*/)
{
}

void GridOptObject::quadraticObj(const OptimizationData& /*unused*/,
                                 vectData<double>& /*linObj*/,
                                 vectData<double>& /*quadObj*/,
                                 const OptimizationMode& /*unused*/)
{
}

double GridOptObject::objValue(const OptimizationData& /*unused*/,
                               const OptimizationMode& /*unused*/)
{
    return 0;
}

void GridOptObject::gradient(const OptimizationData& /*unused*/,
                             double /*grad*/[],
                             const OptimizationMode& /*unused*/)
{
}

void GridOptObject::jacobianElements(const OptimizationData& /*unused*/,
                                     matrixData<double>& /*md*/,
                                     const OptimizationMode& /*unused*/)
{
}

void GridOptObject::getConstraints(const OptimizationData& /*unused*/,
                                   matrixData<double>& /*cons*/,
                                   double /*upperLimit*/[],
                                   double /*lowerLimit*/[],
                                   const OptimizationMode& /*unused*/)
{
}

void GridOptObject::constraintValue(const OptimizationData& /*unused*/,
                                    double /*cVals*/[],
                                    const OptimizationMode& /*unused*/)
{
}

void GridOptObject::constraintJacobianElements(const OptimizationData& /*unused*/,
                                               matrixData<double>& /*md*/,
                                               const OptimizationMode& /*unused*/)
{
}

void GridOptObject::hessianElements(const OptimizationData& /*unused*/,
                                    matrixData<double>& /*md*/,
                                    const OptimizationMode& /*unused*/)
{
}

GridOptObject* GridOptObject::getBus(index_t /*index*/) const
{
    return nullptr;
}

GridOptObject* GridOptObject::getArea(index_t /*index*/) const
{
    return nullptr;
}

GridOptObject* GridOptObject::getLink(index_t /*index*/) const
{
    return nullptr;
}

GridOptObject* GridOptObject::getRelay(index_t /*index*/) const
{
    return nullptr;
}

void printObjStateNames(GridOptObject* obj, const OptimizationMode& oMode)
{
    std::vector<std::string> sNames;
    obj->getObjName(sNames, oMode);
    int nameIndex = 0;
    for (auto& stateName : sNames) {
        std::cout << nameIndex++ << " " << stateName << '\n';
    }
}

}  // namespace griddyn
