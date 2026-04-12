/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "outputEstimator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace griddyn {
namespace fmi {

    outputEstimator::outputEstimator(std::vector<int> sDep, std::vector<int> iDep):
        stateDep(std::move(sDep)), inputDep(std::move(iDep))
    {
        stateDiff.resize(stateDep.size(), 0);
        inputDiff.resize(inputDep.size(), 0);
        prevStates.resize(stateDep.size());
        prevInputs.resize(inputDep.size());
    }

    double outputEstimator::estimate(coreTime t, const IOdata& inputs, const double state[])
    {
        double val = prevValue;
        for (size_t kk = 0; kk < stateDep.size(); ++kk) {
            val += (state[stateDep[kk]] - prevStates[kk]) * stateDiff[kk];
        }
        for (size_t kk = 0; kk < inputDep.size(); ++kk) {
            val += (inputs[inputDep[kk]] - prevInputs[kk]) * inputDiff[kk];
        }
        val += timeDiff * (t - time);
        return val;
    }

    bool outputEstimator::update(coreTime t, double val, const IOdata& inputs, const double state[])
    {
        time = t;

        double pred = estimate(t, inputs, state);
        prevValue = val;
        for (size_t kk = 0; kk < stateDep.size(); ++kk) {
            prevStates[kk] = state[stateDep[kk]];
        }
        for (size_t kk = 0; kk < inputDep.size(); ++kk) {
            prevInputs[kk] = inputs[inputDep[kk]];
        }
        double diff = (std::abs)(pred - val);
        double scaled_error = diff / (std::max)(std::abs(pred), std::abs(val));
        bool diff_large_enough = diff > 1e-4;
        bool scaled_error_large_enough = scaled_error > 0.02;
        return diff_large_enough && scaled_error_large_enough;
    }

}  // namespace fmi
}  // namespace griddyn
