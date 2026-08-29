/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "TransferFunctionBlock.h"

#include "ValueLimiter.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::blocks {
TransferFunctionBlock::TransferFunctionBlock(const std::string& objName):
    GridBlock(objName), a{1.0, 1.0}, b{1.0, 0.0}
{
    opFlags.set(USE_STATE);
}

TransferFunctionBlock::TransferFunctionBlock(int orderValue):
    GridBlock("transferFunctionBlock_#"), a(static_cast<size_t>(std::max(0, orderValue)) + 1, 0.0),
    b(a.size(), 0.0)
{
    a.front() = 1.0;
    a.back() = 1.0;
    b.front() = 1.0;
    opFlags.set(USE_STATE);
}

TransferFunctionBlock::TransferFunctionBlock(std::vector<double> acoef):
    GridBlock("transferFunctionBlock_#"), a(std::move(acoef)), b(a.size(), 0.0)
{
    if (a.empty()) {
        a.push_back(1.0);
        b.assign(1, 1.0);
    } else {
        b.front() = 1.0;
    }
    opFlags.set(USE_STATE);
}

TransferFunctionBlock::TransferFunctionBlock(std::vector<double> acoef, std::vector<double> bcoef):
    GridBlock("transferFunctionBlock_#"), a(std::move(acoef)), b(std::move(bcoef))
{
    if (a.empty()) {
        a.push_back(1.0);
    }
    b.resize(a.size(), 0.0);
    opFlags.set(USE_STATE);
}

CoreObject* TransferFunctionBlock::clone(CoreObject* obj) const
{
    auto* clone = cloneBase<TransferFunctionBlock, GridBlock>(this, obj);
    if (clone == nullptr) {
        return obj;
    }
    clone->a = a;
    clone->b = b;
    return clone;
}

index_t TransferFunctionBlock::order() const
{
    return static_cast<index_t>(a.size() - 1);
}

void TransferFunctionBlock::validateCoefficients()
{
    if (a.empty()) {
        throw InvalidParameterValue("transfer-function denominator");
    }
    if (b.size() > a.size()) {
        throw InvalidParameterValue("transfer-function numerator order");
    }
    b.resize(a.size(), 0.0);
    const bool invalidDenominator =
        std::any_of(a.begin(), a.end(), [](double value) { return !std::isfinite(value); }) ||
        (std::abs(a.back()) < kMin_Res);
    const bool invalidNumerator =
        std::any_of(b.begin(), b.end(), [](double value) { return !std::isfinite(value); });
    if (invalidDenominator || invalidNumerator || !std::isfinite(K) || !std::isfinite(bias)) {
        throw InvalidParameterValue("transfer-function coefficients, gain, or bias");
    }
}

void TransferFunctionBlock::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateCoefficients();
    opFlags.reset(DIFFERENTIAL_OUTPUT);
    // GridBlock's convenience use_limits flag also requests rate limits.  This
    // block has an algebraic output, so retain only its supported output limit.
    opFlags.reset(USE_RAMP_LIMITS);
    GridBlock::dynObjectInitializeA(time0, flags);
    offsets.local().local.diffSize += order();
    offsets.local().local.jacSize += static_cast<count_t>((3 * order()) + 3);
}

double TransferFunctionBlock::rawOutput(double input, const double state[]) const
{
    const index_t stateCount = order();
    const double denominatorScale = a.back();
    if (stateCount == 0) {
        return K * (b.front() / denominatorScale) * input;
    }

    const double directTerm = b.back() / denominatorScale;
    double output = directTerm * input;
    for (index_t index = 0; index < stateCount; ++index) {
        output += ((b[index] / denominatorScale) - (directTerm * a[index] / denominatorScale)) *
            state[index];
    }
    return K * output;
}

void TransferFunctionBlock::stateDerivative(double input,
                                            const double state[],
                                            double derivative[]) const
{
    const index_t stateCount = order();
    if (stateCount == 0) {
        return;
    }
    for (index_t index = 0; index + 1 < stateCount; ++index) {
        derivative[index] = state[index + 1];
    }
    double finalDerivative = input;
    const double denominatorScale = a.back();
    for (index_t index = 0; index < stateCount; ++index) {
        finalDerivative -= (a[index] / denominatorScale) * state[index];
    }
    derivative[stateCount - 1] = finalDerivative;
}

double TransferFunctionBlock::externalStateOutput(double input, const double state[]) const
{
    return rawOutput(input, state);
}

void TransferFunctionBlock::externalStateDerivative(double input,
                                                    const double state[],
                                                    double derivative[]) const
{
    stateDerivative(input, state, derivative);
}

void TransferFunctionBlock::dynObjectInitializeB(const IOdata& inputs,
                                                 const IOdata& desiredOutput,
                                                 IOdata& fieldSet)
{
    fieldSet.resize(1);
    const index_t stateCount = order();
    const index_t rawOutputIndex = limiter_alg;
    const index_t stateStart = limiter_alg + 1;
    double input = inputs.empty() ? 0.0 : inputs[0] + bias;

    if (!desiredOutput.empty()) {
        const double dcGain = K * b.front() / a.front();
        if (!std::isfinite(dcGain) || (std::abs(a.front()) < kMin_Res) ||
            (std::abs(dcGain) < kMin_Res)) {
            throw InvalidParameterValue(
                "transfer-function desired-output initialization requires finite nonzero DC gain");
        }
        const double limitedOutput = opFlags[USE_BLOCK_LIMITS] ?
            std::clamp(desiredOutput[0], static_cast<double>(Omin), static_cast<double>(Omax)) :
            desiredOutput[0];
        input = limitedOutput / dcGain;
        fieldSet[0] = input - bias;
    }

    if (stateCount > 0) {
        if (std::abs(a.front()) < kMin_Res) {
            if (std::abs(input) >= kMin_Res) {
                throw InvalidParameterValue(
                    "transfer-function equilibrium requires a nonzero denominator constant");
            }
        } else {
            m_state[stateStart] = input * a.back() / a.front();
        }
        for (index_t index = 1; index < stateCount; ++index) {
            m_state[stateStart + index] = 0.0;
        }
    }

    m_state[rawOutputIndex] = rawOutput(input, m_state.data() + stateStart);
    if (opFlags[USE_BLOCK_LIMITS]) {
        GridBlock::rootCheck({input - bias},
                             emptyStateData,
                             cLocalSolverMode,
                             CheckLevel::REVERSABLE_ONLY);
        m_state[0] = vLimiter->clampOutput(m_state[rawOutputIndex]);
    }
    if (desiredOutput.empty()) {
        fieldSet[0] = m_state[0];
    }
    m_output = m_state[0];
    prevInput = input;
}

void TransferFunctionBlock::blockResidual(double input,
                                          double /*didt*/,
                                          const StateData& stateData,
                                          double residual[],
                                          const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, residual, sMode, this);
    const index_t stateCount = order();
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index + 1 < stateCount; ++index) {
            locations.destDiffLoc[index] =
                locations.diffStateLoc[index + 1] - locations.dstateLoc[index];
        }
        if (stateCount > 0) {
            double finalDerivative = input + bias;
            for (index_t index = 0; index < stateCount; ++index) {
                finalDerivative -= (a[index] / a.back()) * locations.diffStateLoc[index];
            }
            locations.destDiffLoc[stateCount - 1] =
                finalDerivative - locations.dstateLoc[stateCount - 1];
        }
    }
    if (hasAlgebraic(sMode)) {
        const index_t rawOutputIndex = limiter_alg;
        locations.destLoc[rawOutputIndex] =
            rawOutput(input + bias, locations.diffStateLoc) - locations.algStateLoc[rawOutputIndex];
        if (limiter_alg > 0) {
            locations.destLoc[rawOutputIndex - 1] =
                vLimiter->output(locations.algStateLoc[rawOutputIndex]) -
                locations.algStateLoc[rawOutputIndex - 1];
        }
    }
}

void TransferFunctionBlock::blockDerivative(double input,
                                            double /*didt*/,
                                            const StateData& stateData,
                                            double derivative[],
                                            const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, derivative, sMode, this);
    stateDerivative(input + bias, locations.diffStateLoc, locations.destDiffLoc);
}

void TransferFunctionBlock::blockAlgebraicUpdate(double input,
                                                 const StateData& stateData,
                                                 double update[],
                                                 const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    const index_t rawOutputIndex = limiter_alg;
    locations.destLoc[rawOutputIndex] = rawOutput(input + bias, locations.diffStateLoc);
    if (limiter_alg > 0) {
        locations.destLoc[rawOutputIndex - 1] =
            vLimiter->output(locations.algStateLoc[rawOutputIndex]);
    }
}

void TransferFunctionBlock::blockJacobianElements(double /*input*/,
                                                  double /*didt*/,
                                                  const StateData& stateData,
                                                  MatrixData<double>& matrixData,
                                                  index_t inputLocation,
                                                  const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t stateCount = order();
    const double denominatorScale = a.back();
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < stateCount; ++index) {
            const index_t row = locations.diffOffset + index;
            if (index + 1 < stateCount) {
                matrixData.assign(row, row, -stateData.cj);
                matrixData.assign(row, locations.diffOffset + index + 1, 1.0);
            } else {
                for (index_t column = 0; column < stateCount; ++column) {
                    matrixData.assign(row,
                                      locations.diffOffset + column,
                                      -a[column] / denominatorScale -
                                          ((column == index) ? stateData.cj : 0.0));
                }
                matrixData.assignCheckCol(row, inputLocation, 1.0);
            }
        }
    }
    if (hasAlgebraic(sMode)) {
        const index_t rawOutputIndex = limiter_alg;
        const index_t rawOutputLocation = locations.algOffset + rawOutputIndex;
        matrixData.assign(rawOutputLocation, rawOutputLocation, -1.0);
        if (stateCount == 0) {
            matrixData.assignCheckCol(rawOutputLocation,
                                      inputLocation,
                                      K * b.front() / denominatorScale);
        } else {
            const double directTerm = b.back() / denominatorScale;
            if (hasDifferential(sMode)) {
                for (index_t index = 0; index < stateCount; ++index) {
                    matrixData.assign(rawOutputLocation,
                                      locations.diffOffset + index,
                                      K *
                                          ((b[index] / denominatorScale) -
                                           (directTerm * a[index] / denominatorScale)));
                }
            }
            matrixData.assignCheckCol(rawOutputLocation, inputLocation, K * directTerm);
        }
        if (limiter_alg > 0) {
            const index_t limitedOutputLocation = rawOutputLocation - 1;
            matrixData.assign(limitedOutputLocation, limitedOutputLocation, -1.0);
            matrixData.assign(limitedOutputLocation, rawOutputLocation, vLimiter->DoutDin());
        }
    }
}

double TransferFunctionBlock::step(CoreTime time, double inputValue)
{
    const index_t stateCount = order();
    const index_t rawOutputIndex = limiter_alg;
    const index_t stateStart = limiter_alg + 1;
    const double input = inputValue + bias;
    const double timeStep = time - prevTime;
    if ((stateCount > 0) && (timeStep > 0.0)) {
        const double halfStep = timeStep / 2.0;
        std::vector<double> systemMatrix(static_cast<size_t>(stateCount) * static_cast<size_t>(stateCount), 0.0);
        std::vector<double> rightHandSide(stateCount, 0.0);
        const double denominatorScale = a.back();
        for (index_t row = 0; row < stateCount; ++row) {
            rightHandSide[row] = m_state[stateStart + row];
            for (index_t column = 0; column < stateCount; ++column) {
                double systemValue = (row == column) ? 1.0 : 0.0;
                double systemEntry = 0.0;
                if (row + 1 < stateCount) {
                    systemEntry = (column == row + 1) ? 1.0 : 0.0;
                } else {
                    systemEntry = -a[column] / denominatorScale;
                }
                systemValue -= halfStep * systemEntry;
                rightHandSide[row] += halfStep * systemEntry * m_state[stateStart + column];
                systemMatrix[row * stateCount + column] = systemValue;
            }
            if (row + 1 == stateCount) {
                rightHandSide[row] += halfStep * (prevInput + input);
            }
        }
        for (index_t pivot = 0; pivot < stateCount; ++pivot) {
            index_t pivotRow = pivot;
            for (index_t row = pivot + 1; row < stateCount; ++row) {
                if (std::abs(systemMatrix[row * stateCount + pivot]) >
                    std::abs(systemMatrix[pivotRow * stateCount + pivot])) {
                    pivotRow = row;
                }
            }
            if (std::abs(systemMatrix[pivotRow * stateCount + pivot]) < kMin_Res) {
                throw InvalidParameterValue("singular transfer-function timestep matrix");
            }
            if (pivotRow != pivot) {
                for (index_t column = pivot; column < stateCount; ++column) {
                    std::swap(systemMatrix[pivot * stateCount + column],
                              systemMatrix[pivotRow * stateCount + column]);
                }
                std::swap(rightHandSide[pivot], rightHandSide[pivotRow]);
            }
            const double pivotValue = systemMatrix[pivot * stateCount + pivot];
            for (index_t row = pivot + 1; row < stateCount; ++row) {
                const double scale = systemMatrix[row * stateCount + pivot] / pivotValue;
                for (index_t column = pivot; column < stateCount; ++column) {
                    systemMatrix[row * stateCount + column] -=
                        scale * systemMatrix[pivot * stateCount + column];
                }
                rightHandSide[row] -= scale * rightHandSide[pivot];
            }
        }
        for (index_t row = stateCount; row-- > 0;) {
            double value = rightHandSide[row];
            for (index_t column = row + 1; column < stateCount; ++column) {
                value -= systemMatrix[row * stateCount + column] * m_state[stateStart + column];
            }
            m_state[stateStart + row] = value / systemMatrix[row * stateCount + row];
        }
    }
    m_state[rawOutputIndex] = rawOutput(input, m_state.data() + stateStart);
    if (opFlags[USE_BLOCK_LIMITS]) {
        GridBlock::rootCheck({inputValue},
                             emptyStateData,
                             cLocalSolverMode,
                             CheckLevel::REVERSABLE_ONLY);
        m_state[0] = vLimiter->output(m_state[rawOutputIndex]);
    }
    prevInput = input;
    prevTime = time;
    m_output = m_state[0];
    return m_output;
}

index_t TransferFunctionBlock::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if (field == "output") {
        return offsets.getAlgOffset(sMode);
    }
    std::string prefix;
    const int index = gmlc::utilities::stringOps::trailingStringInt(field, prefix, -1);
    if ((index >= 0) && (prefix == "x")) {
        if ((index >= 0) && (static_cast<index_t>(index) < order())) {
            return offsets.getDiffOffset(sMode) + static_cast<index_t>(index);
        }
    }
    return GridBlock::findIndex(field, sMode);
}

void TransferFunctionBlock::set(std::string_view param, std::string_view value)
{
    if (param == "a") {
        a = gmlc::utilities::str2vector<double>(std::string{value}, 0.0);
        if (a.empty()) {
            throw InvalidParameterValue("transfer-function denominator");
        }
        if (b.size() > a.size()) {
            throw InvalidParameterValue("transfer-function numerator order");
        }
        b.resize(a.size(), 0.0);
        return;
    }
    if (param == "b") {
        const auto numerator = gmlc::utilities::str2vector<double>(std::string{value}, 0.0);
        if (numerator.size() > a.size()) {
            throw InvalidParameterValue("transfer-function numerator order");
        }
        b = numerator;
        b.resize(a.size(), 0.0);
        return;
    }
    GridBlock::set(param, value);
}

void TransferFunctionBlock::set(std::string_view param, double value, units::unit unitType)
{
    std::string prefix;
    const int coefficientIndex = gmlc::utilities::stringOps::trailingStringInt(param, prefix, -1);
    if ((coefficientIndex >= 0) && ((prefix == "a") || (prefix == "b"))) {
        const auto index = static_cast<size_t>(coefficientIndex);
        if (prefix == "a") {
            if (index >= a.size()) {
                a.resize(index + 1, 0.0);
                b.resize(index + 1, 0.0);
            }
            a[index] = value;
        } else {
            if (index >= a.size()) {
                throw InvalidParameterValue("transfer-function numerator order");
            }
            b[index] = value;
        }
        return;
    }
    GridBlock::set(param, value, unitType);
}

stringVec TransferFunctionBlock::localStateNames() const
{
    auto names = GridBlock::localStateNames();
    if (names.empty()) {
        names.emplace_back("output");
    } else {
        names.emplace_back("transfer_output");
    }
    for (index_t index = 0; index < order(); ++index) {
        names.emplace_back("x" + std::to_string(index));
    }
    return names;
}
}  // namespace griddyn::blocks
