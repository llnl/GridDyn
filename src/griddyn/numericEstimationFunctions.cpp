/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "numericEstimationFunctions.h"

#include "gridComponent.h"
#include "utilities/matrixData.hpp"
#include <vector>

namespace griddyn {
// work in progress
void numericJacobianCalculation(GridComponent* /* comp */,
                                const IOdata& inputs,
                                const stateData& sD,
                                matrixData<double>& md,
                                const IOlocs& /*inputLocs*/,
                                const solverMode& /*sMode*/)
{
    std::vector<double> test;
    [[maybe_unused]] std::vector<double> testState;
    [[maybe_unused]] double* stateTest = sD.scratch2;
    if (!sD.hasScratch()) {
        auto ns = md.rowLimit();
        if (ns != kCountMax) {
            test.resize(ns);
            testState.resize(ns);
        }
        stateTest = testState.data();
    }

    [[maybe_unused]] IOdata testInputs = inputs;
}

void copyObjectLocalState(GridComponent* comp,
                          const double state[],
                          double newState[],
                          const solverMode& sMode)
{
    auto sts = getObjectLocalStateIndices(comp, sMode);
    for (auto st : sts) {
        newState[st] = state[st];
    }
}

std::vector<index_t> getObjectLocalStateIndices(const GridComponent* comp, const solverMode& sMode)
{
    std::vector<index_t> states;
    const auto& offsets = comp->getOffsets(sMode);
    if (hasAlgebraic(sMode)) {
        if (offsets.local.vSize > 0) {
            for (index_t ii = 0; ii < offsets.local.vSize; ++ii) {
                states.push_back(offsets.vOffset + ii);
            }
        }
        if (offsets.local.aSize > 0) {
            for (index_t ii = 0; ii < offsets.local.aSize; ++ii) {
                states.push_back(offsets.aOffset + ii);
            }
        }
        if (offsets.local.algSize > 0) {
            for (index_t ii = 0; ii < offsets.local.algSize; ++ii) {
                states.push_back(offsets.algOffset + ii);
            }
        }
    }
    if (hasDifferential(sMode)) {
        if (offsets.local.diffSize > 0) {
            for (index_t ii = 0; ii < offsets.local.diffSize; ++ii) {
                states.push_back(offsets.diffOffset + ii);
            }
        }
    }
    return states;
}

}  // namespace griddyn

