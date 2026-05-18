/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "derivativeBlock.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactory.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>

namespace griddyn::blocks {
DerivativeBlock::DerivativeBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(use_state);
}
DerivativeBlock::DerivativeBlock(double timeConstant, const std::string& objName):
    GridBlock(objName), mT1(timeConstant)
{
    opFlags.set(use_state);
}

CoreObject* DerivativeBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<DerivativeBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mT1 = mT1;

    return nobj;
}

void DerivativeBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridBlock::dynObjectInitializeA(time0, flags);
    offsets.local().local.diffSize++;
    offsets.local().local.jacSize += 2;
}

void DerivativeBlock::dynObjectInitializeB(const IOdata& inputs,
                                           const IOdata& desiredOutput,
                                           IOdata& fieldSet)
{
    const index_t loc = limiter_alg;  // can't have a ramp limiter
    if (desiredOutput.empty()) {
        m_state[loc + 1] = K * (inputs[0] + bias);
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        m_state[loc] = 0;
    } else {
        GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        m_dstate_dt[loc + 1] = desiredOutput[0];
        if (std::abs(m_dstate_dt[loc + 1]) < 1e-7) {
            m_state[loc + 1] = K * (inputs[0] + bias);
        } else {
            m_state[loc + 1] = (m_state[loc] - (m_dstate_dt[loc + 1] * mT1));
        }
    }
}

double DerivativeBlock::step(coreTime time, double inputA)
{
    const index_t loc = limiter_alg;
    const double timeDelta = time - prevTime;
    double out;
    const double input = inputA + bias;
    double intermediateValue;
    if (timeDelta >= fabs(5.0 * mT1)) {
        m_state[loc + 1] = K * input;
        m_state[loc] = 0;
    } else if (timeDelta <= fabs(0.05 * mT1)) {
        m_state[loc + 1] = m_state[loc + 1] +
            ((1.0 / mT1) * (((K * (input + prevInput)) / 2.0) - m_state[loc + 1]) * timeDelta);
        m_state[loc] = (1.0 / mT1) * (((K * (input + prevInput)) / 2.0) - m_state[loc + 1]);
    } else {
        const double timeStep = 0.05 * mT1;
        double currentTime = prevTime + timeStep;
        double currentInput = prevInput;
        double previousInterpolatedInput = prevInput;
        intermediateValue = m_state[loc + 1];
        while (currentTime < time) {
            currentInput = currentInput + (((input - prevInput) / timeDelta) * timeStep);
            intermediateValue = intermediateValue +
                ((K / mT1) *
                 (((previousInterpolatedInput + currentInput) / 2.0) - intermediateValue) *
                 timeStep);
            currentTime += timeStep;
            previousInterpolatedInput = currentInput;
        }
        m_state[loc + 1] = intermediateValue +
            ((K / mT1) * (((previousInterpolatedInput + input) / 2.0) - intermediateValue) *
             (time - currentTime + timeStep));
        m_state[loc] =
            (K / mT1) * (((previousInterpolatedInput + input) / 2.0) - intermediateValue);
    }
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

void DerivativeBlock::blockAlgebraicUpdate(double input,
                                           const stateData& stateDataRef,
                                           double update[],
                                           const solverMode& sMode)
{
    auto locationData = offsets.getLocations(stateDataRef, update, sMode, this);
    locationData.destLoc[limiter_alg] = locationData.dstateLoc[0];
    //    update[Aoffset + limiter_alg] = sD.state[Aoffset + limiter_alg] -
    // sD.dstate_dt[offset];
    GridBlock::blockAlgebraicUpdate(input, stateDataRef, update, sMode);
}

void DerivativeBlock::blockDerivative(double input,
                                      double /*didt*/,
                                      const stateData& stateDataRef,
                                      double deriv[],
                                      const solverMode& sMode)
{
    auto offset =
        offsets.getDiffOffset(sMode);  // limiter diff must be 0 since the output is algebraic

    deriv[offset] = ((K * (input + bias)) - stateDataRef.state[offset]) / mT1;
}

void DerivativeBlock::blockJacobianElements(double input,
                                            double didt,
                                            const stateData& stateDataRef,
                                            matrixData<double>& jacobian,
                                            index_t argLoc,
                                            const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    if (hasDifferential(sMode)) {
        jacobian.assignCheck(offset, argLoc, K / mT1);
        jacobian.assign(offset, offset, (-1.0 / mT1) - stateDataRef.cj);
    } else {
        offset = kNullLocation;
    }
    if (hasAlgebraic(sMode)) {
        auto algebraicOffset = offsets.getAlgOffset(sMode) + limiter_alg;
        jacobian.assignCheck(algebraicOffset, offset, stateDataRef.cj);
        jacobian.assign(algebraicOffset, algebraicOffset, -1);
        if (limiter_alg > 0) {
            GridBlock::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
        }
    }
}

// set parameters
void DerivativeBlock::set(std::string_view param, std::string_view val)
{
    GridBlock::set(param, val);
}
void DerivativeBlock::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "t1") || (param == "t")) {
        if (std::abs(val) < kMin_Res) {
            throw(invalidParameterValue(param));
        }
        mT1 = val;
    } else {
        GridBlock::set(param, val, unitType);
    }
}

stringVec DerivativeBlock::localStateNames() const
{
    auto bbstates = GridBlock::localStateNames();
    bbstates.emplace_back("deriv");
    bbstates.emplace_back("delayI");
    return bbstates;
}
}  // namespace griddyn::blocks
