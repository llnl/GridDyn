/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "OffsetTable.h"

#include "GridComponent.h"
#include <stdexcept>

namespace griddyn {
static const SolverOffsets NULL_OFFSETS{};

OffsetTable::OffsetTable(): offsetContainer(DEFAULT_OFFSET_CONTAINER_SIZE)
{
    // most simulations use the first 1 and powerflow(2) and likely dynamic
    // DAE(3) and often 4 and 5 for dynamic partitioned
#if DEFAULT_OFFSET_CONTAINER_SIZE == 0
    offsetContainer.resize(1);
#endif
    offsetContainer[0].sMode = cLocalSolverMode;
}

bool OffsetTable::isLoaded(const SolverMode& sMode) const
{
    return (isValidIndex(sMode.offsetIndex)) &&
        ((offsetContainer[sMode.offsetIndex].stateLoaded) &&
         (offsetContainer[sMode.offsetIndex].rootsLoaded) &&
         (offsetContainer[sMode.offsetIndex].jacobianLoaded));
}

bool OffsetTable::isStateCountLoaded(const SolverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) && offsetContainer[sMode.offsetIndex].stateLoaded;
}

bool OffsetTable::isRootCountLoaded(const SolverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) && offsetContainer[sMode.offsetIndex].rootsLoaded;
}

bool OffsetTable::isJacobianCountLoaded(const SolverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) && offsetContainer[sMode.offsetIndex].jacobianLoaded;
}

SolverOffsets& OffsetTable::getOffsets(const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
        offsetContainer[sMode.offsetIndex].sMode = sMode;
    }
    return offsetContainer[sMode.offsetIndex];
}

const SolverOffsets& OffsetTable::getOffsets(const SolverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) ? offsetContainer[sMode.offsetIndex] : NULL_OFFSETS;
}

void OffsetTable::setOffsets(const SolverOffsets& newOffsets, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].setOffsets(newOffsets);
}

void OffsetTable::setOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].setOffset(newOffset);
}

void OffsetTable::setAlgOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].algOffset = newOffset;
}

void OffsetTable::setDiffOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].diffOffset = newOffset;
}

void OffsetTable::setVOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].vOffset = newOffset;
}

void OffsetTable::setAOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].aOffset = newOffset;
}

void OffsetTable::setRootOffset(index_t newOffset, const SolverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].rootOffset = newOffset;
}

index_t OffsetTable::maxIndex(const SolverMode& sMode) const
{
    if (!isValidIndex(sMode.offsetIndex)) {
        return 0;
    }
    const auto& offsetInfo = offsetContainer[sMode.offsetIndex];
    index_t maxUsedIndex = 0;
    if (isDynamic(sMode)) {
        if (offsetInfo.total.diffSize > 0) {
            maxUsedIndex = offsetInfo.diffOffset + offsetInfo.total.diffSize;
        }
        if (offsetInfo.total.algSize > 0 &&
            offsetInfo.algOffset + offsetInfo.total.algSize > maxUsedIndex) {
            maxUsedIndex = offsetInfo.algOffset + offsetInfo.total.algSize;
        }
    } else {
        if (offsetInfo.total.algSize > 0) {
            maxUsedIndex = offsetInfo.algOffset + offsetInfo.total.algSize;
        }
    }
    if ((offsetInfo.vOffset != kNullLocation) &&
        (offsetInfo.vOffset + offsetInfo.total.vSize > maxUsedIndex)) {
        maxUsedIndex = offsetInfo.vOffset + offsetInfo.total.vSize;
    }
    if ((offsetInfo.aOffset != kNullLocation) &&
        (offsetInfo.aOffset + offsetInfo.total.aSize > maxUsedIndex)) {
        maxUsedIndex = offsetInfo.aOffset + offsetInfo.total.aSize;
    }
    return maxUsedIndex;
}

void OffsetTable::getLocations(const SolverMode& sMode, Lp* loc) const
{
    loc->algOffset = offsetContainer[sMode.offsetIndex].algOffset;
    loc->diffOffset = offsetContainer[sMode.offsetIndex].diffOffset;
}

void OffsetTable::unload(bool dynamicOnly)
{
    if (dynamicOnly) {
        for (auto& offsetInfo : offsetContainer) {
            if (isDynamic(offsetInfo.sMode)) {
                offsetInfo.stateLoaded = false;
                offsetInfo.rootsLoaded = false;
                offsetInfo.jacobianLoaded = false;
                offsetInfo.diffOffset = kNullLocation;
                offsetInfo.algOffset = kNullLocation;
            }
        }
    } else {
        for (auto& offsetInfo : offsetContainer) {
            offsetInfo.stateLoaded = false;
            offsetInfo.rootsLoaded = false;
            offsetInfo.jacobianLoaded = false;
            offsetInfo.diffOffset = kNullLocation;
            offsetInfo.algOffset = kNullLocation;
        }
    }
}

void OffsetTable::stateUnload(bool dynamicOnly)
{
    if (dynamicOnly) {
        for (auto& offsetInfo : offsetContainer) {
            if (isDynamic(offsetInfo.sMode)) {
                offsetInfo.stateLoaded = false;
                offsetInfo.diffOffset = kNullLocation;
                offsetInfo.algOffset = kNullLocation;
            }
        }
    } else {
        for (auto& offsetInfo : offsetContainer) {
            offsetInfo.stateLoaded = false;
            offsetInfo.diffOffset = kNullLocation;
            offsetInfo.algOffset = kNullLocation;
        }
    }
}

void OffsetTable::rootUnload(bool dynamicOnly)
{
    if (dynamicOnly) {
        for (auto& offsetInfo : offsetContainer) {
            if (isDynamic(offsetInfo.sMode)) {
                offsetInfo.rootsLoaded = false;
            }
        }
    } else {
        for (auto& offsetInfo : offsetContainer) {
            offsetInfo.rootsLoaded = false;
        }
    }
}
void OffsetTable::jacobianUnload(bool dynamicOnly)
{
    if (dynamicOnly) {
        for (auto& offsetInfo : offsetContainer) {
            if (isDynamic(offsetInfo.sMode)) {
                offsetInfo.jacobianLoaded = false;
            }
        }
    } else {
        for (auto& offsetInfo : offsetContainer) {
            offsetInfo.jacobianLoaded = false;
        }
    }
}

void OffsetTable::localUpdateAll(bool dynamicOnly)
{
    if (dynamicOnly) {
        for (auto& offsetInfo : offsetContainer) {
            if (isDynamic(offsetInfo.sMode)) {
                auto& localOffsets = local();
                offsetInfo.total.algRoots = offsetInfo.local.algRoots = localOffsets.local.algRoots;
                offsetInfo.total.diffRoots = offsetInfo.local.diffRoots =
                    localOffsets.local.diffRoots;
                offsetInfo.total.jacSize = offsetInfo.local.jacSize = localOffsets.local.jacSize;
                offsetInfo.rootsLoaded = true;
                offsetInfo.jacobianLoaded = true;
            }
        }
    } else {
        for (auto& offsetInfo : offsetContainer) {
            offsetInfo.local = local().local;
            offsetInfo.localLoadAll(true);
        }
    }
}
const SolverMode& OffsetTable::getSolverMode(index_t index) const
{
    return isValidIndex(index) ? offsetContainer[index].sMode : cEmptySolverMode;
}

const SolverMode& OffsetTable::find(const SolverMode& tMode) const
{
    for (const auto& offsetInfo : offsetContainer) {
        if (offsetInfo.sMode.dynamic != tMode.dynamic) {
            continue;
        }
        if (offsetInfo.sMode.local != tMode.local) {
            continue;
        }
        if (offsetInfo.sMode.algebraic != tMode.algebraic) {
            continue;
        }
        if (offsetInfo.sMode.differential != tMode.differential) {
            continue;
        }

        if (offsetInfo.sMode.extended_state != tMode.extended_state) {
            continue;
        }
        if (offsetInfo.sMode.approx != tMode.approx) {
            continue;
        }
        return offsetInfo.sMode;
    }
    return cEmptySolverMode;
}

Lp OffsetTable::getLocations(const StateData& stateDataValue,
                             double dest[],
                             const SolverMode& sMode,
                             const GridComponent* comp) const
{
    Lp loc = getLocations(stateDataValue, sMode, comp);
    const bool useLocalState = (sMode.local) || (stateDataValue.empty()) ||
        (!isDAE(sMode) && !hasAlgebraic(sMode) && !hasDifferential(sMode));
    if (useLocalState) {
        loc.destLoc = (dest == nullptr) ?
            const_cast<double*>(comp->m_state.data()) + offsetContainer[0].algOffset :
            dest;
        loc.destDiffLoc = loc.destLoc + loc.algSize;
    } else if (isDAE(sMode)) {
        loc.destLoc = dest + loc.algOffset;
        loc.destDiffLoc = dest + loc.diffOffset;
    } else if (hasAlgebraic(sMode)) {
        loc.destLoc = dest + loc.algOffset;
        loc.destDiffLoc = nullptr;
    } else {
        loc.destDiffLoc = dest + loc.diffOffset;
        loc.destLoc = nullptr;
    }
    return loc;
}

Lp OffsetTable::getLocations(const StateData& stateDataValue,
                             const SolverMode& sMode,
                             const GridComponent* comp) const
{
    Lp loc;
    loc.algOffset = offsetContainer[sMode.offsetIndex].algOffset;
    loc.diffOffset = offsetContainer[sMode.offsetIndex].diffOffset;
    loc.diffSize = offsetContainer[sMode.offsetIndex].total.diffSize;
    loc.algSize = offsetContainer[sMode.offsetIndex].total.algSize;
    const bool useLocalState = (sMode.local) || (stateDataValue.empty()) ||
        (!isDAE(sMode) && !hasAlgebraic(sMode) && !hasDifferential(sMode));
    if (useLocalState) {
        loc.time = comp->prevTime;
        loc.algStateLoc = comp->m_state.data();
        loc.diffStateLoc = comp->m_state.data() + loc.algSize;
        loc.dstateLoc = comp->m_dstate_dt.data() + loc.algSize;
        if (loc.algOffset == kNullLocation) {
            loc.algOffset = 0;
        }
        if (loc.diffOffset == kNullLocation) {
            loc.diffOffset = loc.algSize;
        }
    } else if (isDAE(sMode)) {
        loc.time = stateDataValue.time;
        loc.algStateLoc = stateDataValue.state + loc.algOffset;
        loc.diffStateLoc = stateDataValue.state + loc.diffOffset;
        loc.dstateLoc = stateDataValue.dstate_dt + loc.diffOffset;
    } else if (hasAlgebraic(sMode)) {
        loc.time = stateDataValue.time;
        if (stateDataValue.state != nullptr) {
            loc.algStateLoc = stateDataValue.state + loc.algOffset;
        } else {
            loc.algStateLoc = stateDataValue.algState + loc.algOffset;
        }
        if ((isDynamic(sMode)) && (stateDataValue.pairIndex != kNullLocation)) {
            if (stateDataValue.diffState != nullptr) {
                loc.diffStateLoc =
                    stateDataValue.diffState + offsetContainer[stateDataValue.pairIndex].diffOffset;
            } else if (stateDataValue.fullState != nullptr) {
                loc.diffStateLoc =
                    stateDataValue.fullState + offsetContainer[stateDataValue.pairIndex].diffOffset;
            }

            if (stateDataValue.dstate_dt != nullptr) {
                loc.dstateLoc =
                    stateDataValue.dstate_dt + offsetContainer[stateDataValue.pairIndex].diffOffset;
            } else {
                throw std::runtime_error("Missing state required to initialize dstateLoc");
            }
        } else {
            loc.diffStateLoc = comp->m_state.data() + offsetContainer[0].diffOffset;
            loc.dstateLoc = comp->m_dstate_dt.data() + offsetContainer[0].diffOffset;
        }
        loc.destDiffLoc = nullptr;
    } else {
        loc.time = stateDataValue.time;
        if (stateDataValue.state != nullptr) {
            loc.diffStateLoc = stateDataValue.state + loc.diffOffset;
        } else {
            loc.diffStateLoc = stateDataValue.diffState + loc.diffOffset;
        }
        loc.dstateLoc = stateDataValue.dstate_dt + loc.diffOffset;
        if (stateDataValue.pairIndex != kNullLocation) {
            if (stateDataValue.algState != nullptr) {
                loc.algStateLoc =
                    stateDataValue.algState + offsetContainer[stateDataValue.pairIndex].algOffset;
            } else if (stateDataValue.fullState != nullptr) {
                loc.algStateLoc =
                    stateDataValue.fullState + offsetContainer[stateDataValue.pairIndex].algOffset;
            } else {
                throw std::runtime_error("Missing state required to initialize algStateLoc");
            }
        } else {
            loc.algStateLoc = comp->m_state.data() + offsetContainer[0].algOffset;
        }
        loc.destLoc = nullptr;
    }
    return loc;
}

}  // namespace griddyn
