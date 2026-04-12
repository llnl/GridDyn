/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "listMaintainer.h"

#include "../Area.h"
#include "griddyn/griddyn-config.h"
#include <vector>

namespace griddyn {
void fillList(const solverMode& sMode,
              std::vector<gridPrimary*>& list,
              std::vector<gridPrimary*>& partlist,
              const std::vector<gridPrimary*>& possObjs);

listMaintainer::listMaintainer(): objectLists(4), partialLists(4), sModeLists(4) {}

void listMaintainer::makeList(const solverMode& sMode, const std::vector<gridPrimary*>& possObjs)
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

void listMaintainer::appendList(const solverMode& sMode, const std::vector<gridPrimary*>& possObjs)
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

void fillList(const solverMode& sMode,
              std::vector<gridPrimary*>& list,
              std::vector<gridPrimary*>& partlist,
              const std::vector<gridPrimary*>& possObjs)
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

void listMaintainer::makePreList(const std::vector<gridPrimary*>& possObjs)
{
    preExObjs.clear();
    for (auto& obj : possObjs) {
        if (obj->checkFlag(preEx_requested)) {
            preExObjs.push_back(obj);
        }
    }
}

void listMaintainer::preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->preEx(inputs, sD, sMode);
    }
}

void listMaintainer::jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode)
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
            vz[kk]->jacobianElements(inputs, sD, md, inputLocs, sMode);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->jacobianElements(inputs, sD, md, inputLocs, sMode);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->jacobianElements(inputs, sD, md, inputLocs, sMode);
    }
#endif
}

void listMaintainer::residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode)
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
            vz[kk]->residual(inputs, sD, resid, sMode);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->residual(inputs, sD, resid, sMode);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->residual(inputs, sD, resid, sMode);
    }
#endif
}

void listMaintainer::algebraicUpdate(const IOdata& inputs,
                                     const stateData& sD,
                                     double update[],
                                     const solverMode& sMode,
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
            vz[kk]->algebraicUpdate(inputs, sD, update, sMode, alpha);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->algebraicUpdate(inputs, sD, update, sMode, alpha);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->algebraicUpdate(inputs, sD, update, sMode, alpha);
    }
#endif
}

void listMaintainer::derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode)
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
            vz[kk]->derivative(inputs, sD, deriv, sMode);
        }
    } else {
        for (auto& obj : partialLists[sMode.offsetIndex]) {
            obj->derivative(inputs, sD, deriv, sMode);
        }
    }

#else
    for (auto& obj : partialLists[sMode.offsetIndex]) {
        obj->derivative(inputs, sD, deriv, sMode);
    }
#endif
}

void listMaintainer::delayedResidual(const IOdata& inputs,
                                     const stateData& sD,
                                     double resid[],
                                     const solverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->delayedResidual(inputs, sD, resid, sMode);
    }
}
void listMaintainer::delayedDerivative(const IOdata& inputs,
                                       const stateData& sD,
                                       double deriv[],
                                       const solverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->delayedDerivative(inputs, sD, deriv, sMode);
    }
}

void listMaintainer::delayedJacobian(const IOdata& inputs,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    for (auto& obj : preExObjs) {
        obj->delayedJacobian(inputs, sD, md, inputLocs, sMode);
    }
}

void listMaintainer::delayedAlgebraicUpdate(const IOdata& inputs,
                                            const stateData& sD,
                                            double update[],
                                            const solverMode& sMode,
                                            double alpha)
{
    for (auto& obj : preExObjs) {
        obj->delayedAlgebraicUpdate(inputs, sD, update, sMode, alpha);
    }
}

bool listMaintainer::isListValid(const solverMode& sMode) const
{
    if (isValidIndex(sMode.offsetIndex, objectLists)) {
        return (sModeLists[sMode.offsetIndex].offsetIndex != kNullLocation);
    }
    return false;
}

void listMaintainer::invalidate(const solverMode& sMode)
{
    if (isValidIndex(sMode.offsetIndex, objectLists)) {
        sModeLists[sMode.offsetIndex] = solverMode();
    }
}

void listMaintainer::invalidate()
{
    for (auto& sml : sModeLists) {
        sml = solverMode();
    }
}

decltype(listMaintainer::objectLists[0].begin()) listMaintainer::begin(const solverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].begin();
    }
    return objectLists[0].end();
}

decltype(listMaintainer::objectLists[0].end()) listMaintainer::end(const solverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].end();
    }
    return objectLists[0].end();
}

decltype(listMaintainer::objectLists[0].cbegin())
    listMaintainer::cbegin(const solverMode& sMode) const
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].cbegin();
    }
    return objectLists[0].cend();
}

decltype(listMaintainer::objectLists[0].cend()) listMaintainer::cend(const solverMode& sMode) const
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].cend();
    }
    return objectLists[0].cend();
}

decltype(listMaintainer::objectLists[0].rbegin()) listMaintainer::rbegin(const solverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].rbegin();
    }
    return objectLists[0].rend();
}

decltype(listMaintainer::objectLists[0].rend()) listMaintainer::rend(const solverMode& sMode)
{
    if (isListValid(sMode)) {
        return objectLists[sMode.offsetIndex].rend();
    }
    return objectLists[0].rend();
}

}  // namespace griddyn
