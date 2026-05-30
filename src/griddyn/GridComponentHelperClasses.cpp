/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridComponentHelperClasses.h"

#include "GridComponent.h"
#include <cstring>

namespace griddyn {

void StateSizes::reset()
{
    std::memset(this, 0, sizeof(StateSizes));
}
void StateSizes::stateReset()
{
    vSize = aSize = algSize = diffSize = 0;
}
void StateSizes::add(const StateSizes& arg)
{
    vSize += arg.vSize;
    aSize += arg.aSize;
    algSize += arg.algSize;
    diffSize += arg.diffSize;
    algRoots += arg.algRoots;
    diffRoots += arg.diffRoots;
    jacSize += arg.jacSize;
}

void StateSizes::addStateSizes(const StateSizes& arg)
{
    vSize += arg.vSize;
    aSize += arg.aSize;
    algSize += arg.algSize;
    diffSize += arg.diffSize;
}

void StateSizes::addRootSizes(const StateSizes& arg)
{
    algRoots += arg.algRoots;
    diffRoots += arg.diffRoots;
}

void StateSizes::addJacobianSizes(const StateSizes& arg)
{
    jacSize += arg.jacSize;
}

count_t StateSizes::totalSize() const
{
    return vSize + aSize + algSize + diffSize;
}

void SolverOffsets::reset()
{
    diffOffset = aOffset = vOffset = algOffset = rootOffset = kNullLocation;
    local.reset();
    total.reset();

    rootsLoaded = jacobianLoaded = stateLoaded = offetLoaded = false;
}

void SolverOffsets::stateReset()
{
    local.stateReset();
    total.stateReset();
    diffOffset = aOffset = vOffset = algOffset = kNullLocation;
    stateLoaded = false;
}

void SolverOffsets::rootCountReset()
{
    rootOffset = kNullLocation;
    local.rootReset();
    total.rootReset();

    rootsLoaded = false;
}

void SolverOffsets::jacobianCountReset()
{
    local.jacobianReset();
    total.jacobianReset();

    jacobianLoaded = false;
}

void SolverOffsets::increment()
{
    count_t algExtra = 0;
    if (aOffset != kNullLocation) {
        aOffset += total.aSize;
    } else {
        algExtra = total.aSize;
    }
    if (vOffset != kNullLocation) {
        vOffset += total.vSize;
    } else {
        algExtra += total.vSize;
    }

    algOffset += total.algSize + algExtra;

    if (diffOffset != kNullLocation) {
        diffOffset += total.diffSize;
    } else {
        algOffset += total.diffSize;
    }
    if (rootOffset != kNullLocation) {
        rootOffset += total.algRoots + total.diffRoots;
    }
}

void SolverOffsets::increment(const SolverOffsets& offsets)
{
    count_t algExtra = 0;
    if (aOffset != kNullLocation) {
        aOffset += offsets.total.aSize;
    } else {
        algExtra = offsets.total.aSize;
    }
    if (vOffset != kNullLocation) {
        vOffset += offsets.total.vSize;
    } else {
        algExtra += offsets.total.vSize;
    }

    algOffset += offsets.total.algSize + algExtra;

    if (diffOffset != kNullLocation) {
        diffOffset += offsets.total.diffSize;
    } else {
        algOffset += offsets.total.diffSize;
    }
    if (rootOffset != kNullLocation) {
        rootOffset += offsets.total.algRoots + offsets.total.diffRoots;
    }
}

void SolverOffsets::localIncrement(const SolverOffsets& offsets)
{
    count_t algExtra = 0;
    if (aOffset != kNullLocation) {
        aOffset += offsets.local.aSize;
    } else {
        algExtra = offsets.local.aSize;
    }
    if (vOffset != kNullLocation) {
        vOffset += offsets.local.vSize;
    } else {
        algExtra += offsets.local.vSize;
    }

    algOffset += offsets.local.algSize + algExtra;

    if (diffOffset != kNullLocation) {
        diffOffset += offsets.local.diffSize;
    } else {
        algOffset += offsets.local.diffSize;
    }
    if (rootOffset != kNullLocation) {
        rootOffset += offsets.local.algRoots + local.diffRoots;
    }
}

void SolverOffsets::addSizes(const SolverOffsets& offsets)
{
    total.add(offsets.total);
}
void SolverOffsets::addStateSizes(const SolverOffsets& offsets)
{
    total.addStateSizes(offsets.total);
}
void SolverOffsets::addJacobianSizes(const SolverOffsets& offsets)
{
    total.addJacobianSizes(offsets.total);
}

void SolverOffsets::addRootSizes(const SolverOffsets& offsets)
{
    total.addRootSizes(offsets.total);
}

void SolverOffsets::localStateLoad(bool finishedLoading)
{
    total.algSize = local.algSize;
    total.diffSize = local.diffSize;
    total.aSize = local.aSize;
    total.vSize = local.vSize;
    stateLoaded = finishedLoading;
}

void SolverOffsets::localLoadAll(bool finishedLoading)
{
    total = local;
    stateLoaded = finishedLoading;
    jacobianLoaded = finishedLoading;
    rootsLoaded = finishedLoading;
}

void SolverOffsets::setOffsets(const SolverOffsets& newOffsets)
{
    algOffset = newOffsets.algOffset;
    diffOffset = newOffsets.diffOffset;

    if (total.aSize > 0) {
        if (newOffsets.aOffset != kNullLocation) {
            aOffset = newOffsets.aOffset;
        } else {
            aOffset = algOffset;
            algOffset += total.aSize;
        }
    } else {
        aOffset = kNullLocation;
    }

    if (total.vSize > 0) {
        if (newOffsets.vOffset != kNullLocation) {
            vOffset = newOffsets.vOffset;
        } else {
            vOffset = algOffset;
            algOffset += total.vSize;
        }
    } else {
        vOffset = kNullLocation;
    }

    if (diffOffset == kNullLocation) {
        diffOffset = algOffset + total.algSize;
    }
}

void SolverOffsets::setOffset(index_t newOffset)
{
    aOffset = newOffset;
    vOffset = aOffset + total.aSize;
    algOffset = vOffset + total.vSize;
    diffOffset = algOffset + total.algSize;
    if (total.aSize == 0) {
        aOffset = kNullLocation;
    }
    if (total.vSize == 0) {
        vOffset = kNullLocation;
    }
}

}  // namespace griddyn
