/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FilteredDerivativeBlock.h"

#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::blocks {
FilteredDerivativeBlock::FilteredDerivativeBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(use_state);
    opFlags.set(differential_output);
}

FilteredDerivativeBlock::FilteredDerivativeBlock(double preDerivativeTimeConstant,
                                                 double derivativeFilterTimeConstant,
                                                 const std::string& objName):
    GridBlock(objName), mT1(preDerivativeTimeConstant), mT2(derivativeFilterTimeConstant)
{
    opFlags.set(use_state);
    opFlags.set(differential_output);
}

CoreObject* FilteredDerivativeBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FilteredDerivativeBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mT1 = mT1;
    nobj->mT2 = mT2;
    return nobj;
}

void FilteredDerivativeBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridBlock::dynObjectInitializeA(time0, flags);
    offsets.local().local.diffSize++;
    offsets.local().local.jacSize += 2;
}

void FilteredDerivativeBlock::dynObjectInitializeB(const IOdata& inputs,
                                                   const IOdata& desiredOutput,
                                                   IOdata& fieldSet)
{
    const index_t loc = limiter_diff;  // can't have a ramp limiter
    if (desiredOutput.empty()) {
        m_state[loc + 1] = K * (inputs[0] + bias);
        m_state[loc] = 0;
        if (limiter_diff > 0) {
            GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        }
    } else {
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        m_state[loc] = desiredOutput[0];
        m_dstate_dt[loc + 1] = desiredOutput[0];
        if (std::abs(m_dstate_dt[loc + 1]) < 1e-7) {
            m_state[loc + 1] = K * (inputs[0] + bias);
        } else {
            m_state[loc + 1] = (m_state[loc] - (m_dstate_dt[loc + 1] * mT1));
        }
    }
}

double FilteredDerivativeBlock::step(coreTime time, double inputA)
{
    const index_t loc = limiter_diff;
    const double timeDelta = time - prevTime;

    const double input = inputA + bias;
    if ((timeDelta >= fabs(5.0 * mT1)) && (timeDelta >= fabs(5.0 * mT2))) {
        m_state[loc + 1] = K * input;
        m_state[loc] = 0;
    } else {
        const double timeStep = 0.05 * std::min(mT1, mT2);
        double currentTime = prevTime + timeStep;
        double currentInput = prevInput;
        double previousInterpolatedInput = prevInput;
        double derivativeState = m_state[loc + 1];
        double filterState = m_state[loc];
        double derivativeValue;
        while (currentTime < time) {
            currentInput = currentInput + (((input - prevInput) / timeDelta) * timeStep);
            derivativeValue =
                (K / mT1) * (((previousInterpolatedInput + currentInput) / 2.0) - derivativeState);
            derivativeState = derivativeState + (derivativeValue * timeStep);
            filterState = filterState + (((derivativeValue - filterState) / mT2) * timeStep);
            currentTime += timeStep;
            previousInterpolatedInput = currentInput;
        }
        m_state[loc + 1] = derivativeState +
            ((K / mT1) * (((previousInterpolatedInput + input) / 2.0) - derivativeState) *
             (time - currentTime + timeStep));
        m_state[loc] = filterState +
            ((((K / mT1) * (((previousInterpolatedInput + input) / 2.0) - derivativeState)) -
              filterState) /
             mT2) *
                (time - currentTime + timeStep);
    }
    prevInput = input;
    double out;
    if (loc > 0) {
        out = GridBlock::step(time, inputA);
    } else {
        out = m_state[0];
        prevTime = time;
        m_output = out;
    }
    return out;
}

void FilteredDerivativeBlock::blockDerivative(double input,
                                              double /*didt*/,
                                              const stateData& stateDataRef,
                                              double deriv[],
                                              const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

    deriv[offset + 1] = ((K * (input + bias)) - stateDataRef.state[offset + 1]) / mT1;
    deriv[offset] = (stateDataRef.dstate_dt[offset + 1] - stateDataRef.state[offset]) / mT2;
}

void FilteredDerivativeBlock::blockJacobianElements(double input,
                                                    double didt,
                                                    const stateData& stateDataRef,
                                                    matrixData<double>& jacobian,
                                                    index_t argLoc,
                                                    const solverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

    jacobian.assignCheckCol(offset + 1, argLoc, K / mT1);
    jacobian.assign(offset + 1, offset + 1, (-1 / mT1) - stateDataRef.cj);

    jacobian.assign(offset, offset + 1, stateDataRef.cj / mT2);
    jacobian.assign(offset, offset, (-1 / mT2) - stateDataRef.cj);

    if (limiter_diff > 0) {
        GridBlock::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
    }
}

// set parameters
void FilteredDerivativeBlock::set(std::string_view param, std::string_view val)
{
    GridBlock::set(param, val);
}
void FilteredDerivativeBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "t1") {
        mT1 = val;
    } else if (param == "t2") {
        if (std::abs(val) < kMin_Res) {
            throw(invalidParameterValue(param));
        }
        mT2 = val;
    } else {
        GridBlock::set(param, val, unitType);
    }
}

stringVec FilteredDerivativeBlock::localStateNames() const
{
    switch (limiter_diff) {
        case 0:
            return {"deriv", "filter"};
        case 1:
            if (opFlags[use_block_limits]) {
                return {"limited", "deriv", "filter"};
            } else {
                return {"ramp_limited", "deriv", "filter"};
            }
        default:  // should be 0, 1 or 2 so this should run with 2
            return {"limited", "ramp_limited", "deriv", "filter"};
    }
}
}  // namespace griddyn::blocks
