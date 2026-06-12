/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "NumericEstimationFunctions.h"

#include "GridComponent.h"
#include "utilities/MatrixData.hpp"
#include <ranges>
#include <vector>

namespace griddyn {
namespace {
    void appendStateRange(std::vector<index_t>& states, index_t offset, index_t count)
    {
        auto stateRange = std::views::iota(index_t{0}, count) |
            std::views::transform([offset](index_t stateIndex) { return offset + stateIndex; });
        std::ranges::copy(stateRange, std::back_inserter(states));
    }
}  // namespace

// work in progress
void numericJacobianCalculation(GridComponent* /* comp */,
                                const IOdata& inputs,
                                const StateData& sD,
                                MatrixData<double>& md,
                                const IOlocs& /*inputLocs*/,
                                const SolverMode& /*sMode*/)
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
                          const SolverMode& sMode)
{
    auto sts = getObjectLocalStateIndices(comp, sMode);
    for (auto st : sts) {
        newState[st] = state[st];
    }
}

std::vector<index_t> getObjectLocalStateIndices(const GridComponent* comp, const SolverMode& sMode)
{
    std::vector<index_t> states;
    const auto& offsets = comp->getOffsets(sMode);
    states.reserve(static_cast<size_t>(offsets.local.vSize) + offsets.local.aSize +
                   offsets.local.algSize + offsets.local.diffSize);
    if (hasAlgebraic(sMode)) {
        appendStateRange(states, offsets.vOffset, offsets.local.vSize);
        appendStateRange(states, offsets.aOffset, offsets.local.aSize);
        appendStateRange(states, offsets.algOffset, offsets.local.algSize);
    }
    if (hasDifferential(sMode)) {
        appendStateRange(states, offsets.diffOffset, offsets.local.diffSize);
    }
    return states;
}

}  // namespace griddyn
