/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size = oo.total.genSize + oo.total.vSize + oo.total.aSize + oo.total.qSize +
            oo.total.contSize + oo.total.intSize;
    } else {
        loadSizes(oMode);
        size = oo.total.genSize + oo.total.qSize + oo.total.vSize + oo.total.aSize +
            oo.total.contSize + oo.total.intSize;
    }
    return size;
}

count_t gridOptObject::contObjSize(const optimMode& oMode)
{
    count_t size = 0;
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size =
            oo.total.genSize + oo.total.qSize + oo.total.vSize + oo.total.aSize + oo.total.contSize;
    } else {
        loadSizes(oMode);
        size =
            oo.total.genSize + oo.total.qSize + oo.total.vSize + oo.total.aSize + oo.total.contSize;
    }
    return size;
}

count_t gridOptObject::intObjSize(const optimMode& oMode)
{
    count_t size = 0;
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size = oo.total.intSize;
    } else {
        loadSizes(oMode);
        size = oo.total.intSize;
    }
    return size;
}

count_t gridOptObject::genSize(const optimMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    if (!oo.loaded) {
        loadSizes(oMode);
    }
    return oo.total.genSize;
}

count_t gridOptObject::qSize(const optimMode& oMode)
{
    count_t size = 0;
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size = oo.total.qSize;
    } else {
        loadSizes(oMode);
        size = oo.total.qSize;
    }
    return size;
}

count_t gridOptObject::vSize(const optimMode& oMode)
{
    count_t size = 0;
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size = oo.total.vSize;
    } else {
        loadSizes(oMode);
        size = oo.total.vSize;
    }
    return size;
}

count_t gridOptObject::aSize(const optimMode& oMode)
{
    count_t size = 0;
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size = oo.total.aSize;
    } else {
        loadSizes(oMode);
        size = oo.total.aSize;
    }
    return size;
}

count_t gridOptObject::constraintSize(const optimMode& oMode)
{
    count_t size = 0;
    auto& oo = offsets.getOffsets(oMode);
    if (oo.loaded) {
        size = oo.total.constraintsSize;
    } else {
        loadSizes(oMode);
        size = oo.total.constraintsSize;
    }
    return size;
}

// size getter functions
count_t gridOptObject::objSize(const optimMode& oMode) const
{
    auto& oo = offsets.getOffsets(oMode);
    return (oo.total.genSize + oo.total.vSize + oo.total.aSize + oo.total.qSize +
            oo.total.contSize + oo.total.intSize);
}

count_t gridOptObject::contObjSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);
    return (oo.total.genSize + oo.total.qSize + oo.total.vSize + oo.total.aSize +
            oo.total.contSize);
}

count_t gridOptObject::intObjSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);
    return oo.total.intSize;
}

count_t gridOptObject::genSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);
    return oo.total.genSize;
}

count_t gridOptObject::qSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);
    return oo.total.qSize;
}

count_t gridOptObject::vSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);
    return oo.total.vSize;
}

count_t gridOptObject::aSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);
    return oo.total.aSize;
}

count_t gridOptObject::constraintSize(const optimMode& oMode) const
{
    const auto& oo = offsets.getOffsets(oMode);

    return oo.total.constraintsSize;
}

void gridOptObject::dynInitializeA(std::uint32_t flags)
{
    dynObjectInitializeA(flags);
}

void gridOptObject::dynInitializeB(std::uint32_t flags)
{
    dynObjectInitializeB(flags);
    optFlags.set(opt_initialized);
}

void gridOptObject::setOffsets(const optimOffsets& newOffsets, const optimMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    if (!oo.loaded) {
        loadSizes(oMode);
    }
    oo.setOffsets(newOffsets);
}

void gridOptObject::setOffset(index_t offset, index_t constraintOffset, const optimMode& oMode)
{
    offsets.setOffset(offset, oMode);
    offsets.setConstraintOffset(constraintOffset, oMode);
}

void gridOptObject::set(const std::string& param, const std::string& val)
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

void gridOptObject::set(const std::string& param, double val, units::unit unitType)
{
    if (param == "#") {
    } else {
        coreObject::set(param, val, unitType);
    }
}

void gridOptObject::getVariableType(double sdata[], const optimMode& oMode)
{
    if (offsets.isLoaded(oMode)) {
        auto intIndex = offsets.getIntOffset(oMode);
        auto iSize = intObjSize(oMode);
        for (index_t kk = 0; kk < iSize; ++kk) {
            sdata[intIndex + kk] = INTEGER_OBJECTIVE_VARIABLE;
        }
    }
}

void gridOptObject::getObjName(stringVec& objNames,
                               const optimMode& oMode,
                               const std::string& prefix)
{
    auto& os = offsets.getOffsets(oMode);
    // angle variables
    if (static_cast<count_t>(objNames.size()) < os.total.aSize + os.aOffset + 1) {
        objNames.resize(static_cast<size_t>(os.total.aSize) + os.aOffset + 1);
    }
    for (index_t bb = 0; bb < os.total.aSize; ++bb) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(os.aOffset) + bb] =
                getName() + ":angle_" + std::to_string(bb);
        } else {
            objNames[static_cast<size_t>(os.aOffset) + bb] =
                prefix + "::" + getName() + ":angle_" + std::to_string(bb);
        }
    }
    // voltage variables
    if (static_cast<count_t>(objNames.size()) < os.total.vSize + os.vOffset + 1) {
        objNames.resize(static_cast<size_t>(os.total.vSize) + os.vOffset + 1);
    }
    for (index_t bb = 0; bb < os.total.vSize; ++bb) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(os.vOffset) + bb] =
                getName() + ":voltage_" + std::to_string(bb);
        } else {
            objNames[static_cast<size_t>(os.vOffset) + bb] =
                prefix + "::" + getName() + ":voltage_" + std::to_string(bb);
        }
    }
    // real power variables
    if (static_cast<count_t>(objNames.size()) < os.total.genSize + os.gOffset + 1) {
        objNames.resize(static_cast<size_t>(os.total.genSize) + os.gOffset + 1);
    }
    for (index_t bb = 0; bb < os.total.genSize; ++bb) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(os.gOffset) + bb] =
                getName() + ":power_" + std::to_string(bb);
        } else {
            objNames[static_cast<size_t>(os.gOffset) + bb] =
                prefix + "::" + getName() + ":power_" + std::to_string(bb);
        }
    }
    // angle variables
    if (static_cast<count_t>(objNames.size()) < os.total.qSize + os.qOffset + 1) {
        objNames.resize(static_cast<size_t>(os.total.qSize) + os.qOffset + 1);
    }
    for (index_t bb = 0; bb < os.total.qSize; ++bb) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(os.qOffset) + bb] =
                getName() + ":reactive_power_" + std::to_string(bb);
        } else {
            objNames[static_cast<size_t>(os.qOffset) + bb] =
                prefix + "::" + getName() + ":reactive_power_" + std::to_string(bb);
        }
    }
    // other continuous variables
    if (static_cast<count_t>(objNames.size()) < os.total.contSize + os.contOffset + 1) {
        objNames.resize(static_cast<size_t>(os.total.contSize) + os.contOffset + 1);
    }
    for (index_t bb = 0; bb < os.total.contSize; ++bb) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(os.contOffset) + bb] =
                getName() + ":continuous_" + std::to_string(bb);
        } else {
            objNames[static_cast<size_t>(os.contOffset) + bb] =
                prefix + "::" + getName() + ":continuous_" + std::to_string(bb);
        }
    }
    // integer variables
    if (static_cast<count_t>(objNames.size()) < os.total.intSize + os.intOffset + 1) {
        objNames.resize(static_cast<size_t>(os.total.intSize) + os.intOffset + 1);
    }
    for (index_t bb = 0; bb < os.total.intSize; ++bb) {
        if (prefix.empty()) {
            objNames[static_cast<size_t>(os.intOffset) + bb] =
                getName() + ":continuous_" + std::to_string(bb);
        } else {
            objNames[static_cast<size_t>(os.intOffset) + bb] =
                prefix + "::" + getName() + ":continuous_" + std::to_string(bb);
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
    int kk = 0;
    for (auto& sn : sNames) {
        std::cout << kk++ << " " << sn << '\n';
    }
}

}  // namespace griddyn
