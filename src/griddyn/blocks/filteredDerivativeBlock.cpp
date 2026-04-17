/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "filteredDerivativeBlock.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactory.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn {
namespace blocks {
    filteredDerivativeBlock::filteredDerivativeBlock(const std::string& objName): Block(objName)
    {
        opFlags.set(use_state);
        opFlags.set(differential_output);
    }

    filteredDerivativeBlock::filteredDerivativeBlock(double t1,
                                                     double t2,
                                                     const std::string& objName):
        Block(objName), m_T1(t1), m_T2(t2)
    {
        opFlags.set(use_state);
        opFlags.set(differential_output);
    }

    coreObject* filteredDerivativeBlock::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<filteredDerivativeBlock, Block>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->m_T1 = m_T1;
        nobj->m_T2 = m_T2;
        return nobj;
    }

    void filteredDerivativeBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        Block::dynObjectInitializeA(time0, flags);
        offsets.local().local.diffSize++;
        offsets.local().local.jacSize += 2;
    }

    void filteredDerivativeBlock::dynObjectInitializeB(const IOdata& inputs,
                                                       const IOdata& desiredOutput,
                                                       IOdata& fieldSet)
    {
        index_t loc = limiter_diff;  // can't have a ramp limiter
        if (desiredOutput.empty()) {
            m_state[loc + 1] = K * (inputs[0] + bias);
            m_state[loc] = 0;
            if (limiter_diff > 0) {
                Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
            }
        } else {
            Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
            m_state[loc] = desiredOutput[0];
            m_dstate_dt[loc + 1] = desiredOutput[0];
            if (std::abs(m_dstate_dt[loc + 1]) < 1e-7) {
                m_state[loc + 1] = K * (inputs[0] + bias);
            } else {
                m_state[loc + 1] = (m_state[loc] - m_dstate_dt[loc + 1] * m_T1);
            }
        }
    }

    double filteredDerivativeBlock::step(coreTime time, double inputA)
    {
        index_t loc = limiter_diff;
        double dt = time - prevTime;

        double input = inputA + bias;
        if ((dt >= fabs(5.0 * m_T1)) && (dt >= fabs(5.0 * m_T2))) {
            m_state[loc + 1] = K * input;
            m_state[loc] = 0;
        } else {
            double tstep = 0.05 * std::min(m_T1, m_T2);
            double ct = prevTime + tstep;
            double in = prevInput;
            double pin = prevInput;
            double ival = m_state[loc + 1];
            double ival2 = m_state[loc];
            double der1;
            while (ct < time) {
                in = in + (input - prevInput) / dt * tstep;
                der1 = K / m_T1 * ((pin + in) / 2.0 - ival);
                ival = ival + der1 * tstep;
                ival2 = ival2 + (der1 - ival2) / m_T2 * tstep;
                ct += tstep;
                pin = in;
            }
            m_state[loc + 1] = ival + K / m_T1 * ((pin + input) / 2.0 - ival) * (time - ct + tstep);
            m_state[loc] = ival2 +
                (K / m_T1 * ((pin + input) / 2.0 - ival) - ival2) / m_T2 * (time - ct + tstep);
        }
        prevInput = input;
        double out;
        if (loc > 0) {
            out = Block::step(time, inputA);
        } else {
            out = m_state[0];
            prevTime = time;
            m_output = out;
        }
        return out;
    }

    void filteredDerivativeBlock::blockDerivative(double input,
                                                  double /*didt*/,
                                                  const stateData& sD,
                                                  double deriv[],
                                                  const solverMode& sMode)
    {
        auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

        deriv[offset + 1] = (K * (input + bias) - sD.state[offset + 1]) / m_T1;
        deriv[offset] = (sD.dstate_dt[offset + 1] - sD.state[offset]) / m_T2;
    }

    void filteredDerivativeBlock::blockJacobianElements(double input,
                                                        double didt,
                                                        const stateData& sD,
                                                        matrixData<double>& md,
                                                        index_t argLoc,
                                                        const solverMode& sMode)
    {
        if (!hasDifferential(sMode)) {
            return;
        }
        auto offset = offsets.getDiffOffset(sMode) + limiter_diff;

        md.assignCheckCol(offset + 1, argLoc, K / m_T1);
        md.assign(offset + 1, offset + 1, -1 / m_T1 - sD.cj);

        md.assign(offset, offset + 1, sD.cj / m_T2);
        md.assign(offset, offset, -1 / m_T2 - sD.cj);

        if (limiter_diff > 0) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }
    }

    // set parameters
    void filteredDerivativeBlock::set(const std::string& param, const std::string& val)
    {
        Block::set(param, val);
    }
    void filteredDerivativeBlock::set(const std::string& param, double val, units::unit unitType)
    {
        if (param == "t1") {
            m_T1 = val;
        } else if (param == "t2") {
            if (std::abs(val) < kMin_Res) {
                throw(invalidParameterValue(param));
            }
            m_T2 = val;
        } else {
            Block::set(param, val, unitType);
        }
    }

    stringVec filteredDerivativeBlock::localStateNames() const
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
}  // namespace blocks
}  // namespace griddyn
