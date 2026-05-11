/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "delayBlock.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::blocks {
delayBlock::delayBlock(const std::string& objName): Block(objName)
{
    opFlags.set(differential_output);
    opFlags.set(use_state);
}

delayBlock::delayBlock(double timeConstant, const std::string& objName):
    Block(objName), mT1(timeConstant)
{
    if (std::abs(mT1) < kMin_Res) {
        opFlags.set(simplified);
    } else {
        opFlags.set(differential_output);
        opFlags.set(use_state);
    }
}
delayBlock::delayBlock(double timeConstant, double gainValue, const std::string& objName):
    Block(gainValue, objName), mT1(timeConstant)
{
    if (std::abs(mT1) < kMin_Res) {
        opFlags.set(simplified);
    } else {
        opFlags.set(differential_output);
        opFlags.set(use_state);
    }
}

coreObject* delayBlock::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<delayBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mT1 = mT1;

    return nobj;
}

void delayBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if ((mT1 < kMin_Res) || (opFlags[simplified])) {
        opFlags.set(simplified);
        opFlags.reset(differential_output);
        opFlags.reset(use_state);
    }

    Block::dynObjectInitializeA(time0, flags);
}

void delayBlock::dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet)
{
    Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (inputs.empty()) {
        m_state[limiter_diff] = desiredOutput[0];
    } else {
        m_state[limiter_diff] = K * (inputs[0] + bias);
    }
}

double delayBlock::step(coreTime time, double inputA)
{
    if (opFlags[simplified]) {
        return Block::step(time, inputA);
    }
    const double timeDelta = time - prevTime;

    const double input = inputA + bias;
    const index_t stateIndex = limiter_diff;
    if (timeDelta >= fabs(5.0 * mT1)) {
        m_state[stateIndex] = K * input;
    } else if (timeDelta <= std::abs(0.05 * mT1)) {
        m_state[stateIndex] = m_state[stateIndex] +
            ((1.0 / mT1) * (((K * (input + prevInput)) / 2.0) - m_state[stateIndex]) * timeDelta);
    } else {
        const double timeStep = 0.05 * mT1;
        double currentTime = prevTime + timeStep;
        double currentInput = prevInput;
        double previousInterpolatedInput = prevInput;
        double interpolatedValue = m_state[stateIndex];
        while (currentTime < time) {
            currentInput = currentInput + (((input - prevInput) / timeDelta) * timeStep);
            interpolatedValue = interpolatedValue +
                ((1.0 / mT1) *
                 (((K * (previousInterpolatedInput + currentInput)) / 2.0) - interpolatedValue) *
                 timeStep);
            currentTime += timeStep;
            previousInterpolatedInput = currentInput;
        }
        m_state[stateIndex] = interpolatedValue +
            ((1.0 / mT1) *
             (((K * (previousInterpolatedInput + input)) / 2.0) - interpolatedValue) *
             (time - currentTime + timeStep));
    }
    prevInput = input;
    double out;
    if (stateIndex > 0) {
        out = Block::step(time, input);
    } else {
        out = m_state[stateIndex];
        prevTime = time;
        m_output = out;
    }
    return out;
}

void delayBlock::blockDerivative(double input,
                                 double didt,
                                 const stateData& stateDataRef,
                                 double deriv[],
                                 const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

    deriv[offset] = ((K * (input + bias)) - stateDataRef.state[offset]) / mT1;
    if (limiter_diff > 0) {
        Block::blockDerivative(input, didt, stateDataRef, deriv, sMode);
    }
}

void delayBlock::blockJacobianElements(double input,
                                       double didt,
                                       const stateData& stateDataRef,
                                       matrixData<double>& jacobian,
                                       index_t argLoc,
                                       const solverMode& sMode)
{
    if ((isAlgebraicOnly(sMode)) || (opFlags[simplified])) {
        Block::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
        return;
    }
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;
    jacobian.assignCheck(offset, argLoc, K / mT1);
    jacobian.assign(offset, offset, (-1.0 / mT1) - stateDataRef.cj);
    Block::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
}

// set parameters
void delayBlock::set(std::string_view param, std::string_view val)
{
    Block::set(param, val);
}
void delayBlock::set(std::string_view param, double val, units::unit unitType)
{
    // param = gridDynSimulation::toLower(param);
    if ((param == "t1") || (param == "t")) {
        if (opFlags[dyn_initialized]) {
            if (!opFlags[simplified]) {
                // parameter doesn't get used in simplified mode
                if (std::abs(val) < kMin_Res) {
                    throw(invalidParameterValue(param));
                }
            }
        }
        mT1 = val;
    } else {
        Block::set(param, val, unitType);
    }
}
}  // namespace griddyn::blocks
