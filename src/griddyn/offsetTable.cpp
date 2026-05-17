/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "offsetTable.h"

#include "gridComponent.h"
#include <stdexcept>

namespace griddyn {
static const solverOffsets nullOffsets{};

offsetTable::offsetTable() noexcept: offsetContainer(DEFAULT_OFFSET_CONTAINER_SIZE)
{
    // most simulations use the first 1 and powerflow(2) and likely dynamic
    // DAE(3) and often 4 and 5 for dynamic partitioned
#if DEFAULT_OFFSET_CONTAINER_SIZE == 0
    offsetContainer.resize(1);
#endif
    offsetContainer[0].sMode = cLocalSolverMode;
}

bool offsetTable::isLoaded(const solverMode& sMode) const
{
    return (isValidIndex(sMode.offsetIndex)) &&
        ((offsetContainer[sMode.offsetIndex].stateLoaded) &&
         (offsetContainer[sMode.offsetIndex].rootsLoaded) &&
         (offsetContainer[sMode.offsetIndex].jacobianLoaded));
}

bool offsetTable::isStateCountLoaded(const solverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) && offsetContainer[sMode.offsetIndex].stateLoaded;
}

bool offsetTable::isRootCountLoaded(const solverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) && offsetContainer[sMode.offsetIndex].rootsLoaded;
}

bool offsetTable::isJacobianCountLoaded(const solverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) && offsetContainer[sMode.offsetIndex].jacobianLoaded;
}

solverOffsets& offsetTable::getOffsets(const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
        offsetContainer[sMode.offsetIndex].sMode = sMode;
    }
    return offsetContainer[sMode.offsetIndex];
}

const solverOffsets& offsetTable::getOffsets(const solverMode& sMode) const
{
    return isValidIndex(sMode.offsetIndex) ? offsetContainer[sMode.offsetIndex] : nullOffsets;
}

void offsetTable::setOffsets(const solverOffsets& newOffsets, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].setOffsets(newOffsets);
}

void offsetTable::setOffset(index_t newOffset, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].setOffset(newOffset);
}

void offsetTable::setAlgOffset(index_t newOffset, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].algOffset = newOffset;
}

void offsetTable::setDiffOffset(index_t newOffset, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].diffOffset = newOffset;
}

void offsetTable::setVOffset(index_t newOffset, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].vOffset = newOffset;
}

void offsetTable::setAOffset(index_t newOffset, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].aOffset = newOffset;
}

void offsetTable::setRootOffset(index_t newOffset, const solverMode& sMode)
{
    if (!isValidIndex(sMode.offsetIndex)) {
        offsetContainer.resize(sMode.offsetIndex + 1);
    }
    offsetContainer[sMode.offsetIndex].sMode = sMode;
    offsetContainer[sMode.offsetIndex].rootOffset = newOffset;
}

index_t offsetTable::maxIndex(const solverMode& sMode) const
{
    if (!isValidIndex(sMode.offsetIndex)) {
        return 0;
    }
    auto so = offsetContainer[sMode.offsetIndex];
    index_t mx = 0;
    if (isDynamic(sMode)) {
        if (so.total.diffSize > 0) {
            mx = so.diffOffset + so.total.diffSize;
        }
        if (so.total.algSize > 0 && so.algOffset + so.total.algSize > mx) {
            mx = so.algOffset + so.total.algSize;
        }
    } else {
        if (so.total.algSize > 0) {
            mx = so.algOffset + so.total.algSize;
        }
    }
    if ((so.vOffset != kNullLocation) && (so.vOffset + so.total.vSize > mx)) {
        mx = so.vOffset + so.total.vSize;
    }
    if ((so.aOffset != kNullLocation) && (so.aOffset + so.total.aSize > mx)) {
        mx = so.aOffset + so.total.aSize;
    }
    return mx;
}

void offsetTable::getLocations(const solverMode& sMode, Lp* Loc) const
{
    Loc->algOffset = offsetContainer[sMode.offsetIndex].algOffset;
    Loc->diffOffset = offsetContainer[sMode.offsetIndex].diffOffset;
}

void offsetTable::unload(bool dynamic_only)
{
    if (dynamic_only) {
        for (auto& so : offsetContainer) {
            if (isDynamic(so.sMode)) {
                so.stateLoaded = false;
                so.rootsLoaded = false;
                so.jacobianLoaded = false;
                so.diffOffset = kNullLocation;
                so.algOffset = kNullLocation;
            }
        }
    } else {
        for (auto& so : offsetContainer) {
            so.stateLoaded = false;
            so.rootsLoaded = false;
            so.jacobianLoaded = false;
            so.diffOffset = kNullLocation;
            so.algOffset = kNullLocation;
        }
    }
}

void offsetTable::stateUnload(bool dynamic_only)
{
    if (dynamic_only) {
        for (auto& so : offsetContainer) {
            if (isDynamic(so.sMode)) {
                so.stateLoaded = false;
                so.diffOffset = kNullLocation;
                so.algOffset = kNullLocation;
            }
        }
    } else {
        for (auto& so : offsetContainer) {
            so.stateLoaded = false;
            so.diffOffset = kNullLocation;
            so.algOffset = kNullLocation;
        }
    }
}

void offsetTable::rootUnload(bool dynamic_only)
{
    if (dynamic_only) {
        for (auto& so : offsetContainer) {
            if (isDynamic(so.sMode)) {
                so.rootsLoaded = false;
            }
        }
    } else {
        for (auto& so : offsetContainer) {
            so.rootsLoaded = false;
        }
    }
}
void offsetTable::JacobianUnload(bool dynamic_only)
{
    if (dynamic_only) {
        for (auto& so : offsetContainer) {
            if (isDynamic(so.sMode)) {
                so.jacobianLoaded = false;
            }
        }
    } else {
        for (auto& so : offsetContainer) {
            so.jacobianLoaded = false;
        }
    }
}

void offsetTable::localUpdateAll(bool dynamic_only)
{
    if (dynamic_only) {
        for (auto& so : offsetContainer) {
            if (isDynamic(so.sMode)) {
                auto& lc = local();
                so.total.algRoots = so.local.algRoots = lc.local.algRoots;
                so.total.diffRoots = so.local.diffRoots = lc.local.diffRoots;
                so.total.jacSize = so.local.jacSize = lc.local.jacSize;
                so.rootsLoaded = true;
                so.jacobianLoaded = true;
            }
        }
    } else {
        for (auto& so : offsetContainer) {
            so.local = local().local;
            so.localLoadAll(true);
        }
    }
}
const solverMode& offsetTable::getSolverMode(index_t index) const
{
    return isValidIndex(index) ? offsetContainer[index].sMode : cEmptySolverMode;
}

const solverMode& offsetTable::find(const solverMode& tMode) const
{
    for (auto& so : offsetContainer) {
        if (so.sMode.dynamic != tMode.dynamic) {
            continue;
        }
        if (so.sMode.local != tMode.local) {
            continue;
        }
        if (so.sMode.algebraic != tMode.algebraic) {
            continue;
        }
        if (so.sMode.differential != tMode.differential) {
            continue;
        }

        if (so.sMode.extended_state != tMode.extended_state) {
            continue;
        }
        if (so.sMode.approx != tMode.approx) {
            continue;
        }
        return so.sMode;
    }
    return cEmptySolverMode;
}

Lp offsetTable::getLocations(const stateData& sD,
                             double dest[],
                             const solverMode& sMode,
                             const GridComponent* comp) const
{
    Lp Loc = getLocations(sD, sMode, comp);
    if ((sMode.local) || (sD.empty())) {
        Loc.destLoc = (dest == nullptr) ?
            const_cast<double*>(comp->m_state.data()) + offsetContainer[0].algOffset :
            dest;
        Loc.destDiffLoc = Loc.destLoc + Loc.algSize;
    } else if (isDAE(sMode)) {
        Loc.destLoc = dest + Loc.algOffset;
        Loc.destDiffLoc = dest + Loc.diffOffset;
    } else if (hasAlgebraic(sMode)) {
        Loc.destLoc = dest + Loc.algOffset;
        Loc.destDiffLoc = nullptr;
    } else if (hasDifferential(sMode)) {
        Loc.destDiffLoc = dest + Loc.diffOffset;
        Loc.destLoc = nullptr;
    } else {
        Loc.destLoc = (dest == nullptr) ?
            const_cast<double*>(comp->m_state.data()) + offsetContainer[0].algOffset :
            dest;
        Loc.destDiffLoc = Loc.destLoc + Loc.algSize;
    }
    return Loc;
}

Lp offsetTable::getLocations(const stateData& sD,
                             const solverMode& sMode,
                             const GridComponent* comp) const
{
    Lp Loc;
    Loc.algOffset = offsetContainer[sMode.offsetIndex].algOffset;
    Loc.diffOffset = offsetContainer[sMode.offsetIndex].diffOffset;
    Loc.diffSize = offsetContainer[sMode.offsetIndex].total.diffSize;
    Loc.algSize = offsetContainer[sMode.offsetIndex].total.algSize;
    if ((sMode.local) || (sD.empty())) {
        Loc.time = comp->prevTime;
        Loc.algStateLoc = comp->m_state.data();
        Loc.diffStateLoc = comp->m_state.data() + Loc.algSize;
        Loc.dstateLoc = comp->m_dstate_dt.data() + Loc.algSize;
        if (Loc.algOffset == kNullLocation) {
            Loc.algOffset = 0;
        }
        if (Loc.diffOffset == kNullLocation) {
            Loc.diffOffset = Loc.algSize;
        }
    } else if (isDAE(sMode)) {
        Loc.time = sD.time;
        Loc.algStateLoc = sD.state + Loc.algOffset;
        Loc.diffStateLoc = sD.state + Loc.diffOffset;
        Loc.dstateLoc = sD.dstate_dt + Loc.diffOffset;
    } else if (hasAlgebraic(sMode)) {
        Loc.time = sD.time;
        if (sD.state != nullptr) {
            Loc.algStateLoc = sD.state + Loc.algOffset;
        } else {
            Loc.algStateLoc = sD.algState + Loc.algOffset;
        }
        if ((isDynamic(sMode)) && (sD.pairIndex != kNullLocation)) {
            if (sD.diffState != nullptr) {
                Loc.diffStateLoc = sD.diffState + offsetContainer[sD.pairIndex].diffOffset;
            } else if (sD.fullState != nullptr) {
                Loc.diffStateLoc = sD.fullState + offsetContainer[sD.pairIndex].diffOffset;
            }

            if (sD.dstate_dt != nullptr) {
                Loc.dstateLoc = sD.dstate_dt + offsetContainer[sD.pairIndex].diffOffset;
            } else {
                throw std::runtime_error("Missing state required to initialize dstateLoc");
            }
        } else {
            Loc.diffStateLoc = comp->m_state.data() + offsetContainer[0].diffOffset;
            Loc.dstateLoc = comp->m_dstate_dt.data() + offsetContainer[0].diffOffset;
        }
        Loc.destDiffLoc = nullptr;
    } else if (hasDifferential(sMode)) {
        Loc.time = sD.time;
        if (sD.state != nullptr) {
            Loc.diffStateLoc = sD.state + Loc.diffOffset;
        } else {
            Loc.diffStateLoc = sD.diffState + Loc.diffOffset;
        }
        Loc.dstateLoc = sD.dstate_dt + Loc.diffOffset;
        if (sD.pairIndex != kNullLocation) {
            if (sD.algState != nullptr) {
                Loc.algStateLoc = sD.algState + offsetContainer[sD.pairIndex].algOffset;
            } else if (sD.fullState != nullptr) {
                Loc.algStateLoc = sD.fullState + offsetContainer[sD.pairIndex].algOffset;
            } else {
                throw std::runtime_error("Missing state required to initialize algStateLoc");
            }
        } else {
            Loc.algStateLoc = comp->m_state.data() + offsetContainer[0].algOffset;
        }
        Loc.destLoc = nullptr;
    } else {
        Loc.time = comp->prevTime;
        Loc.algStateLoc = comp->m_state.data();
        Loc.diffStateLoc = comp->m_state.data() + Loc.algSize;
        Loc.dstateLoc = comp->m_dstate_dt.data() + Loc.algSize;
        if (Loc.algOffset == kNullLocation) {
            Loc.algOffset = 0;
        }
        if (Loc.diffOffset == kNullLocation) {
            Loc.diffOffset = Loc.algSize;
        }
    }
    return Loc;
}

}  // namespace griddyn
