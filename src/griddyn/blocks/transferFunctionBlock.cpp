/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "transferFunctionBlock.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <string>
#include <utility>
#include <vector>

namespace griddyn::blocks {
transferFunctionBlock::transferFunctionBlock(const std::string& objName):
    Block(objName), a(2, 1), b(2, 0)
{
    b[0] = 1;
    opFlags.set(use_state);
}

transferFunctionBlock::transferFunctionBlock(int order): a(order + 1, 1), b(order + 1, 0)
{
    if (a.empty()) {
        a.push_back(1.0);
        b.assign(1, 1.0);
    } else {
        b[0] = 1;
    }
    opFlags.set(use_state);
}

transferFunctionBlock::transferFunctionBlock(std::vector<double> Acoef):
    a(std::move(Acoef)), b(a.size(), 0)
{
    if (a.empty()) {
        a.push_back(1.0);
        b.assign(1, 1.0);
    } else {
        b[0] = 1;
    }
    opFlags.set(use_state);
}

transferFunctionBlock::transferFunctionBlock(std::vector<double> Acoef, std::vector<double> Bcoef):
    a(std::move(Acoef)), b(std::move(Bcoef))
{
    if (a.empty()) {
        a.push_back(1.0);
    }
    b.resize(a.size(), 0);
    opFlags.set(use_state);
}

CoreObject* transferFunctionBlock::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<transferFunctionBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->a = a;
    nobj->b = b;
    return nobj;
}
// set up the number of states
void transferFunctionBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (b.back() == 0) {
        opFlags[differential_output] = true;
        extraOutputState = false;
    } else {
        extraOutputState = true;
    }
    Block::dynObjectInitializeA(time0, flags);
    offsets.local().local.jacSize += static_cast<count_t>(3 * (a.size() - 2) + 1);
    offsets.local().local.diffSize += static_cast<count_t>(a.size()) - 2;
    if (extraOutputState) {
        offsets.local().local.diffSize += 1;
        offsets.local().local.jacSize += 3;
    }
}
// initial conditions
void transferFunctionBlock::dynObjectInitializeB(const IOdata& inputs,
                                                 const IOdata& desiredOutput,
                                                 IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        //    m_state[2] = (1.0 - m_T2 / m_T1) * (inputs[0] + bias);
        m_state[1] = (inputs[0] + bias);
        m_state[0] = m_state[1] * K;
        if (opFlags[has_limits]) {
            Block::rootCheck(inputs,
                             emptyStateData,
                             cLocalSolverMode,
                             check_level_t::reversable_only);
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
void transferFunctionBlock::blockResidual(double input,
                                          double didt,
                                          const stateData& sD,
                                          double resid[],
                                          const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, resid, sMode, this);
    if (extraOutputState) {
    } else {
        for (size_t kk = 0; kk < a.size() - 1; ++kk) {
            Loc.destLoc[limiter_alg + kk] =
                -a[kk] * Loc.diffStateLoc[kk] + Loc.diffStateLoc[kk + 1] + b[kk];
        }
    }

    // Loc.destLoc[limiter_alg] = Loc.diffStateLoc[limiter_diff] + m_T2 / m_T1 *
    // (input + bias) - Loc.algStateLoc[limiter_alg];
    Block::blockResidual(input, didt, sD, resid, sMode);
}

void transferFunctionBlock::blockDerivative(double input,
                                            double didt,
                                            const stateData& sD,
                                            double deriv[],
                                            const solverMode& sMode)
{
    //  auto offset = offsets.getDiffOffset (sMode);
    // auto Aoffset = offsets.getAlgOffset (sMode);
    // deriv[offset + limiter_diff] = K*(input + bias - sD.state[Aoffset +
    // limiter_alg]) / m_T1;
    if (opFlags[use_ramp_limits]) {
        Block::blockDerivative(input, didt, sD, deriv, sMode);
    }
}

void transferFunctionBlock::blockJacobianElements(double input,
                                                  double didt,
                                                  const stateData& sD,
                                                  matrixData<double>& md,
                                                  index_t argLoc,
                                                  const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    md.assign(Loc.algOffset + 1, Loc.algOffset + 1, -1);

    // md.assignCheck(Loc.algOffset + 1, argLoc, m_T2 / m_T1);

    Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    md.assign(Loc.algOffset + 1, Loc.diffOffset, 1);
    // md.assign(arrayIndex, RowIndex, ColIndex, value)

    // md.assignCheck(Loc.diffOffset, argLoc, 1 / m_T1);
    //    md.assign(Loc.diffOffset, Loc.algOffset + 1, -1 / m_T1);
    md.assign(Loc.diffOffset, Loc.diffOffset, -sD.cj);
}

double transferFunctionBlock::step(coreTime time, double inputA)
{
    double dt = time - prevTime;
    double out;
    double input = inputA + bias;
    // double ival, ival2;
    if (dt >= fabs(5.0)) {
        m_state[2] = input;
    } else if (dt <= fabs(0.05)) {
        // m_state[2] = m_state[2] + 1.0 / m_T1 * ((input + prevInput) / 2.0 -
        // m_state[1]) * dt;
    } else {
        double tstep = 0.05;
        double ct = prevTime + tstep;
        double in = prevInput;
        // double pin = prevInput;
        //   ival = m_state[2];
        //    ival2 = m_state[1];
        while (ct < time) {
            in = in + (input - prevInput) / dt * tstep;
            // ival = ival + 1.0 / m_T1 * ((pin + in) / 2.0 - ival2) * tstep;
            //    ival2 = ival + m_T2 / m_T1 * (input);
            ct += tstep;
            //  pin = in;
        }
        // m_state[2] = ival + 1.0 / m_T1 * ((pin + input) / 2.0 - ival2) * (time -
        // ct + tstep);
    }
    // m_state[1] = m_state[2] + m_T2 / m_T1 * (input);

    prevInput = input;
    if (opFlags[has_limits]) {
        out = Block::step(time, input);
    } else {
        out = K * m_state[1];
        m_state[0] = out;
        prevTime = time;
        m_output = out;
    }
    return out;
}

index_t transferFunctionBlock::findIndex(std::string_view field, const solverMode& sMode) const
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
void transferFunctionBlock::set(std::string_view param, std::string_view val)
{
    if (param == "a") {
        a = gmlc::utilities::str2vector<double>(std::string{val}, 0);
    } else if (param == "b") {
        b = gmlc::utilities::str2vector<double>(std::string{val}, 0);
    } else {
        Block::set(param, val);
    }
}

void transferFunctionBlock::set(std::string_view param, double val, units::unit unitType)
{
    // param   = gridDynSimulation::toLower(param);
    std::string pstr;
    int num = gmlc::utilities::stringOps::trailingStringInt(param, pstr, -1);
    if (pstr.length() == 1) {
        switch (pstr[0]) {
            case '#':
                break;
            case 'a':
            case 't':
                if (num >= 0) {
                    if (num > static_cast<int>(a.size())) {
                        a.resize(num + 1, 0);
                        b.resize(num + 1, 0);
                    }
                    a[num] = val;
                } else {
                    throw(unrecognizedParameter(param));
                }
                break;
            case 'b':
                if (num >= 0) {
                    if (num > static_cast<int>(a.size())) {
                        a.resize(num + 1, 0);
                        b.resize(num + 1, 0);
                    }
                    b[num] = val;
                } else {
                    throw(unrecognizedParameter(param));
                }
                break;
            case 'k':
                K = val;
                break;
            default:
                throw(unrecognizedParameter(param));
        }
    }

    if (param.empty() || param[0] == '#') {
        // m_T1 = val;
    } else {
        Block::set(param, val, unitType);
    }
}

static stringVec stNames{"output", "Intermediate1", "intermediate2"};

stringVec transferFunctionBlock::localStateNames() const
{
    return stNames;
}
}  // namespace griddyn::blocks
