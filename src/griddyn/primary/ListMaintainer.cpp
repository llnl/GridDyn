/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ListMaintainer.h"

#include "../GridArea.h"
#include "griddyn/griddyn-config.h"
#include <vector>

namespace griddyn {
void fillList(const SolverMode& sMode,
              std::vector<GridPrimary*>& list,
              std::vector<GridPrimary*>& partlist,
              const std::vector<GridPrimary*>& possObjs);

ListMaintainer::ListMaintainer(): objectLists(4), partialLists(4), sModeLists(4) {}

void ListMaintainer::makeList(const SolverMode& sMode, const std::vector<GridPrimary*>& possObjs)
{
    if (sMode.offsetIndex >= static_cast<index_t>(objectLists.size())) {
        objectLists.resize(sMode.offsetIndex + 1);
        partialLists.resize(sMode.offsetIndex + 1);
        sModeLists.resize(sMode.offsetIndex + 1);
        sModeLists[sMode.offsetIndex] = sMode;
        objectLists[sMode.offsetIndex].reserve(possObjs.size());
        partialLists[sMode.offsetIndex].reserve(possObjs.size());
    }
    objectLists[sMode.offsetIndex].clear();
    partialLists[sMode.offsetIndex].clear();
    fillList(sMode, objectLists[sMode.offsetIndex], partialLists[sMode.offsetIndex], possObjs);
    sModeLists[sMode.offsetIndex] = sMode;
}

void ListMaintainer::appendList(const SolverMode& sMode, const std::vector<GridPrimary*>& possObjs)
{
    if (sMode.offsetIndex >= static_cast<index_t>(objectLists.size())) {
        objectLists.resize(sMode.offsetIndex + 1);
        partialLists.resize(sMode.offsetIndex + 1);
        sModeLists.resize(sMode.offsetIndex + 1);
        sModeLists[sMode.offsetIndex] = sMode;
        objectLists[sMode.offsetIndex].reserve(possObjs.size());
        partialLists[sMode.offsetIndex].reserve(possObjs.size());
    }
    fillList(sMode, objectLists[sMode.offsetIndex], partialLists[sMode.offsetIndex], possObjs);
}

void fillList(const SolverMode& sMode,
              std::vector<GridPrimary*>& list,
              std::vector<GridPrimary*>& partlist,
              const std::vector<GridPrimary*>& possObjs)
{
    for (auto& obj : possObjs) {
        if (obj->checkFlag(preEx_requested)) {
            if (obj->checkFlag(multipart_calculation_capable)) {
                partlist.push_back(obj);
                list.push_back(obj);
            } else if (obj->stateSize(sMode) > 0) {
                list.push_back(obj);
            }
        } else if (obj->stateSize(sMode) > 0) {
            partlist.push_back(obj);
            list.push_back(obj);
        }
    }
}

void ListMaintainer::makePreList(const std::vector<GridPrimary*>& possObjs)
{
    preExObjs.clear();
    for (auto& obj : possObjs) {
        if (obj->checkFlag(preEx_requested)) {
            preExObjs.push_back(obj);
        }
    }
}

void ListMaintainer::preEx(const IOdata& inputs,
                           const StateData& stateDataValue,
                           const SolverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->preEx(inputs, stateDataValue, sMode);
    }
}

void ListMaintainer::jacobianElements(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      matrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    if (!isListValid(sMode)) {
        return;
    }
#ifdef ENABLE_OPENMP_GRIDDYN
    if (parJac) {
        auto& vz = partialLists[sMode.offsetIndex];
        int sz = static_cast<int>(vz.size());
#    pragma omp parallel for
        for (int kk = 0; kk < sz; ++kk) {
            vz[kk]->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    }
#endif
}

void ListMaintainer::residual(const IOdata& inputs,
                              const StateData& stateDataValue,
                              double resid[],
                              const SolverMode& sMode)
{
    if (!isListValid(sMode)) {
        return;
    }

#ifdef ENABLE_OPENMP_GRIDDYN
    if (parResid) {
        auto& vz = partialLists[sMode.offsetIndex];
        int sz = static_cast<index_t>(vz.size());
#    pragma omp parallel for
        for (index_t kk = 0; kk < sz; ++kk) {
            vz[kk]->residual(inputs, stateDataValue, resid, sMode);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->residual(inputs, stateDataValue, resid, sMode);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->residual(inputs, stateDataValue, resid, sMode);
    }
#endif
}

void ListMaintainer::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     double update[],
                                     const SolverMode& sMode,
                                     double alpha)
{
    if (!isListValid(sMode)) {
        return;
    }

#ifdef ENABLE_OPENMP_GRIDDYN
    if (parAlgebraic) {
        auto& vz = partialLists[sMode.offsetIndex];
        int sz = static_cast<index_t>(vz.size());
#    pragma omp parallel for
        for (index_t kk = 0; kk < sz; ++kk) {
            vz[kk]->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
    }
#endif
}

void ListMaintainer::derivative(const IOdata& inputs,
                                const StateData& stateDataValue,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!isListValid(sMode)) {
        return;
    }
#ifdef ENABLE_OPENMP_GRIDDYN
    if (parDeriv) {
        auto& vz = partialLists[sMode.offsetIndex];
        index_t sz = static_cast<index_t>(vz.size());
#    pragma omp parallel for
        for (index_t kk = 0; kk < sz; ++kk) {
            vz[kk]->derivative(inputs, stateDataValue, deriv, sMode);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->derivative(inputs, stateDataValue, deriv, sMode);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->derivative(inputs, stateDataValue, deriv, sMode);
    }
#endif
}

void ListMaintainer::delayedResidual(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     double resid[],
                                     const SolverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->delayedResidual(inputs, stateDataValue, resid, sMode);
    }
}
void ListMaintainer::delayedDerivative(const IOdata& inputs,
                                       const StateData& stateDataValue,
                                       double deriv[],
                                       const SolverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->delayedDerivative(inputs, stateDataValue, deriv, sMode);
    }
}

void ListMaintainer::delayedJacobian(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     matrixData<double>& matrixDataValue,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->delayedJacobian(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    }
}

void ListMaintainer::delayedAlgebraicUpdate(const IOdata& inputs,
                                            const StateData& stateDataValue,
                                            double update[],
                                            const SolverMode& sMode,
                                            double alpha)
{
    for (auto& obj : preExObjs) {
        obj->delayedAlgebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
    }
}

bool ListMaintainer::isListValid(const SolverMode& sMode) const
{
    if (isValidIndex(sMode.offsetIndex, objectLists)) {
        return (sModeLists[sMode.offsetIndex].offsetIndex != kNullLocation);
    }
    return false;
}

void ListMaintainer::invalidate(const SolverMode& sMode)
{
    if (isValidIndex(sMode.offsetIndex, objectLists)) {
        sModeLists[sMode.offsetIndex] = SolverMode();
    }
}

void ListMaintainer::invalidate()
{
    for (auto& sml : sModeLists) {
        sml = SolverMode();
    }
}

decltype(ListMaintainer::objectLists[0].begin()) ListMaintainer::begin(const SolverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].begin();
    }
    return objectLists[0].end();
}

decltype(ListMaintainer::objectLists[0].end()) ListMaintainer::end(const SolverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].end();
    }
    return objectLists[0].end();
}

decltype(ListMaintainer::objectLists[0].cbegin())
    ListMaintainer::cbegin(const SolverMode& sMode) const
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].cbegin();
    }
    return objectLists[0].cend();
}

decltype(ListMaintainer::objectLists[0].cend()) ListMaintainer::cend(const SolverMode& sMode) const
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].cend();
    }
    return objectLists[0].cend();
}

decltype(ListMaintainer::objectLists[0].rbegin()) ListMaintainer::rbegin(const SolverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].rbegin();
    }
    return objectLists[0].rend();
}

decltype(ListMaintainer::objectLists[0].rend()) ListMaintainer::rend(const SolverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].rend();
    }
    return objectLists[0].rend();
}

}  // namespace griddyn
