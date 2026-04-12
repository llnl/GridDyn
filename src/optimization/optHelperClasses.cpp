/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "optHelperClasses.h"

#include "gmlc/utilities/vectorOps.hpp"
#include <cstring>

namespace griddyn {
bool isAC(const optimMode& oMode)
{
    return (oMode.flowMode == flowModel_t::ac);
}

void optimSizes::reset()
{
    std::memset(this, 0, sizeof(optimSizes));
}

void optimSizes::add(const optimSizes& arg)
{
    vSize += arg.vSize;
    aSize += arg.aSize;
    genSize += arg.genSize;
    qSize += arg.qSize;
    intSize += arg.intSize;
    contSize += arg.contSize;
    constraintsSize += arg.constraintsSize;
}

void optimOffsets::reset()
{
    gOffset = qOffset = vOffset = aOffset = contOffset = intOffset = kNullLocation;
    constraintOffset = 0;
    total.reset();
    local.reset();

    loaded = false;
}

void optimOffsets::increment()
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

void optimOffsets::increment(const optimOffsets& offsets)
{
    count_t contExtra = 0;
    if (aOffset != kNullLocation) {
        aOffset += offsets.total.aSize;
    } else {
        contExtra = offsets.total.aSize;
    }
    if (vOffset != kNullLocation) {
        vOffset += offsets.total.vSize;
    } else {
        contExtra = offsets.total.vSize;
    }

    if (gOffset != kNullLocation) {
        gOffset += offsets.total.genSize;
    } else {
        contExtra = offsets.total.genSize;
    }

    if (qOffset != kNullLocation) {
        qOffset += offsets.total.qSize;
    } else {
        contExtra = offsets.total.qSize;
    }
    contOffset += offsets.total.contSize + contExtra;

    if (intOffset != kNullLocation) {
        intOffset += offsets.total.intSize;
    } else {
        contOffset += offsets.total.intSize;
    }

    constraintOffset += offsets.total.constraintsSize;
}

void optimOffsets::addSizes(const optimOffsets& offsets)
{
    total.add(offsets.total);
}

void optimOffsets::localLoad(bool finishedLoading)
{
    total = local;
    loaded = finishedLoading;
}

void optimOffsets::setOffsets(const optimOffsets& newOffsets)
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

void optimOffsets::setOffset(index_t newOffset)
{
    aOffset = newOffset;
    vOffset = aOffset + total.aSize;
    gOffset = vOffset + total.vSize;
    qOffset = gOffset + total.genSize;
    contOffset = qOffset + total.qSize;
    intOffset = contOffset + total.contSize;
}
using gmlc::utilities::ensureSizeAtLeast;

optimOffsets& optOffsetTable::getOffsets(const optimMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    return offsetContainer[oMode.offsetIndex];
}

static const optimOffsets nullOffsets;

const optimOffsets& optOffsetTable::getOffsets(const optimMode& oMode) const
{
    return (oMode.offsetIndex < static_cast<count_t>(offsetContainer.size())) ?
        offsetContainer[oMode.offsetIndex] :
        nullOffsets;
}

void optOffsetTable::setOffsets(const optimOffsets& newOffsets, const optimMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].setOffsets(newOffsets);
}

void optOffsetTable::setOffset(index_t newOffset, const optimMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].setOffset(newOffset);
}

void optOffsetTable::setContOffset(index_t newOffset, const optimMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].contOffset = newOffset;
}

void optOffsetTable::setIntOffset(index_t newOffset, const optimMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].intOffset = newOffset;
}

void optOffsetTable::setConstraintOffset(index_t newOffset, const optimMode& oMode)
{
    ensureSizeAtLeast(offsetContainer, oMode.offsetIndex + 1);
    offsetContainer[oMode.offsetIndex].constraintOffset = newOffset;
}

index_t optOffsetTable::getaOffset(const optimMode& oMode) const
{
    return getOffsets(oMode).aOffset;
}

index_t optOffsetTable::getvOffset(const optimMode& oMode) const
{
    return getOffsets(oMode).vOffset;
}

index_t optOffsetTable::getContOffset(const optimMode& oMode) const
{
    return getOffsets(oMode).contOffset;
}

index_t optOffsetTable::getIntOffset(const optimMode& oMode) const
{
    return getOffsets(oMode).intOffset;
}

index_t optOffsetTable::getgOffset(const optimMode& oMode) const
{
    return getOffsets(oMode).gOffset;
}

index_t optOffsetTable::getqOffset(const optimMode& oMode) const
{
    return getOffsets(oMode).qOffset;
}

/** get the locations for the data
 *@param[in] oMode the optimMode we are interested in
 */
// void getLocations (const stateData &sD, double d[], const optimMode &oMode, Lp *Loc,
// gridComponent *comp);
/** get the locations for the data from a stateData pointer
 *@param[in] oMode the optimMode we are interested in
 *@return the angle offset
 */
// void getLocations (stateData *sD, double d[], const optimMode &oMode, Lp *Loc, gridComponent
// *comp);
/** get the locations offsets for the data
 *@param[in] oMode the optimMode we are interested in
 *@return the angle offset
 */
// void getLocations (const optimMode &oMode, Lp *Loc);

}  // namespace griddyn
