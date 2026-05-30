/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "PidBlock.h"

#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
namespace griddyn::blocks {
PidBlock::PidBlock(const std::string& objName): GridBlock(objName), no_D(extra_bool)
{
    opFlags.set(useState);
    opFlags.set(differential_output);
    no_D = true;
}

PidBlock::PidBlock(double proportionalGain,
                   double integralGain,
                   double derivativeGain,
                   const std::string& objName):
    GridBlock(objName), m_P(proportionalGain), m_I(integralGain), m_D(derivativeGain),
    no_D(extra_bool)
{
    opFlags.set(useState);
    opFlags.set(differential_output);
    no_D = (derivativeGain == 0.0);
}

CoreObject* PidBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<PidBlock, GridBlock>(this, obj);
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

void PidBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridBlock::dynObjectInitializeA(time0, flags);
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
void PidBlock::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    double inputValue = (inputs.empty()) ? 0 : inputs[0] + bias;
    GridBlock::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (desiredOutput.empty()) {
        m_state[limiter_diff + 2] = iv;
        m_dstate_dt[limiter_diff + 2] = inputValue * m_I;  // integral
        m_state[limiter_diff + 1] = inputValue;  // derivative uses a filter function
        m_state[limiter_diff] =
            K * ((m_P * inputValue) + m_state[limiter_diff + 2]);  // differential should be 0
        // m_state[1] should be 0

        fieldSet[0] = m_state[0];
    } else {
        m_dstate_dt[limiter_diff + 2] = m_I * inputValue;  // rate of change of integral
        // value
        m_state[limiter_diff + 1] = inputValue;  // derivative uses a filter function
        m_state[limiter_diff] = desiredOutput[0];
        // m_state[1] should be 0
        if (m_I != 0) {
            m_state[limiter_diff + 2] = ((m_state[limiter_diff] / K) - (m_P * inputValue));
        } else if (inputValue != 0.0) {
            m_state[limiter_diff + 2] = 0;
            bias += ((m_state[limiter_diff] / K) / m_P) - inputValue;
            inputValue = inputs[0] + bias;
            m_dstate_dt[limiter_diff + 2] = m_I * inputValue;  // integral
            m_state[limiter_diff + 2] = inputValue;  // derivative uses a filter function
        } else {
            m_state[limiter_diff + 1] = 0;
        }
        fieldSet[0] = m_state[0];
    }
    prevInput = inputValue + bias;
}

void PidBlock::blockDerivative(double input,
                               double didt,
                               const StateData& stateDataValue,
                               double deriv[],
                               const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateDataValue, deriv, sMode, this);
    loc.destDiffLoc[limiter_diff + 2] = m_I * (input + bias);
    loc.destDiffLoc[limiter_diff + 1] =
        (no_D) ? 0 : ((m_D * (input + bias)) - loc.diffStateLoc[limiter_diff + 1]) / m_T1;

    loc.destDiffLoc[limiter_diff] = ((K *
                                      ((m_P * (input + bias)) + loc.dstateLoc[limiter_diff + 1] +
                                       loc.diffStateLoc[limiter_diff + 2])) -
                                     loc.diffStateLoc[limiter_diff]) /
        m_Td;
    if (limiter_diff > 0) {
        GridBlock::blockDerivative(input, didt, stateDataValue, deriv, sMode);
    }
}

void PidBlock::blockJacobianElements(double input,
                                     double didt,
                                     const StateData& stateDataValue,
                                     matrixData<double>& matrixDataValue,
                                     index_t argLoc,
                                     const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateDataValue, nullptr, sMode, this);
    // adjust the offset to account for the limiter states;
    loc.diffOffset += limiter_diff;
    if (hasDifferential(sMode)) {
        //  Loc.destDiffLoc[limiter_diff] = (K*(m_P*(input + bias) +
        //  Loc.dstateLoc[limiter_diff + 1] + Loc.diffStateLoc[limiter_diff + 2]) -
        //  Loc.diffStateLoc[limiter_diff]) / m_Td;
        if (opFlags[hasLimits]) {
            GridBlock::blockJacobianElements(
                input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
        }

        matrixDataValue.assign(loc.diffOffset, loc.diffOffset, ((-1.0 / m_Td) - stateDataValue.cj));
        matrixDataValue.assign(loc.diffOffset, loc.diffOffset + 1, K * stateDataValue.cj / m_Td);

        matrixDataValue.assign(loc.diffOffset, loc.diffOffset + 2, K / m_Td);
        matrixDataValue.assignCheckCol(loc.diffOffset, argLoc, K * m_P / m_Td);
        if (no_D) {
            matrixDataValue.assign(loc.diffOffset + 1, loc.diffOffset + 1, -stateDataValue.cj);
        } else {
            matrixDataValue.assignCheckCol(loc.diffOffset + 1, argLoc, m_D / m_T1);
            matrixDataValue.assign(loc.diffOffset + 1,
                                   loc.diffOffset + 1,
                                   ((-1.0 / m_T1) - stateDataValue.cj));
        }

        matrixDataValue.assignCheckCol(loc.diffOffset + 2, argLoc, m_I);
        matrixDataValue.assign(loc.diffOffset + 2, loc.diffOffset + 2, -stateDataValue.cj);
    }
}

double PidBlock::step(coreTime time, double inputA)
{
    const double timeDelta = time - prevTime;
    const double inputValue = inputA + bias;
    // integral state

    // derivative state
    if (timeDelta >= fabs(5.0 * std::max(m_T1, m_Td))) {
        m_state[limiter_diff + 2] =
            m_state[limiter_diff + 2] + ((m_I * (inputValue + prevInput) / 2.0) * timeDelta);
        m_state[limiter_diff + 1] = inputValue;
        m_state[limiter_diff] = K * ((m_P * inputValue) + m_state[limiter_diff + 2]);
    } else {
        const double timeStep = 0.05 * std::min(m_T1, m_Td);
        double currentTime = prevTime + timeStep;
        double intermediateInput = prevInput;
        double priorInput = prevInput;
        double ivalInt = m_state[limiter_diff + 2];
        double ivalDer = m_state[limiter_diff + 1];
        double ivalOut = m_state[limiter_diff];
        const double inputRate = (inputValue - prevInput) / timeDelta;
        double der;
        while (currentTime < time) {
            intermediateInput = intermediateInput + (inputRate * timeStep);
            ivalInt += (m_I * (intermediateInput + priorInput) / 2) * timeStep;
            der = (no_D) ?
                0 :
                (1.0 / m_T1) * (((m_D * (priorInput + intermediateInput)) / 2.0) - ivalDer);
            ivalDer += der * timeStep;
            ivalOut +=
                ((K * ((m_P * intermediateInput) + der + ivalInt)) - ivalOut) / m_Td * timeStep;
            currentTime += timeStep;
            priorInput = intermediateInput;
        }
        const double remainingTime = time - currentTime + timeStep;
        m_state[limiter_diff + 2] =
            ivalInt + ((m_I * (priorInput + inputValue) / 2.0) * remainingTime);
        der = (no_D) ? 0 : (1.0 / m_T1) * (((m_D * (priorInput + inputValue)) / 2.0) - ivalDer);
        m_state[limiter_diff + 1] = ivalDer + (der * remainingTime);
        m_state[limiter_diff] = ivalOut +
            ((((K * ((m_P * inputValue) + der + m_state[limiter_diff + 2])) - ivalOut) / m_Td) *
             remainingTime);
    }
    prevInput = inputValue;

    if (opFlags[hasLimits]) {
        GridBlock::step(time, inputValue);
    } else {
        prevTime = time;
        m_output = m_state[0];
    }
    return m_output;
}

index_t PidBlock::findIndex(std::string_view field, const SolverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "integral") {
        ret = offsets.getDiffOffset(sMode);
        ret = (ret != kNullLocation) ? ret + 1 : ret;
    } else if (field == "derivative") {
        ret = offsets.getDiffOffset(sMode);
    } else {
        ret = GridBlock::findIndex(field, sMode);
    }
    return ret;
}

// set parameters
void PidBlock::set(std::string_view param, std::string_view val)
{
    GridBlock::set(param, val);
}
void PidBlock::set(std::string_view param, double val, units::unit unitType)
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
        GridBlock::set(param, val, unitType);
    }
}

stringVec PidBlock::localStateNames() const
{
    stringVec out = GridBlock::localStateNames();

    out.emplace_back("deriv_delay");
    out.emplace_back("integral");
    return out;
}
}  // namespace griddyn::blocks
