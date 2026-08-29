/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LeadLagBlock.h"

#include "ValueLimiter.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>

namespace griddyn::blocks {
LeadLagBlock::LeadLagBlock(const std::string& objName): GridBlock(objName)
{
    opFlags.set(USE_STATE);
}

LeadLagBlock::LeadLagBlock(double lagTime, double leadTime, const std::string& objName):
    GridBlock(objName), section(leadTime, lagTime)
{
    opFlags.set(USE_STATE);
}

LeadLagBlock::LeadLagBlock(double lagTime,
                           double leadTime,
                           double gainValue,
                           const std::string& objName):
    GridBlock(gainValue, objName), section(leadTime, lagTime, gainValue)
{
    opFlags.set(USE_STATE);
}

CoreObject* LeadLagBlock::clone(CoreObject* obj) const
{
    auto* clone = cloneBase<LeadLagBlock, GridBlock>(this, obj);
    if (clone != nullptr) {
        clone->section = section;
    }
    return (clone == nullptr) ? obj : clone;
}

void LeadLagBlock::validateParameters() const
{
    if (!section.isValid()) {
        throw InvalidParameterValue("lead-lag gain or time constants");
    }
}

void LeadLagBlock::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateParameters();
    GridBlock::dynObjectInitializeA(time0, flags);
    // GridBlock supplies the algebraic output.  This block adds the one
    // differential lag state used by the reusable lead-lag realization.
    ++offsets.local().local.diffSize;
    offsets.local().local.jacSize += 4;
}

void LeadLagBlock::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    fieldSet.resize(1);
    const index_t lagState = offsets.local().local.algSize;
    if (desiredOutput.empty()) {
        const double input = inputs[0] + bias;
        m_state[lagState] = input;
        m_state[limiter_alg] = section.output(input, input);
        if (opFlags[USE_BLOCK_LIMITS]) {
            m_state[0] = vLimiter->clampOutput(m_state[limiter_alg]);
        }
        fieldSet[0] = m_state[0];
        prevInput = input;
        return;
    }

    const double output =
        opFlags[USE_BLOCK_LIMITS] ? vLimiter->clampOutput(desiredOutput[0]) : desiredOutput[0];
    const double input = output / section.gain();
    m_state[lagState] = input;
    m_state[limiter_alg] = output;
    if (opFlags[USE_BLOCK_LIMITS]) {
        m_state[0] = output;
    }
    fieldSet[0] = input - bias;
    prevInput = input;
}

void LeadLagBlock::blockDerivative(double input,
                                   double /*didt*/,
                                   const StateData& stateDataValue,
                                   double deriv[],
                                   const SolverMode& solverModeValue)
{
    const auto locations = offsets.getLocations(stateDataValue, deriv, solverModeValue, this);
    locations.destDiffLoc[0] = section.derivative(input + bias, locations.diffStateLoc[0]);
}

void LeadLagBlock::blockAlgebraicUpdate(double input,
                                        const StateData& stateDataValue,
                                        double update[],
                                        const SolverMode& solverModeValue)
{
    const auto locations = offsets.getLocations(stateDataValue, update, solverModeValue, this);
    locations.destLoc[limiter_alg] = section.output(input + bias, locations.diffStateLoc[0]);
    if (limiter_alg > 0) {
        GridBlock::blockAlgebraicUpdate(input, stateDataValue, update, solverModeValue);
    }
}

void LeadLagBlock::blockJacobianElements(double /*input*/,
                                         double /*didt*/,
                                         const StateData& stateDataValue,
                                         MatrixData<double>& matrixDataValue,
                                         index_t argLoc,
                                         const SolverMode& solverModeValue)
{
    const auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
    if (hasAlgebraic(solverModeValue)) {
        const auto outputLocation = locations.algOffset + limiter_alg;
        matrixDataValue.assign(outputLocation, outputLocation, -1.0);
        matrixDataValue.assignCheckCol(outputLocation, argLoc, section.outputInputJacobian());
        if (hasDifferential(solverModeValue)) {
            matrixDataValue.assign(outputLocation,
                                   locations.diffOffset,
                                   section.outputStateJacobian());
        }
    }
    if (hasDifferential(solverModeValue)) {
        matrixDataValue.assignCheckCol(locations.diffOffset,
                                       argLoc,
                                       section.derivativeInputJacobian());
        matrixDataValue.assign(locations.diffOffset,
                               locations.diffOffset,
                               section.derivativeStateJacobian() - stateDataValue.cj);
    }
    if ((limiter_alg > 0) && hasAlgebraic(solverModeValue)) {
        GridBlock::blockJacobianElements(
            0.0, 0.0, stateDataValue, matrixDataValue, argLoc, solverModeValue);
    }
}

double LeadLagBlock::step(CoreTime time, double inputValue)
{
    const double input = inputValue + bias;
    const double timeStep = time - prevTime;
    const index_t lagState = offsets.local().local.algSize;
    if (timeStep > 0.0) {
        // Exact zero-order-hold solution of T_b xdot=u-x.  Solver-based
        // execution uses the residual above; this path serves local stepping.
        const double decay = std::exp(-timeStep / section.lagTime());
        m_state[lagState] = input + ((m_state[lagState] - input) * decay);
    }
    m_state[limiter_alg] = section.output(input, m_state[lagState]);
    prevInput = input;
    if (opFlags[USE_BLOCK_LIMITS]) {
        return GridBlock::step(time, inputValue);
    }
    prevTime = time;
    m_output = m_state[0];
    return m_output;
}

void LeadLagBlock::set(std::string_view param, std::string_view val)
{
    GridBlock::set(param, val);
}

void LeadLagBlock::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "ta") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("lead-lag Ta must be finite");
        }
        section.setParameters(val, section.lagTime(), section.gain());
    } else if ((param == "tb") || (param == "t")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("lead-lag Tb must be positive and finite");
        }
        section.setParameters(section.leadTime(), val, section.gain());
    } else if ((param == "k") || (param == "gain")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("lead-lag gain must be finite");
        }
        GridBlock::set(param, val, unitType);
        section.setParameters(section.leadTime(), section.lagTime(), val);
    } else {
        GridBlock::set(param, val, unitType);
    }
}

stringVec LeadLagBlock::localStateNames() const
{
    stringVec names(stateSize(cLocalSolverMode));
    index_t index = 0;
    if (opFlags[USE_BLOCK_LIMITS]) {
        names[index++] = "output";
    }
    names[index++] = "unlimited_output";
    names[index] = "lag_state";
    return names;
}
}  // namespace griddyn::blocks
