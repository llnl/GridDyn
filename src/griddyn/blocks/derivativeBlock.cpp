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
derivativeBlock::derivativeBlock(const std::string& objName): Block(objName)
{
    opFlags.set(use_state);
}
derivativeBlock::derivativeBlock(double t1, const std::string& objName): Block(objName), m_T1(t1)
{
    opFlags.set(use_state);
}

coreObject* derivativeBlock::clone(coreObject* obj) const
{
    auto nobj = cloneBase<derivativeBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->m_T1 = m_T1;

    return nobj;
}

void derivativeBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    Block::dynObjectInitializeA(time0, flags);
    offsets.local().local.diffSize++;
    offsets.local().local.jacSize += 2;
}

void derivativeBlock::dynObjectInitializeB(const IOdata& inputs,
                                           const IOdata& desiredOutput,
                                           IOdata& fieldSet)
{
    index_t loc = limiter_alg;  // can't have a ramp limiter
    if (desiredOutput.empty()) {
        m_state[loc + 1] = K * (inputs[0] + bias);
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        m_state[loc] = 0;
    } else {
        Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        m_dstate_dt[loc + 1] = desiredOutput[0];
        if (std::abs(m_dstate_dt[loc + 1]) < 1e-7) {
            m_state[loc + 1] = K * (inputs[0] + bias);
        } else {
            m_state[loc + 1] = (m_state[loc] - m_dstate_dt[loc + 1] * m_T1);
        }
    }
}

double derivativeBlock::step(coreTime time, double inputA)
{
    index_t loc = limiter_alg;
    double dt = time - prevTime;
    double out;
    double input = inputA + bias;
    double ival;
    if (dt >= fabs(5.0 * m_T1)) {
        m_state[loc + 1] = K * input;
        m_state[loc] = 0;
    } else if (dt <= fabs(0.05 * m_T1)) {
        m_state[loc + 1] =
            m_state[loc + 1] + 1.0 / m_T1 * (K * (input + prevInput) / 2.0 - m_state[loc + 1]) * dt;
        m_state[loc] = 1.0 / m_T1 * (K * (input + prevInput) / 2.0 - m_state[loc + 1]);
    } else {
        double tstep = 0.05 * m_T1;
        double ct = prevTime + tstep;
        double in = prevInput;
        double pin = prevInput;
        ival = m_state[loc + 1];
        while (ct < time) {
            in = in + (input - prevInput) / dt * tstep;
            ival = ival + K / m_T1 * ((pin + in) / 2.0 - ival) * tstep;
            ct += tstep;
            pin = in;
        }
        m_state[loc + 1] = ival + K / m_T1 * ((pin + input) / 2.0 - ival) * (time - ct + tstep);
        m_state[loc] = K / m_T1 * ((pin + input) / 2.0 - ival);
    }
    prevInput = input;
    if (loc > 0) {
        out = Block::step(time, inputA);
    } else {
        out = m_state[0];
        prevTime = time;
        m_output = out;
    }
    return out;
}

void derivativeBlock::blockAlgebraicUpdate(double input,
                                           const stateData& sD,
                                           double update[],
                                           const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, update, sMode, this);
    Loc.destLoc[limiter_alg] = Loc.dstateLoc[0];
    //    update[Aoffset + limiter_alg] = sD.state[Aoffset + limiter_alg] -
    // sD.dstate_dt[offset];
    Block::blockAlgebraicUpdate(input, sD, update, sMode);
}

void derivativeBlock::blockDerivative(double input,
                                      double /*didt*/,
                                      const stateData& sD,
                                      double deriv[],
                                      const solverMode& sMode)
{
    auto offset =
        offsets.getDiffOffset(sMode);  // limiter diff must be 0 since the output is algebraic

    deriv[offset] = (K * (input + bias) - sD.state[offset]) / m_T1;
}

void derivativeBlock::blockJacobianElements(double input,
                                            double didt,
                                            const stateData& sD,
                                            matrixData<double>& md,
                                            index_t argLoc,
                                            const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    if (hasDifferential(sMode)) {
        md.assignCheck(offset, argLoc, K / m_T1);
        md.assign(offset, offset, -1.0 / m_T1 - sD.cj);
    } else {
        offset = kNullLocation;
    }
    if (hasAlgebraic(sMode)) {
        auto Aoffset = offsets.getAlgOffset(sMode) + limiter_alg;
        md.assignCheck(Aoffset, offset, sD.cj);
        md.assign(Aoffset, Aoffset, -1);
        if (limiter_alg > 0) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }
    }
}

// set parameters
void derivativeBlock::set(std::string_view param, std::string_view val)
{
    Block::set(param, val);
}
void derivativeBlock::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "t1") || (param == "t")) {
        if (std::abs(val) < kMin_Res) {
            throw(invalidParameterValue(std::string{param}));
        }
        m_T1 = val;
    } else {
        Block::set(param, val, unitType);
    }
}

stringVec derivativeBlock::localStateNames() const
{
    auto bbstates = Block::localStateNames();
    bbstates.emplace_back("deriv");
    bbstates.emplace_back("delayI");
    return bbstates;
}
}  // namespace griddyn::blocks
