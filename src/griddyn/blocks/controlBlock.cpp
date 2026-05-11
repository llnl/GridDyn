/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "controlBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <string>

namespace griddyn::blocks {
controlBlock::controlBlock(const std::string& objName): Block(objName)
{
    opFlags.set(use_state);
}
controlBlock::controlBlock(double timeConstant, const std::string& objName):
    Block(objName), mT1(timeConstant)
{
    opFlags.set(use_state);
}
controlBlock::controlBlock(double timeConstant,
                           double upperTimeConstant,
                           const std::string& objName):
    Block(objName), mT1(timeConstant), mT2(upperTimeConstant)
{
    opFlags.set(use_state);
}

coreObject* controlBlock::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<controlBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mT2 = mT2;
    nobj->mT1 = mT1;
    return nobj;
}
// set up the number of states
void controlBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (opFlags[differential_input]) {
        opFlags.set(differential_output);
    }
    Block::dynObjectInitializeA(time0, flags);

    offsets.local().local.diffSize += 1;
    offsets.local().local.jacSize += 6;
}
// initial conditions
void controlBlock::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    fieldSet.resize(1);
    if (opFlags[has_limits]) {
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    }
    if (desiredOutput.empty()) {
        m_state[limiter_alg + 1] = K * (1.0 - (mT2 / mT1)) * (inputs[0] + bias);
        m_state[limiter_alg] = K * (inputs[0] + bias);

        fieldSet[0] = m_state[0];
        prevInput = inputs[0] + bias;
    } else {
        m_state[limiter_alg] = desiredOutput[0];
        m_state[limiter_alg + 1] = (1.0 - (mT2 / mT1)) * desiredOutput[0] / K;
        fieldSet[0] = (desiredOutput[0] / K) - bias;
        prevInput = desiredOutput[0] / K;
    }
}

void controlBlock::blockAlgebraicUpdate(double input,
                                        const stateData& stateDataRef,
                                        double update[],
                                        const solverMode& sMode)
{
    if (!opFlags[differential_input]) {
        auto locationData = offsets.getLocations(stateDataRef, update, sMode, this);

        locationData.destLoc[limiter_alg] =
            locationData.diffStateLoc[0] + ((mT2 / mT1) * (input + bias) * K);
        if (limiter_alg > 0) {
            Block::blockAlgebraicUpdate(input, stateDataRef, update, sMode);
        }
    }
}

void controlBlock::blockDerivative(double input,
                                   double didt,
                                   const stateData& stateDataRef,
                                   double deriv[],
                                   const solverMode& sMode)
{
    auto locationData = offsets.getLocations(stateDataRef, deriv, sMode, this);
    if (opFlags[differential_input]) {
        locationData.destDiffLoc[limiter_diff] =
            locationData.dstateLoc[limiter_diff + 1] + ((mT2 / mT1) * didt * K);
        locationData.destDiffLoc[limiter_diff + 1] =
            ((K * (input + bias)) - locationData.diffStateLoc[limiter_diff]) / mT1;
        if (limiter_diff > 0) {
            Block::blockDerivative(input, didt, stateDataRef, deriv, sMode);
        }
    } else {
        locationData.destDiffLoc[0] =
            ((K * (input + bias)) - locationData.algStateLoc[limiter_alg]) / mT1;
    }
}

void controlBlock::blockJacobianElements(double input,
                                         double didt,
                                         const stateData& stateDataRef,
                                         matrixData<double>& jacobian,
                                         index_t argLoc,
                                         const solverMode& sMode)
{
    auto locationData = offsets.getLocations(stateDataRef, sMode, this);
    if (opFlags[differential_input]) {
    } else {
        if (hasAlgebraic(sMode)) {
            jacobian.assign(locationData.algOffset + limiter_alg,
                            locationData.algOffset + limiter_alg,
                            -1);

            jacobian.assignCheckCol(locationData.algOffset + limiter_alg, argLoc, K * mT2 / mT1);
            if (limiter_alg > 0) {
                Block::blockJacobianElements(input, didt, stateDataRef, jacobian, argLoc, sMode);
            }
            if (hasDifferential(sMode)) {
                jacobian.assign(locationData.algOffset + limiter_alg, locationData.diffOffset, 1);
            }
        }

        if (hasDifferential(sMode)) {
            jacobian.assignCheckCol(locationData.diffOffset, argLoc, K / mT1);
            if (hasAlgebraic(sMode)) {
                jacobian.assign(
                    locationData.diffOffset, locationData.algOffset + limiter_alg, -1 / mT1);
            }
            jacobian.assign(locationData.diffOffset, locationData.diffOffset, -stateDataRef.cj);
        }
    }
}

double controlBlock::step(coreTime time, double input)
{
    const double timeDelta = time - prevTime;
    double out;
    const double inputWithBias = input + bias;
    double intermediateValue;
    double outputValue;
    if (timeDelta >= fabs(5.0 * mT1)) {
        m_state[limiter_alg + limiter_diff + 1] = K * (1.0 - (mT2 / mT1)) * inputWithBias;
    } else {
        const double timeStep = 0.05 * mT1;
        double currentTime = prevTime + timeStep;
        double currentInput = prevInput;
        double previousIntermediateInput = prevInput;
        intermediateValue = m_state[limiter_alg + limiter_diff + 1];
        outputValue = m_state[limiter_alg + limiter_diff];
        while (currentTime < time) {
            currentInput =
                currentInput + (((inputWithBias - prevInput) / timeDelta) * timeStep);
            intermediateValue = intermediateValue +
                ((1.0 / mT1) *
                 (((K * (previousIntermediateInput + currentInput)) / 2.0) - outputValue) *
                 timeStep);
            outputValue = intermediateValue + ((K * mT2 / mT1) * inputWithBias);
            currentTime += timeStep;
            previousIntermediateInput = currentInput;
        }
        m_state[limiter_alg + limiter_diff + 1] = intermediateValue +
            ((1.0 / mT1) *
             (((K * (previousIntermediateInput + inputWithBias)) / 2.0) - outputValue) *
             (time - currentTime + timeStep));
    }
    m_state[limiter_alg + limiter_diff] =
        m_state[limiter_alg + limiter_diff + 1] + ((K * mT2 / mT1) * inputWithBias);

    prevInput = inputWithBias;
    if (opFlags[has_limits]) {
        out = Block::step(time, inputWithBias);
    } else {
        out = m_state[0];
        prevTime = time;
        m_output = out;
    }
    return out;
}

index_t controlBlock::findIndex(std::string_view field, const solverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "m1") {
        ret = offsets.getDiffOffset(sMode);
    } else {
        ret = Block::findIndex(field, sMode);
    }
    return ret;
}

// set parameters
void controlBlock::set(std::string_view param, std::string_view val)
{
    Block::set(param, val);
}
void controlBlock::set(std::string_view param, double val, units::unit unitType)
{
    // param   = gridDynSimulation::toLower(param);

    if ((param == "t1") || (param == "t")) {
        mT1 = val;
    } else if (param == "t2") {
        mT2 = val;
    } else {
        Block::set(param, val, unitType);
    }
}

stringVec controlBlock::localStateNames() const
{
    stringVec out(stateSize(cLocalSolverMode));
    int loc = 0;
    if (opFlags[use_block_limits]) {
        out[loc++] = "limiter_out";
    }
    if (opFlags[use_ramp_limits]) {
        out[loc++] = "ramp_limiter_out";
    }
    out[loc++] = "output";
    out[loc] = "intermediate";
    return out;
}
}  // namespace griddyn::blocks
