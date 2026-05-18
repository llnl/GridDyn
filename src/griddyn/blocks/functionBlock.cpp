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
FunctionBlock::FunctionBlock(): GridBlock("functionBlock_#")
{
    offsets.local().local.algSize = 2;
    offsets.local().local.diffSize = 0;
    opFlags.set(use_state);
    offsets.local().local.jacSize = 3;
}

FunctionBlock::FunctionBlock(const std::string& functionName): GridBlock("functionBlock_#")
{
    offsets.local().local.algSize = 2;
    offsets.local().local.diffSize = 0;
    opFlags.set(use_state);
    offsets.local().local.jacSize = 3;
    setFunction(functionName);
}

CoreObject* FunctionBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FunctionBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mFunctionPtr = mFunctionPtr;
    nobj->mDerivativeFunctionPtr = mDerivativeFunctionPtr;
    nobj->mBinaryFunctionPtr = mBinaryFunctionPtr;
    nobj->mGain = mGain;
    nobj->mArg2 = mArg2;
    nobj->bias = bias;
    return nobj;
}

// initial conditions
void FunctionBlock::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        if (opFlags[USES_CONSTANT_ARG]) {
            m_state[limiter_alg] = K * mBinaryFunctionPtr(mGain * (inputs[0] + bias), mArg2);
        } else {
            m_state[limiter_alg] = K * mFunctionPtr(mGain * (inputs[0] + bias));
        }
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    } else {
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    }
}

void FunctionBlock::blockAlgebraicUpdate(double input,
                                         const stateData& stateDataValue,
                                         double update[],
                                         const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    if (opFlags[USES_CONSTANT_ARG]) {
        update[offset] = K * mBinaryFunctionPtr(mGain * (input + bias), mArg2);
    } else {
        update[offset] = K * mFunctionPtr(mGain * (input + bias));
    }
    if (limiter_alg > 0) {
        GridBlock::blockAlgebraicUpdate(input, stateDataValue, update, sMode);
    }
}

void FunctionBlock::blockJacobianElements(double input,
                                          double didt,
                                          const stateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          index_t argLoc,
                                          const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    if (opFlags[USES_CONSTANT_ARG]) {
        const double temp1 = mBinaryFunctionPtr(mGain * (input + bias), mArg2);
        const double temp2 = mBinaryFunctionPtr(mGain * (input + 1e-8 + bias), mArg2);
        matrixDataValue.assignCheck(offset, argLoc, K * (temp2 - temp1) / 1e-8);
    } else {
        if (mDerivativeFunctionPtr != nullptr) {
            matrixDataValue.assignCheck(offset,
                                        argLoc,
                                        K * mDerivativeFunctionPtr(mGain * (input + bias)) * mGain);
        } else {
            const double temp1 = mFunctionPtr(mGain * (input + bias));
            const double temp2 = mFunctionPtr(mGain * (input + 1e-8 + bias));
            matrixDataValue.assignCheck(offset, argLoc, K * (temp2 - temp1) / 1e-8);
        }
    }
    matrixDataValue.assign(offset, offset, -1);
    if (limiter_alg > 0) {
        GridBlock::blockJacobianElements(input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
    }
}

double FunctionBlock::step(coreTime time, double input)
{
    if (opFlags[USES_CONSTANT_ARG]) {
        m_state[limiter_alg] = K * mBinaryFunctionPtr(mGain * (input + bias), mArg2);
    } else {
        m_state[limiter_alg] = K * mFunctionPtr(mGain * (input + bias));
    }
    if (limiter_alg > 0) {
        GridBlock::step(time, input);
    }
    m_output = m_state[0];
    prevTime = time;
    return m_state[0];
}

// set parameters
void FunctionBlock::set(std::string_view param, std::string_view val)
{
    if ((param == "function") || (param == "func")) {
        auto functionName = gmlc::utilities::convertToLowerCase(val);
        setFunction(functionName);
    } else {
        GridBlock::set(param, val);
    }
}

void FunctionBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "gain") {
        mGain = val;
    } else if (param == "arg") {
        mArg2 = val;
    } else {
        GridBlock::set(param, val, unitType);
    }
}

void FunctionBlock::setFunction(const std::string& functionName)
{
    if (auto unaryFunctionPtr = get1ArgFunction(functionName)) {
        mFunctionPtr = unaryFunctionPtr;
        mDerivativeFunctionPtr = getDerivative1ArgFunction(functionName);
        mBinaryFunctionPtr = nullptr;
        opFlags.reset(USES_CONSTANT_ARG);
    } else if (auto binaryFunctionPtr = get2ArgFunction(functionName)) {
        mFunctionPtr = nullptr;
        mDerivativeFunctionPtr = nullptr;
        mBinaryFunctionPtr = binaryFunctionPtr;
        opFlags.set(USES_CONSTANT_ARG);
    } else {
        mFunctionPtr = nullptr;
        mDerivativeFunctionPtr = nullptr;
        mBinaryFunctionPtr = nullptr;
    }
}

/*
double FunctionBlock::currentValue(const IOdata &inputs, const stateData &sD,
const solverMode &sMode) const
{
auto Loc;
offsets.getLocations(sD, sMode, &Loc, this);
double val = Loc.algStateLoc[1];
if (!inputs.empty())
{
if (opFlags[USES_CONSTANT_ARG])
{
  val = fptr2(gain*(inputs[0] + bias), arg2);
}
else
{
  val = fptr(gain*(inputs[0] + bias));
}
}
return GridBlock::currentValue({ val }, sD, sMode);
}

double FunctionBlock::currentValue() const
{
return m_state[0];
}
*/
}  // namespace griddyn::blocks
