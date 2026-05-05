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

delayBlock::delayBlock(double t1, const std::string& objName): Block(objName), m_T1(t1)
{
    if (std::abs(m_T1) < kMin_Res) {
        opFlags.set(simplified);
    } else {
        opFlags.set(differential_output);
        opFlags.set(use_state);
    }
}
delayBlock::delayBlock(double t1, double gain, const std::string& objName):
    Block(gain, objName), m_T1(t1)
{
    if (std::abs(m_T1) < kMin_Res) {
        opFlags.set(simplified);
    } else {
        opFlags.set(differential_output);
        opFlags.set(use_state);
    }
}

coreObject* delayBlock::clone(coreObject* obj) const
{
    auto nobj = cloneBase<delayBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->m_T1 = m_T1;

    return nobj;
}

void delayBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if ((m_T1 < kMin_Res) || (opFlags[simplified])) {
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
    double dt = time - prevTime;

    double input = (inputA + bias);
    index_t loc = limiter_diff;
    if (dt >= fabs(5.0 * m_T1)) {
        m_state[loc] = K * input;
    } else if (dt <= std::abs(0.05 * m_T1)) {
        m_state[loc] =
            m_state[loc] + 1.0 / m_T1 * (K * (input + prevInput) / 2.0 - m_state[loc]) * dt;
    } else {
        double tstep = 0.05 * m_T1;
        double ct = prevTime + tstep;
        double in = prevInput;
        double pin = prevInput;
        double ival = m_state[loc];
        while (ct < time) {
            in = in + (input - prevInput) / dt * tstep;
            ival = ival + 1.0 / m_T1 * (K * (pin + in) / 2.0 - ival) * tstep;
            ct += tstep;
            pin = in;
        }
        m_state[loc] = ival + 1.0 / m_T1 * (K * (pin + input) / 2.0 - ival) * (time - ct + tstep);
    }
    prevInput = input;
    double out;
    if (loc > 0) {
        out = Block::step(time, input);
    } else {
        out = m_state[loc];
        prevTime = time;
        m_output = out;
    }
    return out;
}

void delayBlock::blockDerivative(double input,
                                 double didt,
                                 const stateData& sD,
                                 double deriv[],
                                 const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

    deriv[offset] = (K * (input + bias) - sD.state[offset]) / m_T1;
    if (limiter_diff > 0) {
        Block::blockDerivative(input, didt, sD, deriv, sMode);
    }
}

void delayBlock::blockJacobianElements(double input,
                                       double didt,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       index_t argLoc,
                                       const solverMode& sMode)
{
    if ((isAlgebraicOnly(sMode)) || (opFlags[simplified])) {
        Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        return;
    }
    auto offset = offsets.getDiffOffset(sMode) + limiter_diff;
    md.assignCheck(offset, argLoc, K / m_T1);
    md.assign(offset, offset, -1.0 / m_T1 - sD.cj);
    Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
}

// set parameters
void delayBlock::set(std::string_view param, std::string_view val)
{
    return coreObject::set(param, val);
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
        m_T1 = val;
    } else {
        Block::set(param, val, unitType);
    }
}
}  // namespace griddyn::blocks
