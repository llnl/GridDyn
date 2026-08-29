/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LutBlock.h"

#include "ValueLimiter.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace griddyn::blocks {
LutBlock::LutBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(USE_STATE);
}
CoreObject* LutBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<LutBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->lut = lut;
    return nobj;
}

// initial conditions
void LutBlock::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    validateTable(lut);
    fieldSet.resize(1);
    double input = inputs.empty() ? 0.0 : inputs[0] + bias;

    if (desiredOutput.empty()) {
        if (inputs.empty()) {
            throw InvalidParameterValue("LUT initialization requires an input or desired output");
        }
    } else {
        if (!std::isfinite(K) || (std::abs(K) < kMin_Res)) {
            throw InvalidParameterValue(
                "LUT desired-output initialization requires a finite nonzero gain");
        }
        const double limitedOutput = opFlags[USE_BLOCK_LIMITS] ?
            std::clamp(desiredOutput[0], static_cast<double>(Omin), static_cast<double>(Omax)) :
            desiredOutput[0];
        input = inverseValue(limitedOutput / K);
        fieldSet[0] = input - bias;
    }

    const double output = K * evaluate(input).value;
    if (opFlags[USE_BLOCK_LIMITS]) {
        m_state[limiter_alg] = output;
        GridBlock::rootCheck({input - bias},
                             emptyStateData,
                             cLocalSolverMode,
                             CheckLevel::REVERSABLE_ONLY);
        m_state[0] = vLimiter->clampOutput(m_state[limiter_alg]);
    } else {
        m_state[0] = output;
        m_output = output;
    }
    if (desiredOutput.empty()) {
        fieldSet[0] = opFlags[USE_BLOCK_LIMITS] ? m_state[0] : output;
    }
    if (opFlags[USE_BLOCK_LIMITS]) {
        m_output = m_state[0];
    }
    prevInput = input;
}

void LutBlock::blockAlgebraicUpdate(double input,
                                    const StateData& stateDataValue,
                                    double update[],
                                    const SolverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    update[offset] = K * evaluate(input + bias).value;
    if (limiter_alg > 0) {
        GridBlock::blockAlgebraicUpdate(input, stateDataValue, update, sMode);
        return;
    }
}

void LutBlock::blockJacobianElements(double input,
                                     double didt,
                                     const StateData& stateDataValue,
                                     MatrixData<double>& matrixDataValue,
                                     index_t argLoc,
                                     const SolverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    matrixDataValue.assignCheckCol(offset, argLoc, K * evaluate(input + bias).slope);
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
        if ((vectorData.size() % 2) != 0) {
            throw InvalidParameterValue("LUT table requires x,y point pairs");
        }
        std::vector<std::pair<double, double>> table;
        table.reserve(vectorData.size() / 2);
        for (size_t index = 0; index < vectorData.size(); index += 2) {
            table.emplace_back(vectorData[index], vectorData[index + 1]);
        }
        std::sort(table.begin(), table.end());
        validateTable(table);
        lut = std::move(table);
    } else if (param == "element") {
        const auto vectorData = str2vector(std::string{val}, -kBigNum, ";,:");
        if ((vectorData.size() % 2) != 0) {
            throw InvalidParameterValue("LUT table requires x,y point pairs");
        }
        auto table = lut;
        table.reserve(table.size() + (vectorData.size() / 2));
        for (size_t index = 0; index < vectorData.size(); index += 2) {
            table.emplace_back(vectorData[index], vectorData[index + 1]);
        }
        std::sort(table.begin(), table.end());
        validateTable(table);
        lut = std::move(table);
    } else if (param == "file") {
        const gmlc::utilities::TimeSeries<double, double> timeSeries(std::string{val});

        std::vector<std::pair<double, double>> table;
        table.reserve(timeSeries.size());
        for (gmlc::utilities::fsize_t pointIndex = 0; pointIndex < timeSeries.size();
             ++pointIndex) {
            table.emplace_back(timeSeries.time(pointIndex), timeSeries.data(pointIndex));
        }
        std::sort(table.begin(), table.end());
        validateTable(table);
        lut = std::move(table);
    } else {
        GridBlock::set(param, val);
    }
}

void LutBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (!param.empty() && param[0] != '#') {
        GridBlock::set(param, val, unitType);
    }
}

double LutBlock::step(CoreTime time, double input)
{
    const double output = K * evaluate(input + bias).value;

    if (limiter_alg > 0) {
        m_state[limiter_alg] = output;
        GridBlock::step(time, input);
    } else {
        m_state[0] = output;
        m_output = output;
        prevTime = time;
    }

    return (limiter_alg > 0) ? m_state[0] : output;
}

void LutBlock::validateTable(const std::vector<std::pair<double, double>>& table)
{
    if (table.empty()) {
        throw InvalidParameterValue("LUT table requires at least one point");
    }
    for (size_t index = 0; index < table.size(); ++index) {
        if (!std::isfinite(table[index].first) || !std::isfinite(table[index].second)) {
            throw InvalidParameterValue("LUT table values must be finite");
        }
        if ((index > 0) && !(table[index].first > table[index - 1].first)) {
            throw InvalidParameterValue("LUT table abscissas must be strictly increasing");
        }
    }
}

LutBlock::LookupResult LutBlock::evaluate(double input) const
{
    validateTable(lut);
    if (lut.size() == 1 || input <= lut.front().first) {
        return {.value = lut.front().second, .slope = 0.0};
    }
    if (input >= lut.back().first) {
        return {.value = lut.back().second, .slope = 0.0};
    }

    const auto upper =
        std::upper_bound(lut.begin(), lut.end(), input, [](double value, const auto& point) {
            return value < point.first;
        });
    const auto lower = std::prev(upper);
    const double slope = (upper->second - lower->second) / (upper->first - lower->first);
    return {.value = lower->second + ((input - lower->first) * slope), .slope = slope};
}

double LutBlock::inverseValue(double value) const
{
    validateTable(lut);
    bool nondecreasing = true;
    bool nonincreasing = true;
    for (size_t index = 1; index < lut.size(); ++index) {
        nondecreasing = nondecreasing && (lut[index].second >= lut[index - 1].second);
        nonincreasing = nonincreasing && (lut[index].second <= lut[index - 1].second);
    }
    if (!nondecreasing && !nonincreasing) {
        throw InvalidParameterValue("LUT desired-output initialization requires a monotonic table");
    }

    const double minimum = std::min(lut.front().second, lut.back().second);
    const double maximum = std::max(lut.front().second, lut.back().second);
    if ((value < minimum) || (value > maximum)) {
        throw InvalidParameterValue("LUT desired output is outside the table range");
    }
    if (lut.size() == 1) {
        return lut.front().first;
    }
    for (size_t index = 1; index < lut.size(); ++index) {
        const auto& lower = lut[index - 1];
        const auto& upper = lut[index];
        if ((value >= std::min(lower.second, upper.second)) &&
            (value <= std::max(lower.second, upper.second))) {
            if (upper.second == lower.second) {
                return lower.first;
            }
            return lower.first +
                ((value - lower.second) * (upper.first - lower.first) /
                 (upper.second - lower.second));
        }
    }
    return lut.back().first;
}

double LutBlock::computeValue(double input) const
{
    return evaluate(input).value;
}

}  // namespace griddyn::blocks
