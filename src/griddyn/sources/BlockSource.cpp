/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "BlockSource.h"

#include "../Block.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <algorithm>
#include <string>
#include <vector>
namespace griddyn::sources {
BlockSource::BlockSource(const std::string& objName): Source(objName) {}
CoreObject* BlockSource::clone(CoreObject* obj) const
{
    auto blkSrc = cloneBase<BlockSource, Source>(this, obj);
    if (blkSrc == nullptr) {
        return obj;
    }
    blkSrc->maxStepSize = maxStepSize;
    return blkSrc;
}

void BlockSource::add(CoreObject* obj)
{
    if (dynamic_cast<GridBlock*>(obj) != nullptr) {
        if (blk != nullptr) {
            GridComponent::remove(blk);
        }
        blk = static_cast<GridBlock*>(obj);
        addSubObject(blk);
    }
    if (dynamic_cast<Source*>(obj) != nullptr) {
        if (src != nullptr) {
            GridComponent::remove(src);
        }
        src = static_cast<Source*>(obj);
        addSubObject(src);
    } else {
        CoreObject::add(
            obj);  // just pass it to the core to do the appropriate thing(probably throw an
        // exception)
    }
}

void BlockSource::remove(CoreObject* obj)
{
    if (isSameObject(src, obj)) {
        GridComponent::remove(obj);
        src = nullptr;
        return;
    }

    if (isSameObject(blk, obj)) {
        GridComponent::remove(obj);
        blk = nullptr;
        return;
    }
}

void BlockSource::dynObjectInitializeB(const IOdata& /*inputs*/,
                                       const IOdata& desiredOutput,
                                       IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        if (src != nullptr) {
            src->dynInitializeB(noInputs, noInputs, fieldSet);
        }
        if (blk != nullptr) {
            blk->dynInitializeB(fieldSet, noInputs, fieldSet);
        }
        m_output = fieldSet[0];
    } else {
        m_output = desiredOutput[0];
        if (blk != nullptr) {
            blk->dynInitializeB(noInputs, desiredOutput, fieldSet);
        }
        if (src != nullptr) {
            src->dynInitializeB(noInputs, fieldSet, fieldSet);
        }
    }
    if (maxStepSize > kBigNum / 2.0) {
        if (blk != nullptr) {
            maxStepSize = blk->get("maxstepsize");
        }
    }
}

void BlockSource::setFlag(std::string_view flag, bool val)
{
    if (subObjectSet(flag, val)) {
        return;
    }

    try {
        Source::setFlag(flag, val);
    }
    catch (const UnrecognizedParameter&) {
        if (src != nullptr) {
            src->setFlag(flag, val);
        }
    }
}

void BlockSource::set(std::string_view param, std::string_view val)
{
    if (subObjectSet(param, val)) {
        return;
    }

    try {
        Source::set(param, val);
    }
    catch (const UnrecognizedParameter&) {
        if (src != nullptr) {
            src->set(param, val);
        }
    }
}
void BlockSource::set(std::string_view param, double val, units::unit unitType)
{
    if (subObjectSet(param, val, unitType)) {
        return;
    }
    if (param == "maxstepsize") {
        maxStepSize = val;
    } else {
        try {
            Source::set(param, val, unitType);
        }
        catch (const UnrecognizedParameter&) {
            if (src != nullptr) {
                src->set(param, val, unitType);
            }
        }
    }
}
double BlockSource::get(std::string_view param, units::unit unitType) const
{
    double rval = Source::get(param, unitType);
    if (rval == kNullVal) {
        if (src != nullptr) {
            return src->get(param, unitType);
        }
    }
    return rval;
}

// void derivative(const IOdata &inputs, const StateData&stateDataValue, double deriv[], const
// SolverMode &sMode);

void BlockSource::residual(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double resid[],
                           const SolverMode& sMode)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    if (src != nullptr) {
        src->residual(inputs, stateDataValue, resid, sMode);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
        srcDout = src->getDoutdt(inputs, stateDataValue, sMode, 0);
    }
    if (blk != nullptr) {
        blk->blockResidual(srcOut, srcDout, stateDataValue, resid, sMode);
    }
}

void BlockSource::derivative(const IOdata& inputs,
                             const StateData& stateDataValue,
                             double deriv[],
                             const SolverMode& sMode)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    if (src != nullptr) {
        src->derivative(inputs, stateDataValue, deriv, sMode);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
        srcDout = src->getDoutdt(inputs, stateDataValue, sMode, 0);
    }
    if (blk != nullptr) {
        blk->blockDerivative(srcOut, srcDout, stateDataValue, deriv, sMode);
    }
}

void BlockSource::algebraicUpdate(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  double update[],
                                  const SolverMode& sMode,
                                  double alpha)
{
    double srcOut = m_output;
    if (src != nullptr) {
        src->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
    }
    if (blk != nullptr) {
        blk->blockAlgebraicUpdate(srcOut, stateDataValue, update, sMode);
    }
}

void BlockSource::jacobianElements(const IOdata& inputs,
                                   const StateData& stateDataValue,
                                   MatrixData<double>& matrixDataValue,
                                   const IOlocs& inputLocs,
                                   const SolverMode& sMode)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    index_t srcLoc = kNullLocation;
    if (src != nullptr) {
        src->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
        srcDout = src->getDoutdt(inputs, stateDataValue, sMode, 0);
        srcLoc = src->getOutputLoc(sMode, 0);
    }
    if (blk != nullptr) {
        blk->blockJacobianElements(srcOut, srcDout, stateDataValue, matrixDataValue, srcLoc, sMode);
    }
}

void BlockSource::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    while (prevTime < time) {
        auto ntime = std::min(prevTime + maxStepSize, time);
        double srcOut = m_output;
        if (src != nullptr) {
            src->timestep(ntime, inputs, sMode);
            srcOut = getOutput(0);
        }
        if (blk != nullptr) {
            blk->step(ntime, srcOut);
        }
    }
}

void BlockSource::rootTest(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double roots[],
                           const SolverMode& sMode)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    if (src != nullptr) {
        src->rootTest(inputs, stateDataValue, roots, sMode);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
        srcDout = src->getDoutdt(inputs, stateDataValue, sMode, 0);
    }
    if (blk != nullptr) {
        blk->rootTest({srcOut, srcDout}, stateDataValue, roots, sMode);
    }
}
void BlockSource::rootTrigger(CoreTime time,
                              const IOdata& inputs,
                              const std::vector<int>& rootMask,
                              const SolverMode& sMode)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    if (src != nullptr) {
        src->rootTrigger(time, inputs, rootMask, sMode);
        srcOut = src->getOutput(0);
        srcDout = src->getDoutdt(noInputs, emptyStateData, sMode, 0);
    }
    if (blk != nullptr) {
        blk->rootTrigger(time, {srcOut, srcDout}, rootMask, sMode);
    }
}

ChangeCode BlockSource::rootCheck(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode,
                                  CheckLevel level)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (src != nullptr) {
        auto iret = src->rootCheck(inputs, stateDataValue, sMode, level);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
        srcDout = src->getDoutdt(inputs, stateDataValue, sMode, 0);
        ret = (std::max)(iret, ret);
    }
    if (blk != nullptr) {
        auto iret = blk->rootCheck({srcOut, srcDout}, stateDataValue, sMode, level);
        ret = (std::max)(iret, ret);
    }
    return ret;
}

void BlockSource::updateLocalCache(const IOdata& inputs,
                                   const StateData& stateDataValue,
                                   const SolverMode& sMode)
{
    double srcOut = m_output;
    double srcDout = 0.0;
    if (src != nullptr) {
        src->updateLocalCache(inputs, stateDataValue, sMode);
        srcOut = src->getOutput(inputs, stateDataValue, sMode, 0);
        srcDout = src->getDoutdt(inputs, stateDataValue, sMode, 0);
    }
    if (blk != nullptr) {
        blk->updateLocalCache({srcOut, srcDout}, stateDataValue, sMode);
    }
}

/** set the output level
@param[in] newLevel the level to set the output at
*/
void BlockSource::setLevel(double newLevel)
{
    if (src != nullptr) {
        src->setLevel(newLevel);
    }
}

IOdata BlockSource::getOutputs(const IOdata& /*inputs*/,
                               const StateData& stateDataValue,
                               const SolverMode& sMode) const
{
    if (blk != nullptr) {
        return blk->getOutputs(noInputs, stateDataValue, sMode);
    }
    if (src != nullptr) {
        return src->getOutputs(noInputs, stateDataValue, sMode);
    }
    return Source::getOutputs(noInputs, stateDataValue, sMode);
}

double BlockSource::getOutput(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode,
                              index_t outputNum) const
{
    if (blk != nullptr) {
        return blk->getOutput(noInputs, stateDataValue, sMode, outputNum);
    }
    if (src != nullptr) {
        return src->getOutput(inputs, stateDataValue, sMode, outputNum);
    }
    return Source::getOutput(inputs, stateDataValue, sMode, outputNum);
}

double BlockSource::getOutput(index_t outputNum) const
{
    if (blk != nullptr) {
        return blk->getOutput(outputNum);
    }
    if (src != nullptr) {
        return src->getOutput(outputNum);
    }

    return Source::getOutput(outputNum);
}

double BlockSource::getDoutdt(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode,
                              index_t outputNum) const
{
    if (blk != nullptr) {
        return blk->getDoutdt(noInputs, stateDataValue, sMode, outputNum);
    }
    if (src != nullptr) {
        return src->getDoutdt(inputs, stateDataValue, sMode, outputNum);
    }

    return Source::getDoutdt(inputs, stateDataValue, sMode, outputNum);
}

CoreObject* BlockSource::find(std::string_view object) const
{
    if (object == "source") {
        return src;
    }
    if (object == "block") {
        return blk;
    }
    return GridComponent::find(object);
}

CoreObject* BlockSource::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "source") {
        return (num == 0) ? src : nullptr;
    }
    if (typeName == "block") {
        return (num == 0) ? blk : nullptr;
    }

    return GridComponent::getSubObject(typeName, num);
}
}  // namespace griddyn::sources
