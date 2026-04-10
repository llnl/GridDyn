/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "controlBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <string>

namespace griddyn {
namespace blocks {
    controlBlock::controlBlock(const std::string& objName): Block(objName)
    {
        opFlags.set(use_state);
    }
    controlBlock::controlBlock(double t1, const std::string& objName): Block(objName), m_T1(t1)
    {
        opFlags.set(use_state);
    }
    controlBlock::controlBlock(double t1, double t2, const std::string& objName):
        Block(objName), m_T1(t1), m_T2(t2)
    {
        opFlags.set(use_state);
    }

    coreObject* controlBlock::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<controlBlock, Block>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->m_T2 = m_T2;
        nobj->m_T1 = m_T1;
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
            m_state[limiter_alg + 1] = K * (1.0 - m_T2 / m_T1) * (inputs[0] + bias);
            m_state[limiter_alg] = K * (inputs[0] + bias);

            fieldSet[0] = m_state[0];
            prevInput = inputs[0] + bias;
        } else {
            m_state[limiter_alg] = desiredOutput[0];
            m_state[limiter_alg + 1] = (1.0 - m_T2 / m_T1) * desiredOutput[0] / K;
            fieldSet[0] = desiredOutput[0] / K - bias;
            prevInput = desiredOutput[0] / K;
        }
    }

    void controlBlock::blockAlgebraicUpdate(double input,
                                            const stateData& sD,
                                            double update[],
                                            const solverMode& sMode)
    {
        if (!opFlags[differential_input]) {
            auto Loc = offsets.getLocations(sD, update, sMode, this);

            Loc.destLoc[limiter_alg] = Loc.diffStateLoc[0] + m_T2 / m_T1 * (input + bias) * K;
            if (limiter_alg > 0) {
                Block::blockAlgebraicUpdate(input, sD, update, sMode);
            }
        }
    }

    void controlBlock::blockDerivative(double input,
                                       double didt,
                                       const stateData& sD,
                                       double deriv[],
                                       const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, deriv, sMode, this);
        if (opFlags[differential_input]) {
            Loc.destDiffLoc[limiter_diff] =
                Loc.dstateLoc[limiter_diff + 1] + m_T2 / m_T1 * didt * K;
            Loc.destDiffLoc[limiter_diff + 1] =
                (K * (input + bias) - Loc.diffStateLoc[limiter_diff]) / m_T1;
            if (limiter_diff > 0) {
                Block::blockDerivative(input, didt, sD, deriv, sMode);
            }
        } else {
            Loc.destDiffLoc[0] = (K * (input + bias) - Loc.algStateLoc[limiter_alg]) / m_T1;
        }
    }

    void controlBlock::blockJacobianElements(double input,
                                             double didt,
                                             const stateData& sD,
                                             matrixData<double>& md,
                                             index_t argLoc,
                                             const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, sMode, this);
        if (opFlags[differential_input]) {
        } else {
            if (hasAlgebraic(sMode)) {
                md.assign(Loc.algOffset + limiter_alg, Loc.algOffset + limiter_alg, -1);

                md.assignCheckCol(Loc.algOffset + limiter_alg, argLoc, K * m_T2 / m_T1);
                if (limiter_alg > 0) {
                    Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
                }
                if (hasDifferential(sMode)) {
                    md.assign(Loc.algOffset + limiter_alg, Loc.diffOffset, 1);
                }
            }

            if (hasDifferential(sMode)) {
                md.assignCheckCol(Loc.diffOffset, argLoc, K / m_T1);
                if (hasAlgebraic(sMode)) {
                    md.assign(Loc.diffOffset, Loc.algOffset + limiter_alg, -1 / m_T1);
                }
                md.assign(Loc.diffOffset, Loc.diffOffset, -sD.cj);
            }
        }
    }

    double controlBlock::step(coreTime time, double input)
    {
        double dt = time - prevTime;
        double out;
        double inputB = input + bias;
        double ival, ival2;
        if (dt >= fabs(5.0 * m_T1)) {
            m_state[limiter_alg + limiter_diff + 1] = K * (1.0 - m_T2 / m_T1) * (inputB);
        } else {
            double tstep = 0.05 * m_T1;
            double ct = prevTime + tstep;
            double in = prevInput;
            double pin = prevInput;
            ival = m_state[limiter_alg + limiter_diff + 1];
            ival2 = m_state[limiter_alg + limiter_diff];
            while (ct < time) {
                in = in + (inputB - prevInput) / dt * tstep;
                ival = ival + 1.0 / m_T1 * (K * (pin + in) / 2.0 - ival2) * tstep;
                ival2 = ival + K * m_T2 / m_T1 * (inputB);
                ct += tstep;
                pin = in;
            }
            m_state[limiter_alg + limiter_diff + 1] =
                ival + 1.0 / m_T1 * (K * (pin + inputB) / 2.0 - ival2) * (time - ct + tstep);
        }
        m_state[limiter_alg + limiter_diff] =
            m_state[limiter_alg + limiter_diff + 1] + K * m_T2 / m_T1 * (inputB);

        prevInput = inputB;
        if (opFlags[has_limits]) {
            out = Block::step(time, inputB);
        } else {
            out = m_state[0];
            prevTime = time;
            m_output = out;
        }
        return out;
    }

    index_t controlBlock::findIndex(const std::string& field, const solverMode& sMode) const
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
    void controlBlock::set(const std::string& param, const std::string& val)
    {
        Block::set(param, val);
    }
    void controlBlock::set(const std::string& param, double val, units::unit unitType)
    {
        // param   = gridDynSimulation::toLower(param);

        if ((param == "t1") || (param == "t")) {
            m_T1 = val;
        } else if (param == "t2") {
            m_T2 = val;
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
}  // namespace blocks
}  // namespace griddyn
