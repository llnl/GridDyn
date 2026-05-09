/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "functionBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/functionInterpreter.h"
#include "utilities/matrixData.hpp"
#include <string>

namespace griddyn::blocks {
functionBlock::functionBlock(): Block("functionBlock_#")
{
    offsets.local().local.algSize = 2;
    offsets.local().local.diffSize = 0;
    opFlags.set(use_state);
    offsets.local().local.jacSize = 3;
}

functionBlock::functionBlock(const std::string& functionName): Block("functionBlock_#")
{
    offsets.local().local.algSize = 2;
    offsets.local().local.diffSize = 0;
    opFlags.set(use_state);
    offsets.local().local.jacSize = 3;
    setFunction(functionName);
}

coreObject* functionBlock::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<functionBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->fptr = fptr;
    nobj->dfptr = dfptr;
    nobj->gain = gain;
    nobj->bias = bias;
    return nobj;
}

// initial conditions
void functionBlock::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        if (opFlags[uses_constantarg]) {
            m_state[limiter_alg] = K * fptr2(gain * (inputs[0] + bias), arg2);
        } else {
            m_state[limiter_alg] = K * fptr(gain * (inputs[0] + bias));
        }
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    } else {
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    }
}

void functionBlock::blockAlgebraicUpdate(double input,
                                         const stateData& stateDataValue,
                                         double update[],
                                         const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    if (opFlags[uses_constantarg]) {
        update[offset] = K * fptr2(gain * (input + bias), arg2);
    } else {
        update[offset] = K * fptr(gain * (input + bias));
    }
    if (limiter_alg > 0) {
        Block::blockAlgebraicUpdate(input, stateDataValue, update, sMode);
    }
}

void functionBlock::blockJacobianElements(double input,
                                          double didt,
                                          const stateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          index_t argLoc,
                                          const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    if (opFlags[uses_constantarg]) {
        const double temp1 = fptr2(gain * (input + bias), arg2);
        const double temp2 = fptr2(gain * (input + 1e-8 + bias), arg2);
        matrixDataValue.assignCheck(offset, argLoc, K * (temp2 - temp1) / 1e-8);
    } else {
        if (dfptr != nullptr) {
            matrixDataValue.assignCheck(offset, argLoc, K * dfptr(gain * (input + bias)) * gain);
        } else {
            const double temp1 = fptr(gain * (input + bias));
            const double temp2 = fptr(gain * (input + 1e-8 + bias));
            matrixDataValue.assignCheck(offset, argLoc, K * (temp2 - temp1) / 1e-8);
        }
    }
    matrixDataValue.assign(offset, offset, -1);
    if (limiter_alg > 0) {
        Block::blockJacobianElements(input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
    }
}

double functionBlock::step(coreTime time, double input)
{
    if (opFlags[uses_constantarg]) {
        m_state[limiter_alg] = K * fptr2(gain * (input + bias), arg2);
    } else {
        m_state[limiter_alg] = K * fptr(gain * (input + bias));
    }
    if (limiter_alg > 0) {
        Block::step(time, input);
    }
    m_output = m_state[0];
    prevTime = time;
    return m_state[0];
}

// set parameters
void functionBlock::set(std::string_view param, std::string_view val)
{
    if ((param == "function") || (param == "func")) {
        auto functionName = gmlc::utilities::convertToLowerCase(val);
        setFunction(functionName);
    } else {
        Block::set(param, val);
    }
}

void functionBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "gain") {
        gain = val;
    } else if (param == "arg") {
        arg2 = val;
    } else {
        Block::set(param, val, unitType);
    }
}

void functionBlock::setFunction(const std::string& functionName)
{
    if (auto unaryFunctionPtr = get1ArgFunction(functionName)) {
        fptr = unaryFunctionPtr;
        dfptr = getDerivative1ArgFunction(functionName);
        fptr2 = nullptr;
        opFlags.reset(uses_constantarg);
    } else if (auto binaryFunctionPtr = get2ArgFunction(functionName)) {
        fptr = nullptr;
        dfptr = nullptr;
        fptr2 = binaryFunctionPtr;
        opFlags.set(uses_constantarg);
    } else {
        fptr = nullptr;
        dfptr = nullptr;
        fptr2 = nullptr;
    }
}

/*
double functionBlock::currentValue(const IOdata &inputs, const stateData &sD,
const solverMode &sMode) const
{
auto Loc;
offsets.getLocations(sD, sMode, &Loc, this);
double val = Loc.algStateLoc[1];
if (!inputs.empty())
{
if (opFlags[uses_constantarg])
{
  val = fptr2(gain*(inputs[0] + bias), arg2);
}
else
{
  val = fptr(gain*(inputs[0] + bias));
}
}
return Block::currentValue({ val }, sD, sMode);
}

double functionBlock::currentValue() const
{
return m_state[0];
}
*/
}  // namespace griddyn::blocks
