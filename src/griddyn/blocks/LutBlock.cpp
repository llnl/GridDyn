/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LutBlock.h"

#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
#include <utility>

namespace griddyn::blocks {
LutBlock::LutBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(useState);
}
CoreObject* LutBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<LutBlock, GridBlock>(this, obj);
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
void LutBlock::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        m_state[limiter_alg] = K * computeValue(inputs[0] + bias);
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    } else {
        // TODO(pt): figure out how to invert the lookup table
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    }
}

void LutBlock::blockAlgebraicUpdate(double input,
                                    const StateData& stateDataValue,
                                    double update[],
                                    const SolverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    update[offset] = K * computeValue(input + bias);
    if (limiter_alg > 0) {
        GridBlock::blockAlgebraicUpdate(input, stateDataValue, update, sMode);
        return;
    }
}

void LutBlock::blockJacobianElements(double input,
                                     double didt,
                                     const StateData& stateDataValue,
                                     matrixData<double>& matrixDataValue,
                                     index_t argLoc,
                                     const SolverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    matrixDataValue.assignCheckCol(offset, argLoc, K * m);
    matrixDataValue.assign(offset, offset, -1);
    if (limiter_alg > 0) {
        GridBlock::blockJacobianElements(
            input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
    }
}

// set parameters
void LutBlock::set(std::string_view param, std::string_view val)
{
    using gmlc::utilities::str2vector;
    if (param == "lut") {
        const auto vectorData = str2vector(std::string{val}, -kBigNum, ";,:");
        lut.clear();
        lut.emplace_back(-kBigNum, 0.0);
        lut.emplace_back(kBigNum, 0.0);
        for (size_t mm = 0; mm < vectorData.size(); mm += 2) {
            lut.emplace_back(vectorData[mm], vectorData[mm + 1]);
        }
        std::sort(lut.begin(), lut.end());
        lut[0].second = lut[1].second;
        (*lut.end()).second = (*(lut.end() - 1)).second;
    } else if (param == "element") {
        const auto vectorData = str2vector(std::string{val}, -kBigNum, ";,:");
        for (size_t mm = 0; mm < vectorData.size(); mm += 2) {
            lut.emplace_back(vectorData[mm], vectorData[mm + 1]);
        }
        std::sort(lut.begin(), lut.end());
        lut[0].second = lut[1].second;
        (*lut.end()).second = (*(lut.end() - 1)).second;
    } else if (param == "file") {
        const gmlc::utilities::TimeSeries<double, double> timeSeries(std::string{val});

        lut.clear();
        lut.emplace_back(-kBigNum, 0.0);
        lut.emplace_back(kBigNum, 0.0);
        for (gmlc::utilities::fsize_t pointIndex = 0; pointIndex < timeSeries.size();
             ++pointIndex) {
            lut.emplace_back(timeSeries.time(pointIndex), timeSeries.data(pointIndex));
        }
        std::sort(lut.begin(), lut.end());
        lut[0].second = lut[1].second;
        (*lut.end()).second = (*(lut.end() - 1)).second;
    } else {
        GridBlock::set(param, val);
    }
}

void LutBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty() || param[0] == '#') {
    } else {
        GridBlock::set(param, val, unitType);
    }
}

double LutBlock::step(coreTime time, double input)
{
    m_state[limiter_alg] = K * computeValue(input + bias);

    if (limiter_alg > 0) {
        GridBlock::step(time, input);
    } else {
        m_output = m_state[0];
        prevTime = time;
    }

    return m_state[0];
}

double LutBlock::computeValue(double input)
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
    return (((input - vlower) * m) + b);
}

}  // namespace griddyn::blocks
