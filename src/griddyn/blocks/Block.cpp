/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RampLimiter.h"
#include "ValueLimiter.h"
#include "blockLibrary.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
static const TypeFactory<GridBlock>
    BLOCK_FACTORY("block", std::to_array<std::string_view>({"basic", "gain"}), "basic");
static const ChildTypeFactory<blocks::ControlBlock, GridBlock> CONTROL_BLOCK_FACTORY("block",
                                                                                     "control");
static const ChildTypeFactory<blocks::LeadLagBlock, GridBlock>
    LEAD_LAG_BLOCK_FACTORY("block", std::to_array<std::string_view>({"leadlag", "lead_lag"}));
static const ChildTypeFactory<blocks::DeadbandBlock, GridBlock>
    DEADBAND_BLOCK_FACTORY("block", std::to_array<std::string_view>({"deadband", "db"}));
static const ChildTypeFactory<blocks::DelayBlock, GridBlock>
    DELAY_BLOCK_FACTORY("block", std::to_array<std::string_view>({"delay", "filter"}));
static const ChildTypeFactory<blocks::PidBlock, GridBlock> PID_BLOCK_FACTORY("block", "pid");
static const ChildTypeFactory<blocks::IntegralBlock, GridBlock>
    INTEGRAL_BLOCK_FACTORY("block", std::to_array<std::string_view>({"integrator", "integral"}));
static const ChildTypeFactory<blocks::FunctionBlock, GridBlock>
    FUNCTION_BLOCK_FACTORY("block", std::to_array<std::string_view>({"function", "func"}));
static const ChildTypeFactory<blocks::LutBlock, GridBlock>
    LOOKUP_TABLE_BLOCK_FACTORY("block", std::to_array<std::string_view>({"lut", "lookuptable"}));
static const ChildTypeFactory<blocks::DerivativeBlock, GridBlock>
    DERIVATIVE_BLOCK_FACTORY("block",
                             std::to_array<std::string_view>({"der", "derivative", "deriv"}));
static const ChildTypeFactory<blocks::FilteredDerivativeBlock, GridBlock>
    FILTERED_DERIVATIVE_BLOCK_FACTORY(
        "block",
        std::to_array<std::string_view>({"fder", "filtered_deriv", "filtered_derivative"}));
static const ChildTypeFactory<blocks::TransferFunctionBlock, GridBlock>
    TRANSFER_FUNCTION_BLOCK_FACTORY(
        "block",
        std::to_array<std::string_view>({"transfer_function", "transferfunction", "tf"}));

GridBlock::GridBlock(const std::string& objName): GridSubModel(objName)
{
    m_inputSize = 1;
}
GridBlock::GridBlock(double gain, const std::string& objName): GridSubModel(objName), K(gain)
{
    m_inputSize = 1;
}
GridBlock::~GridBlock() = default;

CoreObject* GridBlock::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<GridBlock, GridSubModel>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->K = K;
    nobj->bias = bias;
    nobj->Omax = Omax;
    nobj->Omin = Omin;
    nobj->rampMax = rampMax;
    nobj->rampMin = rampMin;
    nobj->resetLevel = resetLevel;
    nobj->limiter_alg = limiter_alg;
    nobj->limiter_diff = limiter_diff;
    nobj->outputName = outputName;
    return nobj;
}

void GridBlock::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    auto& lcinfo = offsets.local();
    lcinfo.reset();
    offsets.unload();  // unload all the offsets

    lcinfo.local.jacSize = 2;
    if (!opFlags[USE_STATE]) {
        if (opFlags[USE_DIRECT]) {  // In use direct mode it just processes the input
            // also ignore the gain and bias
            lcinfo.local.jacSize = 0;
        } else {
            if (opFlags[DIFFERENTIAL_INPUT]) {
                lcinfo.local.diffSize = 1;
                opFlags.set(DIFFERENTIAL_OUTPUT);
            } else {
                lcinfo.local.algSize = 1;
            }
        }
    } else if (opFlags[DIFFERENTIAL_OUTPUT]) {
        lcinfo.local.diffSize = 1;
    } else {
        lcinfo.local.algSize = 1;
    }
    limiter_alg = 0;
    limiter_diff = 0;

    if (resetLevel < 0) {
        resetLevel = computeDefaultResetLevel();
    }
    if (opFlags[USE_BLOCK_LIMITS]) {
        if (opFlags[DIFFERENTIAL_OUTPUT]) {
            ++(lcinfo.local.diffRoots);
            ++(lcinfo.local.diffSize);
            limiter_diff = 1;
        } else {
            ++(lcinfo.local.algRoots);
            ++(lcinfo.local.algSize);
            limiter_alg = 1;
        }
        lcinfo.local.jacSize += 2;
        vLimiter = std::make_unique<blocks::ValueLimiter>(Omin, Omax);
        vLimiter->setResetLevel(resetLevel);
    }
    if ((opFlags[USE_RAMP_LIMITS]) &&
        (opFlags[DIFFERENTIAL_OUTPUT]))  // ramp limits only work with a
    // differential output state before the
    // limiters
    {
        ++lcinfo.local.diffSize;

        ++lcinfo.local.diffRoots;

        ++limiter_diff;
        lcinfo.local.jacSize += 2;
        rLimiter = std::make_unique<blocks::RampLimiter>(rampMin, rampMax);
        rLimiter->setResetLevel(resetLevel);
    }
    if (limiter_alg + limiter_diff > 0) {
        opFlags[HAS_LIMITS] = true;
    }
    if (opFlags[DIFFERENTIAL_INPUT]) {
        m_inputSize = 2;
    }
}

double GridBlock::getRateInput(const IOdata& inputs)
{
    return (inputs.size() > 1) ? inputs[1] : 0.0;
}
double GridBlock::computeDefaultResetLevel() const
{
    double rlevel = 0.001;
    if (Omax < kHalfBigNum) {
        if (Omin > -kHalfBigNum) {
            rlevel = (Omax - Omin) * 0.001;
        } else {
            rlevel = std::abs(Omax) * 0.001;
        }
    } else if (Omin > -kHalfBigNum) {
        rlevel = std::abs(Omin) * 0.001;
    }

    return rlevel;
}

double GridBlock::blockInitialize(double input, double desiredOutput)
{
    IOdata fieldSet;
    dynInitializeB((input != kNullVal) ? IOdata{input} : noInputs,
                   (desiredOutput != kNullVal) ? IOdata{desiredOutput} : noInputs,
                   fieldSet);
    return (!fieldSet.empty()) ? fieldSet[0] : kNullVal;
}

// initial conditions
void GridBlock::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    if (fieldSet.empty()) {
        fieldSet.resize(1);
    }

    if (desiredOutput.empty()) {
        assert(!inputs.empty());
        prevInput = (inputs[0] + bias);
        if (!opFlags[USE_STATE]) {
            if (!opFlags[USE_DIRECT]) {
                m_state[limiter_alg + limiter_diff] = (prevInput)*K;
                if (opFlags[USE_RAMP_LIMITS]) {
                    m_state[limiter_diff - 1] = m_state[limiter_diff];
                }
                if (opFlags[USE_BLOCK_LIMITS]) {
                    GridBlock::rootCheck(inputs,
                                         emptyStateData,
                                         cLocalSolverMode,
                                         CheckLevel::REVERSABLE_ONLY);
                    m_state[0] = vLimiter->clampOutput(m_state[1]);
                }
            }
        } else {
            if (opFlags[USE_RAMP_LIMITS]) {
                const index_t diffOffset = offsets.local().local.algSize + limiter_diff;
                m_state[diffOffset - 1] = m_state[diffOffset];
                if (opFlags[USE_BLOCK_LIMITS]) {
                    GridBlock::rootCheck(inputs,
                                         emptyStateData,
                                         cLocalSolverMode,
                                         CheckLevel::REVERSABLE_ONLY);
                    m_state[0] = vLimiter->clampOutput(m_state[diffOffset - 1]);
                }
            } else {
                if (opFlags[USE_BLOCK_LIMITS]) {
                    if (opFlags[DIFFERENTIAL_OUTPUT]) {
                        const index_t diffOffset = offsets.local().local.algSize;
                        GridBlock::rootCheck(inputs,
                                             emptyStateData,
                                             cLocalSolverMode,
                                             CheckLevel::REVERSABLE_ONLY);
                        m_state[0] = vLimiter->clampOutput(m_state[diffOffset]);
                    } else {
                        GridBlock::rootCheck(inputs,
                                             emptyStateData,
                                             cLocalSolverMode,
                                             CheckLevel::REVERSABLE_ONLY);
                        m_state[0] = vLimiter->clampOutput(m_state[1]);
                    }
                }
            }
        }

        fieldSet[0] = m_state[0];
    } else {
        m_state[0] = desiredOutput[0];
        if (opFlags[USE_BLOCK_LIMITS]) {
            m_state[0] = vLimiter->clampOutput(m_state[0]);
        }

        if (!opFlags[USE_STATE]) {
            if (opFlags[USE_BLOCK_LIMITS]) {
                m_state[1] = m_state[0];
            }
            if (opFlags[USE_RAMP_LIMITS]) {
                m_state[2] = m_state[0];  // we know the layout in this case
            }
        } else {
            if (opFlags[USE_RAMP_LIMITS]) {
                const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode) + limiter_diff;

                if (opFlags[USE_BLOCK_LIMITS]) {
                    m_state[limiter_diff - 1] = m_state[0];
                }
                m_state[diffOffset] = m_state[0];
                m_state[diffOffset + 1] = m_state[0];
            } else {
                if (opFlags[USE_BLOCK_LIMITS]) {
                    if (opFlags[DIFFERENTIAL_OUTPUT]) {
                        const index_t diffOffset =
                            offsets.getDiffOffset(cLocalSolverMode) + limiter_diff;
                        GridBlock::rootCheck(inputs,
                                             emptyStateData,
                                             cLocalSolverMode,
                                             CheckLevel::REVERSABLE_ONLY);
                        m_state[diffOffset] = m_state[0];
                    } else {
                        m_state[1] = m_state[0];
                    }
                }
            }
        }
        fieldSet[0] = (m_state[0] / K) - bias;
        prevInput = fieldSet[0] + bias;
    }
}

void GridBlock::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    step(time, inputs[0]);
}

static IOdata gKNullVec;

double GridBlock::step(CoreTime time, double input)
{
    if (!opFlags[USE_STATE]) {
        m_state[limiter_alg + limiter_diff] = (input + bias) * K;

        if (opFlags[HAS_LIMITS]) {
            if (opFlags[USE_RAMP_LIMITS]) {
                const int offset = offsets.getDiffOffset(cLocalSolverMode);
                const double ramp = (m_state[offset + 1] - m_state[offset]) / (time - prevTime);
                const double cramp = rLimiter->clampOutputRamp(ramp);
                if (cramp == ramp) {
                    m_state[offset] = m_state[offset + 1];
                } else {
                    m_state[offset] += cramp * (time - prevTime);
                }
            } else {
                rootCheck({input}, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY);
                m_state[0] = vLimiter->clampOutput(m_state[1]);
            }
        }
    } else {
        if (opFlags[USE_RAMP_LIMITS]) {
            const int offset = offsets.getDiffOffset(cLocalSolverMode);
            const double ramp = (m_state[offset + 1] - m_state[offset]) / (time - prevTime);
            const double cramp = rLimiter->clampOutputRamp(ramp);
            if (cramp == ramp) {
                m_state[offset] = m_state[offset + 1];
            } else {
                m_state[offset] += ramp * (time - prevTime);
            }
        } else {
            if (opFlags[USE_BLOCK_LIMITS]) {
                const auto offset = opFlags[DIFFERENTIAL_OUTPUT] ?
                    (offsets.getDiffOffset(cLocalSolverMode)) + 1 :
                    1;
                rootCheck(gKNullVec, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY);
                m_state[offset - 1] = vLimiter->clampOutput(m_state[offset]);
            }
        }
    }
    prevTime = time;
    const auto offset =
        opFlags[DIFFERENTIAL_OUTPUT] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
    return m_state[offset];
}

double GridBlock::getBlockOutput(const StateData& stateDataValue,
                                 const SolverMode& solverModeValue) const
{
    auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
    return opFlags[DIFFERENTIAL_OUTPUT] ? *locations.diffStateLoc : *locations.algStateLoc;
}

double GridBlock::getBlockOutput() const
{
    const auto offset =
        opFlags[DIFFERENTIAL_OUTPUT] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
    return m_state[offset];
}

double GridBlock::getBlockDoutDt(const StateData& stateDataValue,
                                 const SolverMode& solverModeValue) const
{
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
        return *locations.dstateLoc;
    }
    return 0.0;
}

double GridBlock::getBlockDoutDt() const
{
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        const auto offset =
            opFlags[DIFFERENTIAL_OUTPUT] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
        return m_dstate_dt[offset];
    }
    return 0.0;
}

void GridBlock::blockResidual(double input,
                              double didt,
                              const StateData& stateDataValue,
                              double resid[],
                              const SolverMode& solverModeValue)
{
    auto& solverOffsetsValue = offsets.getOffsets(solverModeValue);
    if (solverOffsetsValue.total.diffSize > 0) {
        blockDerivative(input, didt, stateDataValue, resid, solverModeValue);
        for (index_t ii = 0; ii < solverOffsetsValue.total.diffSize; ++ii) {
            resid[solverOffsetsValue.diffOffset + ii] -=
                stateDataValue.dstate_dt[solverOffsetsValue.diffOffset + ii];
        }
    }

    if (solverOffsetsValue.total.algSize > 0) {
        blockAlgebraicUpdate(input, stateDataValue, resid, solverModeValue);
        for (index_t ii = 0; ii < solverOffsetsValue.total.algSize; ++ii) {
            resid[solverOffsetsValue.algOffset + ii] -=
                stateDataValue.state[solverOffsetsValue.algOffset + ii];
            /*if ((vLimiter) && (vLimiter->isActive()))
            {
                    printf("%d:%d:%f input=%f , state=%f resid=%f\n", sD.seqID,
            ii,static_cast<double>(sD.time), input, sD.state[so.algOffset + ii],
            resid[so.algOffset + ii]);
            }
            */
        }
    }
}

void GridBlock::limiterResidElements(double input,
                                     double didt,
                                     const StateData& stateDataValue,
                                     double resid[],
                                     const SolverMode& solverModeValue)
{
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        auto offset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        double testValue = getTestRate(didt, stateDataValue.dstate_dt[offset]);

        if (limiter_diff > 0) {
            if (opFlags[USE_RAMP_LIMITS]) {
                --offset;
                resid[offset] = rLimiter->deriv(testValue);
                testValue = resid[offset];
                resid[offset] -= stateDataValue.dstate_dt[offset];
            }
            if (opFlags[USE_BLOCK_LIMITS]) {
                resid[offset - 1] =
                    vLimiter->deriv(testValue) - stateDataValue.dstate_dt[offset - 1];
            }
        }
    } else {
        auto offset = offsets.getAlgOffset(solverModeValue) + limiter_alg;
        const double testValue = getTestValue(input, stateDataValue.state[offset]);
        if (opFlags[HAS_LIMITS]) {
            resid[offset - 1] = vLimiter->output(testValue) - stateDataValue.state[offset - 1];
        }
    }

    auto& solverOffsetsValue = offsets.getOffsets(solverModeValue);
    if (solverOffsetsValue.total.diffSize > 0) {
        blockDerivative(input, didt, stateDataValue, resid, solverModeValue);
        for (index_t ii = 0; ii < solverOffsetsValue.total.diffSize; ++ii) {
            resid[solverOffsetsValue.diffOffset + ii] -=
                stateDataValue.dstate_dt[solverOffsetsValue.diffOffset + ii];
        }
    }

    if (solverOffsetsValue.total.algSize > 0) {
        blockAlgebraicUpdate(input, stateDataValue, resid, solverModeValue);
        for (index_t ii = 0; ii < solverOffsetsValue.total.algSize; ++ii) {
            resid[solverOffsetsValue.algOffset + ii] -=
                stateDataValue.state[solverOffsetsValue.algOffset + ii];
        }
    }
}
// residual
void GridBlock::residual(const IOdata& inputs,
                         const StateData& stateDataValue,
                         double resid[],
                         const SolverMode& solverModeValue)
{
    blockResidual(inputs[0], getRateInput(inputs), stateDataValue, resid, solverModeValue);
}

bool GridBlock::hasValueState() const
{
    return (!((opFlags[USE_STATE]) || (opFlags[USE_DIRECT])));
}
double GridBlock::getTestValue(double input, double currentState) const
{
    double testVal;
    if (opFlags[USE_STATE]) {
        testVal = currentState;
    } else if (opFlags[USE_DIRECT]) {
        testVal = input * K;
    } else {
        testVal = (input + bias) * K;
    }
    return testVal;
}

double GridBlock::getTestRate(double didt, double currentStateRate) const
{
    double testRate;
    if (opFlags[USE_STATE]) {
        testRate = currentStateRate;
    } else {
        testRate = didt * K;
    }
    return testRate;
}

void GridBlock::blockAlgebraicUpdate(double input,
                                     const StateData& stateDataValue,
                                     double update[],
                                     const SolverMode& solverModeValue)
{
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        return;
    }

    auto offset = offsets.getAlgOffset(solverModeValue) + limiter_alg;
    double testValue = getTestValue(input, stateDataValue.state[offset]);
    if (hasValueState()) {
        update[offset] = testValue;
        testValue = stateDataValue.state[offset];  // need to alter the testVal for the next check
        // otherwise the residual fails to check
        // properly
    }
    if (opFlags[HAS_LIMITS]) {
        update[offset - 1] = vLimiter->output(testValue);
    }
}

void GridBlock::algebraicUpdate(const IOdata& inputs,
                                const StateData& stateDataValue,
                                double update[],
                                const SolverMode& solverModeValue,
                                double /*alpha*/)
{
    blockAlgebraicUpdate(inputs[0], stateDataValue, update, solverModeValue);
}

void GridBlock::blockDerivative(double /*input*/,
                                double didt,
                                const StateData& stateDataValue,
                                double deriv[],
                                const SolverMode& solverModeValue)
{
    if (opFlags[DIFFERENTIAL_OUTPUT]) {
        auto offset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        double testValue = getTestRate(didt, stateDataValue.dstate_dt[offset]);
        if (hasValueState()) {
            deriv[offset] = testValue;
        }
        if (limiter_diff > 0) {
            if (opFlags[USE_RAMP_LIMITS]) {
                --offset;
                deriv[offset] = rLimiter->output(testValue);
                testValue = deriv[offset];
            }
            if (opFlags[USE_BLOCK_LIMITS]) {
                deriv[offset - 1] = vLimiter->deriv(testValue);
            }
        }
    }
}
// residual
void GridBlock::derivative(const IOdata& inputs,
                           const StateData& stateDataValue,
                           double deriv[],
                           const SolverMode& solverModeValue)
{
    blockDerivative(inputs[0], getRateInput(inputs), stateDataValue, deriv, solverModeValue);
}

void GridBlock::blockJacobianElements(double /*input*/,
                                      double /*didt*/,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      index_t argLoc,
                                      const SolverMode& solverModeValue)
{
    if ((opFlags[DIFFERENTIAL_OUTPUT]) && (hasDifferential(solverModeValue))) {
        auto offset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        if (hasValueState()) {
            matrixDataValue.assignCheckCol(offset, argLoc, K * stateDataValue.cj);
            matrixDataValue.assign(offset, offset, -stateDataValue.cj);
        }
        if (limiter_diff > 0) {
            if (opFlags[USE_RAMP_LIMITS]) {
                --offset;
                matrixDataValue.assign(offset, offset, -stateDataValue.cj);
                if (opFlags[USE_DIRECT]) {
                    matrixDataValue.assignCheckCol(offset,
                                                   argLoc,
                                                   K * stateDataValue.cj * rLimiter->DoutDin());
                } else {
                    matrixDataValue.assign(offset,
                                           offset + 1,
                                           stateDataValue.cj * rLimiter->DoutDin());
                }
            }
            if (opFlags[USE_BLOCK_LIMITS]) {
                --offset;
                matrixDataValue.assign(offset, offset, -stateDataValue.cj);

                if ((opFlags[USE_DIRECT]) && (!opFlags[USE_RAMP_LIMITS])) {
                    matrixDataValue.assignCheckCol(offset,
                                                   argLoc,
                                                   K * stateDataValue.cj * vLimiter->DoutDin());
                } else {
                    matrixDataValue.assign(offset,
                                           offset + 1,
                                           stateDataValue.cj * vLimiter->DoutDin());
                }
            }
        }
    }
    // Now do the algebraic states if needed
    if ((!opFlags[DIFFERENTIAL_OUTPUT]) && (hasAlgebraic(solverModeValue))) {
        auto offset = offsets.getAlgOffset(solverModeValue) + limiter_alg;
        if (hasValueState()) {
            matrixDataValue.assignCheckCol(offset, argLoc, K);
            matrixDataValue.assign(offset, offset, -1.0);
        }
        if (limiter_alg > 0) {
            --offset;
            matrixDataValue.assign(offset, offset, -1.0);
            if (opFlags[USE_DIRECT]) {
                matrixDataValue.assign(offset, argLoc, K * vLimiter->DoutDin());
            } else {
                matrixDataValue.assign(offset, offset + 1, vLimiter->DoutDin());
            }
        }
    }
}

void GridBlock::jacobianElements(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 MatrixData<double>& matrixDataValue,
                                 const IOlocs& inputLocs,
                                 const SolverMode& solverModeValue)
{
    blockJacobianElements(inputs[0],
                          getRateInput(inputs),
                          stateDataValue,
                          matrixDataValue,
                          inputLocs[0],
                          solverModeValue);
}

double GridBlock::getLimiterTestValue(double input,
                                      const StateData& stateDataValue,
                                      const SolverMode& solverModeValue)
{
    auto offset = (opFlags[DIFFERENTIAL_OUTPUT]) ? offsets.getDiffOffset(solverModeValue) :
                                                   offsets.getAlgOffset(solverModeValue);
    auto stateVal = (stateDataValue.empty()) ? m_state[1] : stateDataValue.state[offset + 1];
    if (hasValueState() || opFlags[USE_RAMP_LIMITS]) {
        return stateVal;
    }
    return getTestValue(input, stateVal);
}

void GridBlock::rootTest(const IOdata& inputs,
                         const StateData& stateDataValue,
                         double roots[],
                         const SolverMode& solverModeValue)
{
    if (!opFlags[HAS_LIMITS]) {
        return;
    }
    int rootOffset = offsets.getRootOffset(solverModeValue);
    if (opFlags[USE_RAMP_LIMITS]) {
        auto doffset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        const double testRate = getTestRate(inputs[1], stateDataValue.dstate_dt[doffset]);
        const double testValue = getTestValue(inputs[0], stateDataValue.state[doffset]);
        roots[rootOffset] =
            rLimiter->limitCheck(stateDataValue.state[doffset], testValue, testRate);
        ++rootOffset;
    }

    if (opFlags[USE_BLOCK_LIMITS]) {
        const double value = getLimiterTestValue(inputs[0], stateDataValue, solverModeValue);
        roots[rootOffset] = vLimiter->limitCheck(value);
    }
}

ChangeCode GridBlock::rootCheck(const IOdata& inputs,
                                const StateData& stateDataValue,
                                const SolverMode& solverModeValue,
                                CheckLevel /*level*/)
{
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (!opFlags[HAS_LIMITS]) {
        return ret;
    }
    const double* stateValues = ((!stateDataValue.empty()) ? stateDataValue.state : m_state.data());
    const double* stateDerivatives =
        ((!stateDataValue.empty()) ? stateDataValue.dstate_dt : m_dstate_dt.data());
    if (opFlags[USE_RAMP_LIMITS]) {
        auto doffset = offsets.getDiffOffset(solverModeValue);
        const double testRate = getTestRate(getRateInput(inputs), stateDerivatives[doffset]);
        const double testValue = getTestValue(inputs[0], stateValues[doffset]);
        const double limitValue = rLimiter->limitCheck(stateValues[doffset], testValue, testRate);
        if (limitValue < 0.0) {
            rLimiter->changeLimitActivation(testRate);
            ret = ChangeCode::NON_STATE_CHANGE;
        }
    }
    if (opFlags[USE_BLOCK_LIMITS]) {
        const double value = getLimiterTestValue(inputs[0], stateDataValue, solverModeValue);
        const double limitValue = vLimiter->limitCheck(value);
        if (limitValue < 0.0) {
            vLimiter->changeLimitActivation(value);
            ret = ChangeCode::NON_STATE_CHANGE;
        }
    }

    return ret;
}

void GridBlock::rootTrigger(CoreTime /*time*/,
                            const IOdata& inputs,
                            const std::vector<int>& rootMask,
                            const SolverMode& solverModeValue)
{
    if (!opFlags[HAS_LIMITS]) {
        return;
    }
    auto roffset = offsets.getRootOffset(solverModeValue);

    if (opFlags[USE_RAMP_LIMITS]) {
        if (rootMask[roffset] != 0) {
            auto doffset = offsets.getDiffOffset(cLocalSolverMode);
            const double testRate = getTestRate(getRateInput(inputs), m_dstate_dt[doffset]);
            rLimiter->changeLimitActivation(testRate);
            // ret = ChangeCode::NON_STATE_CHANGE;
        }
        ++roffset;
    }
    if (opFlags[USE_BLOCK_LIMITS]) {
        if (rootMask[roffset] == 0) {
            return;
        }
        const double value = getLimiterTestValue(inputs.empty() ? m_state[0] : inputs[0],
                                                 emptyStateData,
                                                 solverModeValue);

        vLimiter->changeLimitActivation(value);
        m_state[0] = vLimiter->output(value);
    }
}

void GridBlock::setFlag(std::string_view flag, bool val)
{
    if (flag == "use_limits") {
        if (!opFlags[DYN_INITIALIZED]) {
            opFlags[HAS_LIMITS] = val;
            opFlags[USE_BLOCK_LIMITS] = val;
            opFlags[USE_RAMP_LIMITS] = val;
        }
    } else if (flag == "simplified") {
        if (opFlags[DYN_INITIALIZED]) {
            if (opFlags[SIMPLIFIED_MODE] != val) {
                // this is probably not the best thing to
                // be changing after initialization
                opFlags[SIMPLIFIED_MODE] = val;
                dynObjectInitializeA(prevTime, 0);
                alert(this, STATE_COUNT_CHANGE);
                logging::warning(this,
                                 "changing object state computations during simulation "
                                 "triggers solver reset");
            }
        } else {
            opFlags[SIMPLIFIED_MODE] = val;
        }
    } else if (flag == "use_direct") {
        if (!opFlags[DYN_INITIALIZED]) {
            opFlags[USE_DIRECT] = val;
        }
    } else if (flag == "differential_input") {
        if (!opFlags[DYN_INITIALIZED]) {
            opFlags[DIFFERENTIAL_INPUT] = val;
        }
    } else if (flag == "use_ramp_limits") {
        if (!opFlags[DYN_INITIALIZED]) {
            opFlags[USE_RAMP_LIMITS] = val;
        }
    } else {
        GridSubModel::setFlag(flag, val);
    }
}

// set parameters
void GridBlock::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}
void GridBlock::set(std::string_view param, double val, units::unit unitType)
{
    // param   = GridDynSimulation::toLower(param);

    if ((param == "k") || (param == "gain")) {
        K = val;
    } else if ((param == "bias") || (param == "b")) {
        bias = val;
    } else if ((param == "omax") || (param == "max")) {
        Omax = val;
        valLimiterUpdate();
    } else if ((param == "omin") || (param == "min")) {
        Omin = val;
        valLimiterUpdate();
    } else if (param == "limit") {
        Omax = val;
        Omin = -val;
        valLimiterUpdate();
    } else if (param == "rampmax") {
        rampMax = val;
        rampLimiterUpdate();
    } else if (param == "rampmin") {
        rampMin = val;
        rampLimiterUpdate();
    } else if (param == "ramplimit") {
        rampMin = -val;
        rampMax = val;
        rampLimiterUpdate();
    } else if (param == "resetlevel") {
        resetLevel = val;
        if (vLimiter) {
            vLimiter->setResetLevel(val);
        }
        if (rLimiter) {
            rLimiter->setResetLevel(val);
        }
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

double GridBlock::get(std::string_view param, units::unit unitType) const
{
    if (param == "maxstepsize") {
        return kBigNum;
    }
    return GridSubModel::get(param, unitType);
}

void GridBlock::valLimiterUpdate()
{
    if (!opFlags[DYN_INITIALIZED]) {
        opFlags.set(USE_BLOCK_LIMITS);
    } else {
        if (vLimiter) {
            vLimiter->setLimits(Omin, Omax);
        }
    }
}

void GridBlock::rampLimiterUpdate()
{
    if (!opFlags[DYN_INITIALIZED]) {
        opFlags.set(USE_RAMP_LIMITS);
    } else {
        if (rLimiter) {
            rLimiter->setLimits(rampMin, rampMax);
        }
    }
}

stringVec GridBlock::localStateNames() const
{
    stringVec stNames;
    if (opFlags[USE_BLOCK_LIMITS]) {
        stNames.emplace_back("block_limits");
    }
    if (opFlags[USE_RAMP_LIMITS]) {
        stNames.emplace_back("ramp_limits");
    }
    if (hasValueState()) {
        stNames.emplace_back("test");
    }
    if (!stNames.empty()) {
        stNames[0] = outputName;
    }
    return stNames;
}

std::unique_ptr<GridBlock> make_block(const std::string& blockstr)
{
    using gmlc::utilities::convertToLowerCase;
    using gmlc::utilities::numeric_conversion;
    using gmlc::utilities::numeric_conversionComplete;
    using gmlc::utilities::str2vector;
    using gmlc::utilities::string_viewOps::split;
    using gmlc::utilities::string_viewOps::trim;

    const std::string_view blockstrv(blockstr);
    auto posp1 = blockstrv.find_first_of('(');
    auto posp2 = blockstrv.find_last_of(')');
    auto blockNameStr = blockstrv.substr(0, posp1 - 1);
    auto argstr = blockstrv.substr(posp1 + 1, posp2 - posp1 - 1);

    auto inputs = str2vector(argstr, kNullVal);
    auto tail = blockstrv.substr(posp2 + 2);
    auto tailArgs = split(tail);
    trim(tailArgs);
    double gain = 1.0;
    posp1 = blockNameStr.find_first_of('*');
    std::unique_ptr<GridBlock> ret;
    std::string fstr;
    if (posp1 == std::string::npos) {
        fstr = convertToLowerCase(std::string{blockNameStr});
    } else {
        gain = numeric_conversion(blockNameStr, 1.0);  // purposely not using
                                                       // numeric_conversionComplete to just
                                                       // get the first number
        fstr = convertToLowerCase(std::string{blockNameStr.substr(posp1 + 1)});
    }
    if (fstr == "basic") {
        ret = std::make_unique<GridBlock>(gain);
    } else if ((fstr == "der") || (fstr == "derivative")) {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::DerivativeBlock>();
        } else {
            ret = std::make_unique<blocks::DerivativeBlock>(inputs[0]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if ((fstr == "integral") || (fstr == "integrator")) {
        ret = std::make_unique<blocks::IntegralBlock>(gain);
    } else if (fstr == "control") {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::ControlBlock>();
        } else if (inputs.size() == 1) {
            ret = std::make_unique<blocks::ControlBlock>(inputs[0]);
        } else {
            ret = std::make_unique<blocks::ControlBlock>(inputs[0], inputs[1]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "delay") {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::DelayBlock>();
        } else {
            ret = std::make_unique<blocks::DelayBlock>(inputs[0]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "deadband") {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::DeadbandBlock>();
        } else {
            ret = std::make_unique<blocks::DeadbandBlock>(inputs[0]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "pid") {
        double proportionalGain = 1.0;
        double integralGain = 0.0;
        double derivativeGain = 0.0;
        if (!inputs.empty()) {
            proportionalGain = inputs[0];
        }
        if (inputs.size() > 1) {
            integralGain = inputs[1];
        }
        if (inputs.size() > 2) {
            derivativeGain = inputs[2];
        }

        ret = std::make_unique<blocks::PidBlock>(proportionalGain, integralGain, derivativeGain);
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "function") {
        if (argstr.empty()) {
            ret = std::make_unique<blocks::FunctionBlock>();
        } else {
            ret = std::make_unique<blocks::FunctionBlock>(std::string{argstr});
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else {
        return ret;
    }
    // process any additional parameters
    if (!tailArgs.empty()) {
        for (auto& tailArg : tailArgs) {
            auto eloc = tailArg.find_first_of('=');
            if (eloc == std::string::npos) {
                ret->setFlag(std::string{tailArg}, true);
            } else {
                auto param = tailArg.substr(0, eloc);
                const double numericValue =
                    numeric_conversionComplete(tailArg.substr(eloc + 1), kNullVal);
                if (numericValue == kNullVal) {
                    ret->set(std::string{param}, std::string{tailArg.substr(eloc + 1)});
                } else {
                    ret->set(std::string{param}, numericValue);
                }
            }
        }
    }
    return ret;
}
}  // namespace griddyn
