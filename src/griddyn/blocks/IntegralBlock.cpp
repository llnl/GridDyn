/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "IntegralBlock.h"

#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <string>
namespace griddyn::blocks {
IntegralBlock::IntegralBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(differential_output);
    opFlags.set(use_state);
}

IntegralBlock::IntegralBlock(double gain, const std::string& objName): GridBlock(gain, objName)
{
    opFlags.set(differential_output);
    opFlags.set(use_state);
}

CoreObject* IntegralBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<IntegralBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->iv = iv;
    return nobj;
}

// initial conditions
void IntegralBlock::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    const index_t loc = limiter_diff;
    if (desiredOutput.empty()) {
        m_state[loc] = iv;
        if (limiter_diff > 0) {
            GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        }
        m_dstate_dt[loc] = K * (inputs[0] + bias);
    } else {
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    }
}

// residual
void IntegralBlock::blockResidual(double input,
                                  double didt,
                                  const stateData& stateDataValue,
                                  double resid[],
                                  const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        GridBlock::blockResidual(input, didt, stateDataValue, resid, sMode);
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);
    resid[offset] = ((K * (input + bias)) - stateDataValue.dstate_dt[offset]);
    GridBlock::blockResidual(input, didt, stateDataValue, resid, sMode);
}

void IntegralBlock::blockDerivative(double input,
                                    double didt,
                                    const stateData& stateDataValue,
                                    double deriv[],
                                    const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    deriv[offset + limiter_diff] = K * (input + bias);
    if (opFlags[use_ramp_limits]) {
        GridBlock::blockDerivative(input, didt, stateDataValue, deriv, sMode);
    }
}

void IntegralBlock::blockJacobianElements(double input,
                                          double didt,
                                          const stateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          index_t argLoc,
                                          const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        GridBlock::blockJacobianElements(
            input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
    }
    auto offset = offsets.getDiffOffset(sMode);
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    matrixDataValue.assignCheck(offset, argLoc, K);
    matrixDataValue.assign(offset, offset, -stateDataValue.cj);
    GridBlock::blockJacobianElements(input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
}

double IntegralBlock::step(coreTime time, double inputA)
{
    const double timeDelta = time - prevTime;
    double out;
    const double inputValue = inputA + bias;
    const index_t loc = limiter_diff + limiter_alg;
    m_state[loc] = m_state[loc] + ((K * (inputValue + prevInput) / 2.0) * timeDelta);
    prevInput = inputValue;
    if (loc > 0) {
        out = GridBlock::step(time, inputA);
    } else {
        out = m_state[0];
        prevTime = time;
        m_output = out;
    }
    return out;
}

// set parameters
void IntegralBlock::set(std::string_view param, std::string_view val)
{
    GridBlock::set(param, val);
}
void IntegralBlock::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "iv") || (param == "initial_value")) {
        iv = val;
    } else if (param == "t") {
        if (val != 0) {
            K = 1.0 / val;
        }
    } else {
        GridBlock::set(param, val, unitType);
    }
}
}  // namespace griddyn::blocks
