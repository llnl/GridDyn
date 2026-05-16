/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "outputEstimator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace griddyn::fmi {

OutputEstimator::OutputEstimator(std::vector<int> sDep, std::vector<int> iDep):
    stateDep(std::move(sDep)), inputDep(std::move(iDep))
{
    stateDiff.resize(stateDep.size(), 0);
    inputDiff.resize(inputDep.size(), 0);
    prevStates.resize(stateDep.size());
    prevInputs.resize(inputDep.size());
}

double OutputEstimator::estimate(coreTime timeVal, const IOdata& inputs, const double state[])
{
    double val = prevValue;
    for (size_t kk = 0; kk < stateDep.size(); ++kk) {
        val += (state[stateDep[kk]] - prevStates[kk]) * stateDiff[kk];
    }
    for (size_t kk = 0; kk < inputDep.size(); ++kk) {
        val += (inputs[inputDep[kk]] - prevInputs[kk]) * inputDiff[kk];
    }
    val += timeDiff * (timeVal - time);
    return val;
}

bool OutputEstimator::update(coreTime timeVal,
                             double val,
                             const IOdata& inputs,
                             const double state[])
{
    time = timeVal;

    const double pred = estimate(timeVal, inputs, state);
    prevValue = val;
    for (size_t kk = 0; kk < stateDep.size(); ++kk) {
        prevStates[kk] = state[stateDep[kk]];
    }
    for (size_t kk = 0; kk < inputDep.size(); ++kk) {
        prevInputs[kk] = inputs[inputDep[kk]];
    }
    const double diff = std::abs(pred - val);
    const double scaled_error = diff / (std::max)(std::abs(pred), std::abs(val));
    const bool diff_large_enough = diff > 1e-4;
    const bool scaled_error_large_enough = scaled_error > 0.02;
    return diff_large_enough && scaled_error_large_enough;
}

}  // namespace griddyn::fmi
