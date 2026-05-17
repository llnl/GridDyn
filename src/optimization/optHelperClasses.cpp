/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "optHelperClasses.h"

#include "gmlc/utilities/vectorOps.hpp"
#include <cstring>
#include <utility>

namespace griddyn {
bool isAC(const OptimizationMode& oMode)
{
    return (oMode.flowMode == FlowModel::AC);
}

void OptimizationSizes::reset()
{
    std::memset(this, 0, sizeof(OptimizationSizes));
}

void OptimizationSizes::add(const OptimizationSizes& arg)
{
    vSize += arg.vSize;
    aSize += arg.aSize;
    genSize += arg.genSize;
    qSize += arg.qSize;
    intSize += arg.intSize;
    contSize += arg.contSize;
    constraintsSize += arg.constraintsSize;
}

void OptimizationOffsets::reset()
{
    gOffset = qOffset = vOffset = aOffset = contOffset = intOffset = kNullLocation;
    constraintOffset = 0;
    total.reset();
    local.reset();

    loaded = false;
}

void OptimizationOffsets::increment()
{
    count_t contExtra = 0;
    if (aOffset != kNullLocation) {
        aOffset += total.aSize;
    } else {
        contExtra = total.aSize;
    }
    if (vOffset != kNullLocation) {
        vOffset += total.vSize;
    } else {
        contExtra = total.vSize;
    }

    if (gOffset != kNullLocation) {
        gOffset += total.genSize;
    } else {
        contExtra = total.genSize;
    }

    if (qOffset != kNullLocation) {
        qOffset += total.qSize;
    } else {
        contExtra = total.qSize;
    }
    contOffset += total.contSize + contExtra;

    if (intOffset != kNullLocation) {
        intOffset += total.intSize;
    } else {
        contOffset += total.intSize;
    }

    constraintOffset += total.constraintsSize;
}

void OptimizationOffsets::increment(const OptimizationOffsets& offsets)
{
    count_t continuousExtra = 0;
    if (aOffset != kNullLocation) {
        aOffset += offsets.total.aSize;
    } else {
        continuousExtra = offsets.total.aSize;
    }
    if (vOffset != kNullLocation) {
        vOffset += offsets.total.vSize;
    } else {
        continuousExtra = offsets.total.vSize;
    }

    if (gOffset != kNullLocation) {
        gOffset += offsets.total.genSize;
    } else {
        continuousExtra = offsets.total.genSize;
    }

    if (qOffset != kNullLocation) {
        qOffset += offsets.total.qSize;
    } else {
        continuousExtra = offsets.total.qSize;
    }
    contOffset += offsets.total.contSize + continuousExtra;

    if (intOffset != kNullLocation) {
        intOffset += offsets.total.intSize;
    } else {
        contOffset += offsets.total.intSize;
    }

    constraintOffset += offsets.total.constraintsSize;
}

void OptimizationOffsets::addSizes(const OptimizationOffsets& offsets)
{
    total.add(offsets.total);
}

void OptimizationOffsets::localLoad(bool finishedLoading)
{
    total = local;
    loaded = finishedLoading;
}

void OptimizationOffsets::setOffsets(const OptimizationOffsets& newOffsets)
{
    aOffset = newOffsets.aOffset;
    vOffset = newOffsets.vOffset;
    gOffset = newOffsets.gOffset;
    qOffset = newOffsets.qOffset;
    contOffset = newOffsets.contOffset;
    intOffset = newOffsets.intOffset;

    constraintOffset = newOffsets.constraintOffset;

    if (aOffset == kNullLocation) {
        aOffset = contOffset;
        contOffset += total.aSize;
    }
    if (vOffset == kNullLocation) {
        vOffset = contOffset;
        contOffset += total.vSize;
    }
    if (gOffset == kNullLocation) {
        gOffset = contOffset;
        contOffset += total.genSize;
    }
    if (qOffset == kNullLocation) {
        qOffset = contOffset;
        contOffset += total.qSize;
    }
    if (intOffset == kNullLocation) {
        intOffset = contOffset + total.contSize;
    }
}

void OptimizationOffsets::setOffset(index_t newOffset)
{
    aOffset = newOffset;
    vOffset = aOffset + total.aSize;
    gOffset = vOffset + total.vSize;
    qOffset = gOffset + total.genSize;
    contOffset = qOffset + total.qSize;
    intOffset = contOffset + total.contSize;
}
using gmlc::utilities::ensureSizeAtLeast;

OptimizationOffsets& OptimizationOffsetTable::getOffsets(const OptimizationMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    return offsetContainer[oMode.offsetIndex];
}

static const OptimizationOffsets nullOffsets;

const OptimizationOffsets& OptimizationOffsetTable::getOffsets(const OptimizationMode& oMode) const
{
    return std::cmp_less(oMode.offsetIndex, offsetContainer.size()) ?
        offsetContainer[oMode.offsetIndex] :
        nullOffsets;
}

void OptimizationOffsetTable::setOffsets(const OptimizationOffsets& newOffsets,
                                         const OptimizationMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].setOffsets(newOffsets);
}

void OptimizationOffsetTable::setOffset(index_t newOffset, const OptimizationMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].setOffset(newOffset);
}

void OptimizationOffsetTable::setContOffset(index_t newOffset, const OptimizationMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].contOffset = newOffset;
}

void OptimizationOffsetTable::setIntOffset(index_t newOffset, const OptimizationMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].intOffset = newOffset;
}

void OptimizationOffsetTable::setConstraintOffset(index_t newOffset, const OptimizationMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].constraintOffset = newOffset;
}

index_t OptimizationOffsetTable::getAngleOffset(const OptimizationMode& oMode) const
{
    return getOffsets(oMode).aOffset;
}

index_t OptimizationOffsetTable::getVoltageOffset(const OptimizationMode& oMode) const
{
    return getOffsets(oMode).vOffset;
}

index_t OptimizationOffsetTable::getContOffset(const OptimizationMode& oMode) const
{
    return getOffsets(oMode).contOffset;
}

index_t OptimizationOffsetTable::getIntOffset(const OptimizationMode& oMode) const
{
    return getOffsets(oMode).intOffset;
}

index_t OptimizationOffsetTable::getGenerationOffset(const OptimizationMode& oMode) const
{
    return getOffsets(oMode).gOffset;
}

index_t OptimizationOffsetTable::getReactiveGenerationOffset(const OptimizationMode& oMode) const
{
    return getOffsets(oMode).qOffset;
}

/** get the locations for the data
 *@param[in] oMode the OptimizationMode we are interested in
 */
// void getLocations (const stateData &sD, double d[], const OptimizationMode &oMode, Lp *Loc,
// gridComponent *comp);
/** get the locations for the data from a stateData pointer
 *@param[in] oMode the OptimizationMode we are interested in
 *@return the angle offset
 */
// void getLocations (stateData *sD, double d[], const OptimizationMode &oMode, Lp *Loc,
// gridComponent *comp);
/** get the locations offsets for the data
 *@param[in] oMode the OptimizationMode we are interested in
 *@return the angle offset
 */
// void getLocations (const OptimizationMode &oMode, Lp *Loc);

}  // namespace griddyn
