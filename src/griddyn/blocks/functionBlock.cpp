/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "functionBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/functionInterpreter.h"
#include "utilities/matrixData.hpp"
#include <string>

namespace griddyn::blocks {
    functionBlock::functionBlock(): Block("functionBlock_#")
    {
        offsets.local().local.algSize = 2;
        offsets.local().local.diffSize = 0;
        opFlags.set(use_state);
        offsets.local().local.jacSize = 3;
    }

    functionBlock::functionBlock(const std::string& functionName): Block("functionBlock_#")
    {
        offsets.local().local.algSize = 2;
        offsets.local().local.diffSize = 0;
        opFlags.set(use_state);
        offsets.local().local.jacSize = 3;
        setFunction(functionName);
    }

    coreObject* functionBlock::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<functionBlock, Block>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->fptr = fptr;
        nobj->gain = gain;
        nobj->bias = bias;
        return nobj;
    }

    // initial conditions
    void functionBlock::dynObjectInitializeB(const IOdata& inputs,
                                             const IOdata& desiredOutput,
                                             IOdata& fieldSet)
    {
        if (desiredOutput.empty()) {
            if (opFlags[uses_constantarg]) {
                m_state[limiter_alg] = K * fptr2(gain * (inputs[0] + bias), arg2);
            } else {
                m_state[limiter_alg] = K * fptr(gain * (inputs[0] + bias));
            }
            Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        } else {
            Block::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
        }
    }

    void functionBlock::blockAlgebraicUpdate(double input,
                                             const stateData& sD,
                                             double update[],
                                             const solverMode& sMode)
    {
        auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
        if (opFlags[uses_constantarg]) {
            update[offset] = K * fptr2(gain * (input + bias), arg2);
        } else {
            update[offset] = K * fptr(gain * (input + bias));
        }
        if (limiter_alg > 0) {
            Block::blockAlgebraicUpdate(input, sD, update, sMode);
        }
    }

    void functionBlock::blockJacobianElements(double input,
                                              double didt,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              index_t argLoc,
                                              const solverMode& sMode)
    {
        auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
        // use the md.assign Macro defined in basicDefs
        // md.assign(arrayIndex, RowIndex, ColIndex, value)
        if (opFlags[uses_constantarg]) {
            double temp1 = fptr2(gain * (input + bias), arg2);
            double temp2 = fptr2(gain * (input + 1e-8 + bias), arg2);
            md.assignCheck(offset, argLoc, K * (temp2 - temp1) / 1e-8);
        } else {
            double temp1 = fptr(gain * (input + bias));
            double temp2 = fptr(gain * (input + 1e-8 + bias));
            md.assignCheck(offset, argLoc, K * (temp2 - temp1) / 1e-8);
        }
        md.assign(offset, offset, -1);
        if (limiter_alg > 0) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }
    }

    double functionBlock::step(coreTime time, double input)
    {
        if (opFlags[uses_constantarg]) {
            m_state[limiter_alg] = K * fptr2(gain * (input + bias), arg2);
        } else {
            m_state[limiter_alg] = K * fptr(gain * (input + bias));
        }
        if (limiter_alg > 0) {
            Block::step(time, input);
        }
        m_output = m_state[0];
        prevTime = time;
        return m_state[0];
    }

    // set parameters
    void functionBlock::set(const std::string& param, const std::string& val)
    {
        if ((param == "function") || (param == "func")) {
            auto v2 = gmlc::utilities::convertToLowerCase(val);
            setFunction(v2);
        } else {
            Block::set(param, val);
        }
    }

    void functionBlock::set(const std::string& param, double val, units::unit unitType)
    {
        if (param == "gain") {
            gain = val;
        } else if (param == "arg") {
            arg2 = val;
        } else {
            Block::set(param, val, unitType);
        }
    }

    void functionBlock::setFunction(const std::string& functionName)
    {
        if (isFunctionName(functionName, function_type::arg)) {
            fptr = get1ArgFunction(functionName);
            opFlags.reset(uses_constantarg);
        } else if (isFunctionName(functionName, function_type::arg2)) {
            fptr2 = get2ArgFunction(functionName);
            opFlags.set(uses_constantarg);
        }
    }

    /*
double functionBlock::currentValue(const IOdata &inputs, const stateData &sD,
const solverMode &sMode) const
{
  auto Loc;
  offsets.getLocations(sD, sMode, &Loc, this);
  double val = Loc.algStateLoc[1];
  if (!inputs.empty())
  {
    if (opFlags[uses_constantarg])
    {
      val = fptr2(gain*(inputs[0] + bias), arg2);
    }
    else
    {
      val = fptr(gain*(inputs[0] + bias));
    }
  }
  return Block::currentValue({ val }, sD, sMode);
}

double functionBlock::currentValue() const
{
  return m_state[0];
}
*/
}  // namespace griddyn::blocks
