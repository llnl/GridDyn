/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "IntegralBlock.h"

#include "core/coreObjectTemplates.hpp"
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
    auto nobj = cloneBase<IntegralBlock, GridBlock>(this, obj);
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
    index_t loc = limiter_diff;
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
                                  const stateData& sD,
                                  double resid[],
                                  const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        GridBlock::blockResidual(input, didt, sD, resid, sMode);
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);
    resid[offset] = (K * (input + bias) - sD.dstate_dt[offset]);
    GridBlock::blockResidual(input, didt, sD, resid, sMode);
}

void IntegralBlock::blockDerivative(double input,
                                    double didt,
                                    const stateData& sD,
                                    double deriv[],
                                    const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    deriv[offset + limiter_diff] = K * (input + bias);
    if (opFlags[use_ramp_limits]) {
        GridBlock::blockDerivative(input, didt, sD, deriv, sMode);
    }
}

void IntegralBlock::blockJacobianElements(double input,
                                          double didt,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          index_t argLoc,
                                          const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        GridBlock::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
    }
    auto offset = offsets.getDiffOffset(sMode);
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    md.assignCheck(offset, argLoc, K);
    md.assign(offset, offset, -sD.cj);
    GridBlock::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
}

double IntegralBlock::step(coreTime time, double inputA)
{
    double dt = time - prevTime;
    double out;
    double input = inputA + bias;
    index_t loc = limiter_diff + limiter_alg;
    m_state[loc] = m_state[loc] + K * (input + prevInput) / 2.0 * dt;
    prevInput = input;
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
