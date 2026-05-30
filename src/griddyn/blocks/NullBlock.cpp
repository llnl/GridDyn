/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "NullBlock.h"

#include "core/CoreObjectTemplates.hpp"
#include <string>
#include <vector>

namespace griddyn::blocks {
NullBlock::NullBlock(const std::string& objName): GridBlock(objName)
{
    opFlags[useDirect] = true;
    opFlags[no_powerflow_operations] = true;
    opFlags[no_dynamics] = true;
}

CoreObject* NullBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<NullBlock, GridSubModel>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    return nobj;
}

void NullBlock::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    auto& lcinfo = offsets.local();
    lcinfo.reset();
    offsets.unload();  // unload all the offsets

    lcinfo.local.jacSize = 0;
    lcinfo.local.diffSize = 0;
    lcinfo.local.algSize = 0;

    if (opFlags[differentialInput]) {
        m_inputSize = 2;
    }
}

// initial conditions
void NullBlock::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        assert(!inputs.empty());
        prevInput = inputs[0];
    } else {
        prevInput = desiredOutput[0];
    }
    fieldSet.resize(1);
    fieldSet[0] = prevInput;
}

void NullBlock::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    step(time, inputs[0]);
}

static IOdata gNullVec;

double NullBlock::step(CoreTime time, double input)
{
    prevTime = time;
    return input;
}

double NullBlock::getBlockOutput(const StateData& stateDataValue,
                                 const SolverMode& solverModeValue) const
{
    auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
    return opFlags[differential_output] ? *locations.diffStateLoc : *locations.algStateLoc;
}

double NullBlock::getBlockOutput() const
{
    auto offset = opFlags[differential_output] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
    return m_state[offset];
}

double NullBlock::getBlockDoutDt(const StateData& stateDataValue,
                                 const SolverMode& solverModeValue) const
{
    if (opFlags[differential_output]) {
        auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
        return *locations.dstateLoc;
    }
    return 0.0;
}

double NullBlock::getBlockDoutDt() const
{
    return 0.0;
}

void NullBlock::blockResidual(double /*input*/,
                              double /*didt*/,
                              const StateData& /*sD*/,
                              double /*resid*/[],
                              const SolverMode& /*sMode*/)
{
}

void NullBlock::blockAlgebraicUpdate(double /*input*/,
                                     const StateData& /*sD*/,
                                     double /*update*/[],
                                     const SolverMode& /*sMode*/)
{
}

void NullBlock::blockDerivative(double /*input*/,
                                double /*didt*/,
                                const StateData& /*sD*/,
                                double /*deriv*/[],
                                const SolverMode& /*sMode*/)
{
}

void NullBlock::blockJacobianElements(double /*input*/,
                                      double /*didt*/,
                                      const StateData& /*sD*/,
                                      MatrixData<double>& /*md*/,
                                      index_t /*argLoc*/,
                                      const SolverMode& /*sMode*/)
{
}

void NullBlock::rootTest(const IOdata& /*inputs*/,
                         const StateData& /*sD*/,
                         double /*roots*/[],
                         const SolverMode& /*sMode*/)
{
}

ChangeCode NullBlock::rootCheck(const IOdata& /*inputs*/,
                                const StateData& /*sD*/,
                                const SolverMode& /*sMode*/,
                                CheckLevel /*level*/)
{
    return ChangeCode::NO_CHANGE;
}

void NullBlock::rootTrigger(CoreTime /*time*/,
                            const IOdata& /*inputs*/,
                            const std::vector<int>& /*rootMask*/,
                            const SolverMode& /*sMode*/)
{
}

void NullBlock::setFlag(std::string_view flag, bool val)
{
    if (flag == "differential_input") {
        opFlags[differentialInput] = val;
        opFlags[differential_output] = val;
    } else {
        GridSubModel::setFlag(flag, val);
    }
}

// set parameters
void NullBlock::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}
void NullBlock::set(std::string_view param, double val, units::unit unitType)
{
    GridSubModel::set(param, val, unitType);
}

double NullBlock::get(std::string_view param, units::unit unitType) const
{
    return GridSubModel::get(param, unitType);
}

}  // namespace griddyn::blocks
