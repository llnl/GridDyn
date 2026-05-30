/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "TransferFunctionBlock.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <string>
#include <utility>
#include <vector>

namespace griddyn::blocks {
TransferFunctionBlock::TransferFunctionBlock(const std::string& objName):
    GridBlock(objName), a(2, 1), b(2, 0)
{
    b[0] = 1;
    opFlags.set(useState);
}

TransferFunctionBlock::TransferFunctionBlock(int order): a(order + 1, 1), b(order + 1, 0)
{
    if (a.empty()) {
        a.push_back(1.0);
        b.assign(1, 1.0);
    } else {
        b[0] = 1;
    }
    opFlags.set(useState);
}

TransferFunctionBlock::TransferFunctionBlock(std::vector<double> acoef):
    a(std::move(acoef)), b(a.size(), 0)
{
    if (a.empty()) {
        a.push_back(1.0);
        b.assign(1, 1.0);
    } else {
        b[0] = 1;
    }
    opFlags.set(useState);
}

TransferFunctionBlock::TransferFunctionBlock(std::vector<double> acoef, std::vector<double> bcoef):
    a(std::move(acoef)), b(std::move(bcoef))
{
    if (a.empty()) {
        a.push_back(1.0);
    }
    b.resize(a.size(), 0);
    opFlags.set(useState);
}

CoreObject* TransferFunctionBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<TransferFunctionBlock, GridBlock>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->a = a;
    nobj->b = b;
    return nobj;
}
// set up the number of states
void TransferFunctionBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (b.back() == 0) {
        opFlags[differential_output] = true;
        extraOutputState = false;
    } else {
        extraOutputState = true;
    }
    GridBlock::dynObjectInitializeA(time0, flags);
    offsets.local().local.jacSize += static_cast<count_t>((3 * (a.size() - 2)) + 1);
    offsets.local().local.diffSize += static_cast<count_t>(a.size()) - 2;
    if (extraOutputState) {
        offsets.local().local.diffSize += 1;
        offsets.local().local.jacSize += 3;
    }
}
// initial conditions
void TransferFunctionBlock::dynObjectInitializeB(const IOdata& inputs,
                                                 const IOdata& desiredOutput,
                                                 IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        //    m_state[2] = (1.0 - m_T2 / m_T1) * (inputs[0] + bias);
        m_state[1] = (inputs[0] + bias);
        m_state[0] = m_state[1] * K;
        if (opFlags[hasLimits]) {
            GridBlock::rootCheck(inputs,
                                 emptyStateData,
                                 cLocalSolverMode,
                                 CheckLevel::reversable_only);
            m_state[0] = gmlc::utilities::valLimit(m_state[0], Omin, Omax);
        }
        fieldSet[0] = m_state[0];
        prevInput = inputs[0] + bias;
    } else {
        m_state[0] = desiredOutput[0];
        //    m_state[1] = (1.0 - m_T2 / m_T1) * desiredOutput[0] / K;
        fieldSet[0] = desiredOutput[0] - bias;
        prevInput = desiredOutput[0] / K;
    }
}

// residual
void TransferFunctionBlock::blockResidual(double input,
                                          double didt,
                                          const StateData& stateDataValue,
                                          double resid[],
                                          const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateDataValue, resid, sMode, this);
    if (extraOutputState) {
    } else {
        for (size_t kk = 0; kk < a.size() - 1; ++kk) {
            loc.destLoc[limiter_alg + kk] =
                (-a[kk] * loc.diffStateLoc[kk]) + loc.diffStateLoc[kk + 1] + b[kk];
        }
    }

    // Loc.destLoc[limiter_alg] = Loc.diffStateLoc[limiter_diff] + m_T2 / m_T1 *
    // (input + bias) - Loc.algStateLoc[limiter_alg];
    GridBlock::blockResidual(input, didt, stateDataValue, resid, sMode);
}

void TransferFunctionBlock::blockDerivative(double input,
                                            double didt,
                                            const StateData& stateDataValue,
                                            double deriv[],
                                            const SolverMode& sMode)
{
    //  auto offset = offsets.getDiffOffset (sMode);
    // auto Aoffset = offsets.getAlgOffset (sMode);
    // deriv[offset + limiter_diff] = K*(input + bias - sD.state[Aoffset +
    // limiter_alg]) / m_T1;
    if (opFlags[useRampLimits]) {
        GridBlock::blockDerivative(input, didt, stateDataValue, deriv, sMode);
    }
}

void TransferFunctionBlock::blockJacobianElements(double input,
                                                  double didt,
                                                  const StateData& stateDataValue,
                                                  matrixData<double>& matrixDataValue,
                                                  index_t argLoc,
                                                  const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateDataValue, sMode, this);
    matrixDataValue.assign(loc.algOffset + 1, loc.algOffset + 1, -1);

    // md.assignCheck(Loc.algOffset + 1, argLoc, m_T2 / m_T1);

    GridBlock::blockJacobianElements(input, didt, stateDataValue, matrixDataValue, argLoc, sMode);
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    matrixDataValue.assign(loc.algOffset + 1, loc.diffOffset, 1);
    // md.assign(arrayIndex, RowIndex, ColIndex, value)

    // md.assignCheck(Loc.diffOffset, argLoc, 1 / m_T1);
    //    md.assign(Loc.diffOffset, Loc.algOffset + 1, -1 / m_T1);
    matrixDataValue.assign(loc.diffOffset, loc.diffOffset, -stateDataValue.cj);
}

double TransferFunctionBlock::step(coreTime time, double inputA)
{
    const double timeDelta = time - prevTime;
    double out;
    const double input = inputA + bias;
    // double ival, ival2;
    if (timeDelta >= fabs(5.0)) {
        m_state[2] = input;
    } else if (timeDelta <= fabs(0.05)) {
        // m_state[2] = m_state[2] + 1.0 / m_T1 * ((input + prevInput) / 2.0 -
        // m_state[1]) * dt;
    } else {
        const double timeStep = 0.05;
        double currentTime = prevTime + timeStep;
        double intermediateInput = prevInput;
        // double pin = prevInput;
        //   ival = m_state[2];
        //    ival2 = m_state[1];
        while (currentTime < time) {
            intermediateInput = intermediateInput + (((input - prevInput) / timeDelta) * timeStep);
            // ival = ival + 1.0 / m_T1 * ((pin + in) / 2.0 - ival2) * tstep;
            //    ival2 = ival + m_T2 / m_T1 * (input);
            currentTime += timeStep;
            //  pin = in;
        }
        // m_state[2] = ival + 1.0 / m_T1 * ((pin + input) / 2.0 - ival2) * (time -
        // ct + tstep);
    }
    // m_state[1] = m_state[2] + m_T2 / m_T1 * (input);

    prevInput = input;
    if (opFlags[hasLimits]) {
        out = GridBlock::step(time, input);
    } else {
        out = K * m_state[1];
        m_state[0] = out;
        prevTime = time;
        m_output = out;
    }
    return out;
}

index_t TransferFunctionBlock::findIndex(std::string_view field, const SolverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "m1") {
        ret = offsets.getDiffOffset(sMode);
    } else {
        ret = GridBlock::findIndex(field, sMode);
    }
    return ret;
}

// set parameters
void TransferFunctionBlock::set(std::string_view param, std::string_view val)
{
    if (param == "a") {
        a = gmlc::utilities::str2vector<double>(std::string{val}, 0);
    } else if (param == "b") {
        b = gmlc::utilities::str2vector<double>(std::string{val}, 0);
    } else {
        GridBlock::set(param, val);
    }
}

void TransferFunctionBlock::set(std::string_view param, double val, units::unit unitType)
{
    // param   = GridDynSimulation::toLower(param);
    std::string pstr;
    const int num = gmlc::utilities::stringOps::trailingStringInt(param, pstr, -1);
    if (pstr.length() == 1) {
        switch (pstr[0]) {
            case '#':
                break;
            case 'a':
            case 't':
                if (num >= 0) {
                    const auto index = static_cast<size_t>(num);
                    if (index >= a.size()) {
                        a.resize(index + 1, 0);
                        b.resize(index + 1, 0);
                    }
                    a[index] = val;
                } else {
                    throw(UnrecognizedParameter(param));
                }
                break;
            case 'b':
                if (num >= 0) {
                    const auto index = static_cast<size_t>(num);
                    if (index >= a.size()) {
                        a.resize(index + 1, 0);
                        b.resize(index + 1, 0);
                    }
                    b[index] = val;
                } else {
                    throw(UnrecognizedParameter(param));
                }
                break;
            case 'k':
                K = val;
                break;
            default:
                throw(UnrecognizedParameter(param));
        }
    }

    if (param.empty() || param[0] == '#') {
        // m_T1 = val;
    } else {
        GridBlock::set(param, val, unitType);
    }
}

static stringVec gStateNames{"output", "Intermediate1", "intermediate2"};

stringVec TransferFunctionBlock::localStateNames() const
{
    return gStateNames;
}
}  // namespace griddyn::blocks
