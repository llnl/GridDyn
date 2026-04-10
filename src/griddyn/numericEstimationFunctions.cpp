/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "numericEstimationFunctions.h"

#include "gridComponent.h"
#include "utilities/matrixData.hpp"
#include <vector>

namespace griddyn {
// work in progress
void numericJacobianCalculation(gridComponent* /* comp */,
                                const IOdata& inputs,
                                const stateData& sD,
                                matrixData<double>& md,
                                const IOlocs& /*inputLocs*/,
                                const solverMode& /*sMode*/)
{
    stateData sDtest = sD;
    std::vector<double> test;
    std::vector<double> testState;
    double* residTest;
    double* stateTest;
    if (sD.hasScratch()) {
        residTest = sD.scratch1;
        stateTest = sD.scratch2;
    } else {
        auto ns = md.rowLimit();
        if (ns != kCountMax) {
            test.resize(ns);
            testState.resize(ns);
        } else {
        }
        residTest = test.data();
        stateTest = testState.data();
    }
    sDtest.scratch1 = nullptr;
    sDtest.scratch2 = nullptr;
    sDtest.state = stateTest;

    IOdata testInputs = inputs;
}

void copyObjectLocalState(gridComponent* comp,
                          const double state[],
                          double newState[],
                          const solverMode& sMode)
{
    auto sts = getObjectLocalStateIndices(comp, sMode);
    for (auto st : sts) {
        newState[st] = state[st];
    }
}

std::vector<index_t> getObjectLocalStateIndices(const gridComponent* comp, const solverMode& sMode)
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
