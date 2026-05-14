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
count_t gridOptObject::objSize(const optimMode& oMode)
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

count_t gridOptObject::contObjSize(const optimMode& oMode)
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

count_t gridOptObject::intObjSize(const optimMode& oMode)
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

count_t gridOptObject::genSize(const optimMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    if (!offsetData.loaded) {
        loadSizes(oMode);
    }
    return offsetData.total.genSize;
}

count_t gridOptObject::qSize(const optimMode& oMode)
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

count_t gridOptObject::vSize(const optimMode& oMode)
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

count_t gridOptObject::aSize(const optimMode& oMode)
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

count_t gridOptObject::constraintSize(const optimMode& oMode)
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
count_t gridOptObject::objSize(const optimMode& oMode) const
{
    auto& offsetData = offsets.getOffsets(oMode);
    return (offsetData.total.genSize + offsetData.total.vSize + offsetData.total.aSize +
            offsetData.total.qSize + offsetData.total.contSize + offsetData.total.intSize);
}

count_t gridOptObject::contObjSize(const optimMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return (offsetData.total.genSize + offsetData.total.qSize + offsetData.total.vSize +
            offsetData.total.aSize + offsetData.total.contSize);
}

count_t gridOptObject::intObjSize(const optimMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.intSize;
}

count_t gridOptObject::genSize(const optimMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.genSize;
}

count_t gridOptObject::qSize(const optimMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.qSize;
}

count_t gridOptObject::vSize(const optimMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.vSize;
}

count_t gridOptObject::aSize(const optimMode& oMode) const
{
    const auto& offsetData = offsets.getOffsets(oMode);
    return offsetData.total.aSize;
}

count_t gridOptObject::constraintSize(const optimMode& oMode) const
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

void gridOptObject::setOffsets(const optimOffsets& newOffsets, const optimMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    if (!offsetData.loaded) {
        loadSizes(oMode);
    }
    offsetData.setOffsets(newOffsets);
}

void gridOptObject::setOffset(index_t offset, index_t constraintOffset, const optimMode& oMode)
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

void gridOptObject::getVariableType(double sdata[], const optimMode& oMode)
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
                               const optimMode& oMode,
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

void gridOptObject::loadSizes(const optimMode& /*unused*/) {}

void gridOptObject::setValues(const optimData& /*unused*/, const optimMode& /*unused*/) {}

void gridOptObject::guessState(double /*time*/, double /*val*/[], const optimMode& /*unused*/) {}

void gridOptObject::getTols(double /*tols*/[], const optimMode& /*unused*/) {}

void gridOptObject::valueBounds(double /*time*/,
                                double /*upLimit*/[],
                                double /*lowerLimit*/[],
                                const optimMode& /*unused*/)
{
}

void gridOptObject::linearObj(const optimData& /*unused*/,
                              vectData<double>& /*linObj*/,
                              const optimMode& /*unused*/)
{
}

void gridOptObject::quadraticObj(const optimData& /*unused*/,
                                 vectData<double>& /*linObj*/,
                                 vectData<double>& /*quadObj*/,
                                 const optimMode& /*unused*/)
{
}

double gridOptObject::objValue(const optimData& /*unused*/, const optimMode& /*unused*/)
{
    return 0;
}

void gridOptObject::gradient(const optimData& /*unused*/,
                             double /*grad*/[],
                             const optimMode& /*unused*/)
{
}

void gridOptObject::jacobianElements(const optimData& /*unused*/,
                                     matrixData<double>& /*md*/,
                                     const optimMode& /*unused*/)
{
}

void gridOptObject::getConstraints(const optimData& /*unused*/,
                                   matrixData<double>& /*cons*/,
                                   double /*upperLimit*/[],
                                   double /*lowerLimit*/[],
                                   const optimMode& /*unused*/)
{
}

void gridOptObject::constraintValue(const optimData& /*unused*/,
                                    double /*cVals*/[],
                                    const optimMode& /*unused*/)
{
}

void gridOptObject::constraintJacobianElements(const optimData& /*unused*/,
                                               matrixData<double>& /*md*/,
                                               const optimMode& /*unused*/)
{
}

void gridOptObject::hessianElements(const optimData& /*unused*/,
                                    matrixData<double>& /*md*/,
                                    const optimMode& /*unused*/)
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

void printObjStateNames(gridOptObject* obj, const optimMode& oMode)
{
    std::vector<std::string> sNames;
    obj->getObjName(sNames, oMode);
    int nameIndex = 0;
    for (auto& stateName : sNames) {
        std::cout << nameIndex++ << " " << stateName << '\n';
    }
}

}  // namespace griddyn
