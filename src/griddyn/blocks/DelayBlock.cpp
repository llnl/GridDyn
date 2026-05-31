/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DelayBlock.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::blocks {
DelayBlock::DelayBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(differential_output);
    opFlags.set(useState);
}

DelayBlock::DelayBlock(double timeConstant, const std::string& objName):
    GridBlock(objName), mT1(timeConstant)
{
    if (std::abs(mT1) < kMin_Res) {
        opFlags.set(simplifiedMode);
    } else {
        opFlags.set(differential_output);
        opFlags.set(useState);
    }
}
DelayBlock::DelayBlock(double timeConstant, double gainValue, const std::string& objName):
    GridBlock(gainValue, objName), mT1(timeConstant)
{
    if (std::abs(mT1) < kMin_Res) {
        opFlags.set(simplifiedMode);
    } else {
        opFlags.set(differential_output);
        opFlags.set(useState);
    }
}

CoreObject* DelayBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<DelayBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mT1 = mT1;

    return nobj;
}

void DelayBlock::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if ((mT1 < kMin_Res) || (opFlags[simplifiedMode])) {
        opFlags.set(simplifiedMode);
        opFlags.reset(differential_output);
        opFlags.reset(useState);
    }

    GridBlock::dynObjectInitializeA(time0, flags);
}

void DelayBlock::dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet)
{
    GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (inputs.empty()) {
        m_state[limiter_diff] = desiredOutput[0];
    } else {
        m_state[limiter_diff] = K * (inputs[0] + bias);
    }
}

double DelayBlock::step(CoreTime time, double inputA)
{
    if (opFlags[simplifiedMode]) {
        return GridBlock::step(time, inputA);
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
            ((1.0 / mT1) * (((K * (previousInterpolatedInput + input)) / 2.0) - interpolatedValue) *
             (time - currentTime + timeStep));
    }
    prevInput = input;
    double out;
    if (stateIndex > 0) {
        out = GridBlock::step(time, input);
    } else {
        out = m_state[stateIndex];
        prevTime = time;
        m_output = out;
    }
    return out;
}

void DelayBlock::blockDerivative(double input,
                                 double didt,
                                 const StateData& stateDataRef,
                                 double deriv[],
                                 const SolverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

    deriv[offset] = ((K * (input + bias)) - stateDataRef.state[offset]) / mT1;
    if (limiter_diff > 0) {
        GridBlock::blockDerivative(input, didt, stateDataRef, deriv, sMode);
    }
}

void DelayBlock::blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataRef,
                                       MatrixData<double>& jacobian,
                                       index_t argLoc,
                                       const SolverMode& sMode)
{
    if ((isAlgebraicOnly(sMode)) || (opFlags[simplifiedMode])) {
        GridBlock::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
        return;
    }
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;
    jacobian.assignCheck(offset, argLoc, K / mT1);
    jacobian.assign(offset, offset, (-1.0 / mT1) - stateDataRef.cj);
    GridBlock::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
}

// set parameters
void DelayBlock::set(std::string_view param, std::string_view val)
{
    GridBlock::set(param, val);
}
void DelayBlock::set(std::string_view param, double val, units::unit unitType)
{
    // param = GridDynSimulation::toLower(param);
    if ((param == "t1") || (param == "t")) {
        if (opFlags[dyn_initialized]) {
            if (!opFlags[simplifiedMode]) {
                // parameter doesn't get used in simplified mode
                if (std::abs(val) < kMin_Res) {
                    throw(InvalidParameterValue(param));
                }
            }
        }
        mT1 = val;
    } else {
        GridBlock::set(param, val, unitType);
    }
}
}  // namespace griddyn::blocks
