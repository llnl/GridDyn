/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pidBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
namespace griddyn::blocks {
pidBlock::pidBlock(const std::string& objName): Block(objName), no_D(extra_bool)
{
    opFlags.set(use_state);
    opFlags.set(differential_output);
    no_D = true;
}

pidBlock::pidBlock(double P, double I, double D, const std::string& objName):
    Block(objName), m_P(P), m_I(I), m_D(D), no_D(extra_bool)
{
    opFlags.set(use_state);
    opFlags.set(differential_output);
    no_D = (D == 0.0);
}

CoreObject* pidBlock::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<pidBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->m_P = m_P;
    nobj->m_I = m_I;
    nobj->m_D = m_D;
    nobj->m_T1 = m_T1;
    nobj->iv = iv;
    nobj->no_D = no_D;
    nobj->m_Td = m_Td;
    return nobj;
}

void pidBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    Block::dynObjectInitializeA(time0, flags);
    offsets.local().local.diffSize += 2;
    offsets.local().local.jacSize += 8;
}

// initial conditions
/*in local layout algebraic states come first then differential
0 is PID output
1 is derivative
[limiter_diff] states for the limiters
[limiter_diff] filtered PID output--this is what goes into the limiters
[limiter_diff+1] is the derivative filter
[limiter_diff+2] is the integral calculation
*/
void pidBlock::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    double in = (inputs.empty()) ? 0 : inputs[0] + bias;
    Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (desiredOutput.empty()) {
        m_state[limiter_diff + 2] = iv;
        m_dstate_dt[limiter_diff + 2] = in * m_I;  // integral
        m_state[limiter_diff + 1] = in;  // derivative uses a filter function
        m_state[limiter_diff] =
            K * (m_P * in + m_state[limiter_diff + 2]);  // differential should be 0
        // m_state[1] should be 0

        fieldSet[0] = m_state[0];
    } else {
        m_dstate_dt[limiter_diff + 2] = m_I * in;  // rate of change of integral
        // value
        m_state[limiter_diff + 1] = in;  // derivative uses a filter function
        m_state[limiter_diff] = desiredOutput[0];
        // m_state[1] should be 0
        if (m_I != 0) {
            m_state[limiter_diff + 2] =
                (m_state[limiter_diff] / K - m_P * (in));  // note: differential component assumed 0
        } else if (in != 0.0) {
            m_state[limiter_diff + 2] = 0;
            bias += m_state[limiter_diff] / K / m_P - in;
            in = inputs[0] + bias;
            m_dstate_dt[limiter_diff + 2] = m_I * in;  // integral
            m_state[limiter_diff + 2] = in;  // derivative uses a filter function
        } else {
            m_state[limiter_diff + 1] = 0;
        }
        fieldSet[0] = m_state[0];
    }
    prevInput = in + bias;
}

void pidBlock::blockDerivative(double input,
                               double didt,
                               const stateData& sD,
                               double deriv[],
                               const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    Loc.destDiffLoc[limiter_diff + 2] = m_I * (input + bias);
    Loc.destDiffLoc[limiter_diff + 1] =
        (no_D) ? 0 : (m_D * (input + bias) - Loc.diffStateLoc[limiter_diff + 1]) / m_T1;

    Loc.destDiffLoc[limiter_diff] = (K *
                                         (m_P * (input + bias) + Loc.dstateLoc[limiter_diff + 1] +
                                          Loc.diffStateLoc[limiter_diff + 2]) -
                                     Loc.diffStateLoc[limiter_diff]) /
        m_Td;
    if (limiter_diff > 0) {
        Block::blockDerivative(input, didt, sD, deriv, sMode);
    }
}

void pidBlock::blockJacobianElements(double input,
                                     double didt,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     index_t argLoc,
                                     const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, nullptr, sMode, this);
    // adjust the offset to account for the limiter states;
    Loc.diffOffset += limiter_diff;
    if (hasDifferential(sMode)) {
        //  Loc.destDiffLoc[limiter_diff] = (K*(m_P*(input + bias) +
        //  Loc.dstateLoc[limiter_diff + 1] + Loc.diffStateLoc[limiter_diff + 2]) -
        //  Loc.diffStateLoc[limiter_diff]) / m_Td;
        if (opFlags[has_limits]) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }

        md.assign(Loc.diffOffset, Loc.diffOffset, -1.0 / m_Td - sD.cj);
        md.assign(Loc.diffOffset, Loc.diffOffset + 1, K * sD.cj / m_Td);

        md.assign(Loc.diffOffset, Loc.diffOffset + 2, K / m_Td);
        md.assignCheckCol(Loc.diffOffset, argLoc, K * m_P / m_Td);
        if (no_D) {
            md.assign(Loc.diffOffset + 1, Loc.diffOffset + 1, -sD.cj);
        } else {
            md.assignCheckCol(Loc.diffOffset + 1, argLoc, m_D / m_T1);
            md.assign(Loc.diffOffset + 1, Loc.diffOffset + 1, -1.0 / m_T1 - sD.cj);
        }

        md.assignCheckCol(Loc.diffOffset + 2, argLoc, m_I);
        md.assign(Loc.diffOffset + 2, Loc.diffOffset + 2, -sD.cj);
    }
}

double pidBlock::step(coreTime time, double inputA)
{
    double dt = time - prevTime;
    double input = inputA + bias;
    // integral state

    // derivative state
    if (dt >= fabs(5.0 * std::max(m_T1, m_Td))) {
        m_state[limiter_diff + 2] =
            m_state[limiter_diff + 2] + m_I * (input + prevInput) / 2.0 * dt;
        m_state[limiter_diff + 1] = input;
        m_state[limiter_diff] = K * (m_P * input + m_state[limiter_diff + 2]);
    } else {
        double tstep = 0.05 * std::min(m_T1, m_Td);
        double ct = prevTime + tstep;
        double in = prevInput;
        double pin = prevInput;
        double ival_int = m_state[limiter_diff + 2];
        double ival_der = m_state[limiter_diff + 1];
        double ival_out = m_state[limiter_diff];
        double didt = (input - prevInput) / dt;
        double der;
        while (ct < time) {
            in = in + didt * tstep;
            ival_int += m_I * (in + pin) / 2 * tstep;
            der = (no_D) ? 0 : 1.0 / m_T1 * (m_D * (pin + in) / 2.0 - ival_der);
            ival_der += der * tstep;
            ival_out += (K * (m_P * in + der + ival_int) - ival_out) / m_Td * tstep;
            ct += tstep;
            pin = in;
        }
        m_state[limiter_diff + 2] = ival_int + m_I * (pin + input) / 2.0 * (time - ct + tstep);
        der = (no_D) ? 0 : 1.0 / m_T1 * (m_D * (pin + input) / 2.0 - ival_der);
        m_state[limiter_diff + 1] = ival_der + der * (time - ct + tstep);
        m_state[limiter_diff] = ival_out +
            (K * (m_P * input + der + m_state[limiter_diff + 2]) - ival_out) / m_Td *
                (time - ct + tstep);
    }
    prevInput = input;

    if (opFlags[has_limits]) {
        Block::step(time, input);
    } else {
        prevTime = time;
        m_output = m_state[0];
    }
    return m_output;
}

index_t pidBlock::findIndex(std::string_view field, const solverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "integral") {
        ret = offsets.getDiffOffset(sMode);
        ret = (ret != kNullLocation) ? ret + 1 : ret;
    } else if (field == "derivative") {
        ret = offsets.getDiffOffset(sMode);
    } else {
        ret = Block::findIndex(field, sMode);
    }
    return ret;
}

// set parameters
void pidBlock::set(std::string_view param, std::string_view val)
{
    Block::set(param, val);
}
void pidBlock::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "p") || (param == "proportional")) {
        m_P = val;
    } else if ((param == "i") || (param == "integral")) {
        m_I = val;
    } else if ((param == "d") || (param == "derivative")) {
        m_D = val;
        no_D = (m_D == 0.0);
    } else if ((param == "t") || (param == "t1")) {
        m_T1 = val;
    } else if ((param == "iv") || (param == "initial_value")) {
        iv = val;
    } else {
        Block::set(param, val, unitType);
    }
}

stringVec pidBlock::localStateNames() const
{
    stringVec out = Block::localStateNames();

    out.emplace_back("deriv_delay");
    out.emplace_back("integral");
    return out;
}
}  // namespace griddyn::blocks
