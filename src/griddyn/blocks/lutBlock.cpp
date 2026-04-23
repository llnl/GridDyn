/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "lutBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
#include <utility>

namespace griddyn::blocks {
lutBlock::lutBlock(const std::string& objName): Block(objName)
{
    opFlags.set(use_state);
}
coreObject* lutBlock::clone(coreObject* obj) const
{
    auto nobj = cloneBase<lutBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->lut = lut;
    nobj->b = b;
    nobj->m = m;
    nobj->vlower = vlower;
    nobj->vupper = vupper;
    nobj->lindex = lindex;
    return nobj;
}

// initial conditions
void lutBlock::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        m_state[limiter_alg] = K * computeValue(inputs[0] + bias);
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    } else {
        // TODO(pt): figure out how to invert the lookup table
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    }
}

void lutBlock::blockAlgebraicUpdate(double input,
                                    const stateData& sD,
                                    double update[],
                                    const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    update[offset] = K * computeValue(input + bias);
    if (limiter_alg > 0) {
        return Block::blockAlgebraicUpdate(input, sD, update, sMode);
    }
}

void lutBlock::blockJacobianElements(double input,
                                     double didt,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     index_t argLoc,
                                     const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    md.assignCheckCol(offset, argLoc, K * m);
    md.assign(offset, offset, -1);
    if (limiter_alg > 0) {
        Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
    }
}

// set parameters
void lutBlock::set(std::string_view param, std::string_view val)
{
    using gmlc::utilities::str2vector;
    if (param == "lut") {
        auto v2 = str2vector(std::string{val}, -kBigNum, ";,:");
        lut.clear();
        lut.emplace_back(-kBigNum, 0.0);
        lut.emplace_back(kBigNum, 0.0);
        for (size_t mm = 0; mm < v2.size(); mm += 2) {
            lut.emplace_back(v2[mm], v2[mm + 1]);
        }
        std::sort(lut.begin(), lut.end());
        lut[0].second = lut[1].second;
        (*lut.end()).second = (*(lut.end() - 1)).second;
    } else if (param == "element") {
        auto v2 = str2vector(std::string{val}, -kBigNum, ";,:");
        for (size_t mm = 0; mm < v2.size(); mm += 2) {
            lut.emplace_back(v2[mm], v2[mm + 1]);
        }
        std::sort(lut.begin(), lut.end());
        lut[0].second = lut[1].second;
        (*lut.end()).second = (*(lut.end() - 1)).second;
    } else if (param == "file") {
        gmlc::utilities::TimeSeries<double, double> ts(std::string{val});

        lut.clear();
        lut.emplace_back(-kBigNum, 0.0);
        lut.emplace_back(kBigNum, 0.0);
        for (index_t pp = 0; pp < static_cast<index_t>(ts.size()); ++pp) {
            lut.emplace_back(ts.time(pp), ts.data(pp));
        }
        std::sort(lut.begin(), lut.end());
        lut[0].second = lut[1].second;
        (*lut.end()).second = (*(lut.end() - 1)).second;
    } else {
        Block::set(param, val);
    }
}

void lutBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Block::set(param, val, unitType);
    }
}

double lutBlock::step(coreTime time, double input)
{
    m_state[limiter_alg] = K * computeValue(input + bias);

    if (limiter_alg > 0) {
        Block::step(time, input);
    } else {
        m_output = m_state[0];
        prevTime = time;
    }

    return m_state[0];
}

double lutBlock::computeValue(double input)
{
    if (input > vupper) {
        ++lindex;
        auto lower = std::lower_bound(lut.begin() + lindex, lut.end(), std::make_pair(input, 0.0));
        auto upper = lower;
        ++upper;
        lindex = static_cast<int>(upper - lut.begin());
        vlower = lower->first;
        vupper = upper->first;
        m = (upper->second - lower->second) / (vupper - vlower);
        b = lower->second;
    } else if (input < vlower) {
        --lindex;
        while (lut[lindex].first > input) {
            --lindex;
        }
        vlower = lut[lindex - 1].first;
        vupper = lut[lindex].first;
        m = (lut[lindex].second - lut[lindex - 1].second) / (vupper - vlower);
        b = lut[lindex - 1].second;
    }
    return (input - vlower) * m + b;
}

}  // namespace griddyn::blocks
