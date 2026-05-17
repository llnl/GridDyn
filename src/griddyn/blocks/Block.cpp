/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "blockLibrary.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "rampLimiter.h"
#include "utilities/matrixData.hpp"
#include "valueLimiter.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
namespace {
void registerBlockFactoryTypes()
{
    static const typeFactory<Block> blockFactory(
        "block", std::to_array<std::string_view>({"basic", "gain"}), "basic");
    static const childTypeFactory<blocks::controlBlock, Block> controlBlockFactory("block", "control");
    static const childTypeFactory<blocks::deadbandBlock, Block> deadbandBlockFactory(
        "block", std::to_array<std::string_view>({"deadband", "db"}));
    static const childTypeFactory<blocks::delayBlock, Block> delayBlockFactory(
        "block", std::to_array<std::string_view>({"delay", "filter"}));
    static const childTypeFactory<blocks::pidBlock, Block> pidBlockFactory("block", "pid");
    static const childTypeFactory<blocks::integralBlock, Block> integralBlockFactory(
        "block", std::to_array<std::string_view>({"integrator", "integral"}));
    static const childTypeFactory<blocks::functionBlock, Block> functionBlockFactory(
        "block", std::to_array<std::string_view>({"function", "func"}));
    static const childTypeFactory<blocks::lutBlock, Block> lookupTableBlockFactory(
        "block", std::to_array<std::string_view>({"lut", "lookuptable"}));
    static const childTypeFactory<blocks::derivativeBlock, Block> derivativeBlockFactory(
        "block", std::to_array<std::string_view>({"der", "derivative", "deriv"}));
    static const childTypeFactory<blocks::filteredDerivativeBlock, Block> filteredDerivativeBlockFactory(
        "block",
        std::to_array<std::string_view>({"fder", "filtered_deriv", "filtered_derivative"}));
    static_cast<void>(blockFactory);
    static_cast<void>(controlBlockFactory);
    static_cast<void>(deadbandBlockFactory);
    static_cast<void>(delayBlockFactory);
    static_cast<void>(pidBlockFactory);
    static_cast<void>(integralBlockFactory);
    static_cast<void>(functionBlockFactory);
    static_cast<void>(lookupTableBlockFactory);
    static_cast<void>(derivativeBlockFactory);
    static_cast<void>(filteredDerivativeBlockFactory);
}
}  // namespace

Block::Block(const std::string& objName): GridSubModel(objName)
{
    registerBlockFactoryTypes();
    m_inputSize = 1;
}
Block::Block(double gain, const std::string& objName): GridSubModel(objName), K(gain)
{
    registerBlockFactoryTypes();
    m_inputSize = 1;
}
Block::~Block() = default;

CoreObject* Block::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<Block, GridSubModel>(this, obj);
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

void Block::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    auto& lcinfo = offsets.local();
    lcinfo.reset();
    offsets.unload();  // unload all the offsets

    lcinfo.local.jacSize = 2;
    if (!opFlags[use_state]) {
        if (opFlags[use_direct]) {  // In use direct mode it just processes the input
            // also ignore the gain and bias
            lcinfo.local.jacSize = 0;
        } else {
            if (opFlags[differential_input]) {
                lcinfo.local.diffSize = 1;
                opFlags.set(differential_output);
            } else {
                lcinfo.local.algSize = 1;
            }
        }
    } else if (opFlags[differential_output]) {
        lcinfo.local.diffSize = 1;
    } else {
        lcinfo.local.algSize = 1;
    }
    limiter_alg = 0;
    limiter_diff = 0;

    if (resetLevel < 0) {
        resetLevel = computeDefaultResetLevel();
    }
    if (opFlags[use_block_limits]) {
        if (opFlags[differential_output]) {
            ++(lcinfo.local.diffRoots);
            ++(lcinfo.local.diffSize);
            limiter_diff = 1;
        } else {
            ++(lcinfo.local.algRoots);
            ++(lcinfo.local.algSize);
            limiter_alg = 1;
        }
        lcinfo.local.jacSize += 2;
        vLimiter = std::make_unique<blocks::valueLimiter>(Omin, Omax);
        vLimiter->setResetLevel(resetLevel);
    }
    if ((opFlags[use_ramp_limits]) &&
        (opFlags[differential_output]))  // ramp limits only work with a
    // differential output state before the
    // limiters
    {
        ++lcinfo.local.diffSize;

        ++lcinfo.local.diffRoots;

        ++limiter_diff;
        lcinfo.local.jacSize += 2;
        rLimiter = std::make_unique<blocks::rampLimiter>(rampMin, rampMax);
        rLimiter->setResetLevel(resetLevel);
    }
    if (limiter_alg + limiter_diff > 0) {
        opFlags[has_limits] = true;
    }
    if (opFlags[differential_input]) {
        m_inputSize = 2;
    }
}

double Block::getRateInput(const IOdata& inputs)
{
    return (inputs.size() > 1) ? inputs[1] : 0.0;
}
double Block::computeDefaultResetLevel() const
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

double Block::blockInitialize(double input, double desiredOutput)
{
    IOdata fieldSet;
    dynInitializeB((input != kNullVal) ? IOdata{input} : noInputs,
                   (desiredOutput != kNullVal) ? IOdata{desiredOutput} : noInputs,
                   fieldSet);
    return (!fieldSet.empty()) ? fieldSet[0] : kNullVal;
}

// initial conditions
void Block::dynObjectInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (fieldSet.empty()) {
        fieldSet.resize(1);
    }

    if (desiredOutput.empty()) {
        assert(!inputs.empty());
        prevInput = (inputs[0] + bias);
        if (!opFlags[use_state]) {
            if (!opFlags[use_direct]) {
                m_state[limiter_alg + limiter_diff] = (prevInput)*K;
                if (opFlags[use_ramp_limits]) {
                    m_state[limiter_diff - 1] = m_state[limiter_diff];
                }
                if (opFlags[use_block_limits]) {
                    Block::rootCheck(inputs,
                                     emptyStateData,
                                     cLocalSolverMode,
                                     check_level_t::reversable_only);
                    m_state[0] = vLimiter->clampOutput(m_state[1]);
                }
            }
        } else {
            if (opFlags[use_ramp_limits]) {
                const index_t diffOffset = offsets.local().local.algSize + limiter_diff;
                m_state[diffOffset - 1] = m_state[diffOffset];
                if (opFlags[use_block_limits]) {
                    Block::rootCheck(inputs,
                                     emptyStateData,
                                     cLocalSolverMode,
                                     check_level_t::reversable_only);
                    m_state[0] = vLimiter->clampOutput(m_state[diffOffset - 1]);
                }
            } else {
                if (opFlags[use_block_limits]) {
                    if (opFlags[differential_output]) {
                        const index_t diffOffset = offsets.local().local.algSize;
                        Block::rootCheck(inputs,
                                         emptyStateData,
                                         cLocalSolverMode,
                                         check_level_t::reversable_only);
                        m_state[0] = vLimiter->clampOutput(m_state[diffOffset]);
                    } else {
                        Block::rootCheck(inputs,
                                         emptyStateData,
                                         cLocalSolverMode,
                                         check_level_t::reversable_only);
                        m_state[0] = vLimiter->clampOutput(m_state[1]);
                    }
                }
            }
        }

        fieldSet[0] = m_state[0];
    } else {
        m_state[0] = desiredOutput[0];
        if (opFlags[use_block_limits]) {
            m_state[0] = vLimiter->clampOutput(m_state[0]);
        }

        if (!opFlags[use_state]) {
            if (opFlags[use_block_limits]) {
                m_state[1] = m_state[0];
            }
            if (opFlags[use_ramp_limits]) {
                m_state[2] = m_state[0];  // we know the layout in this case
            }
        } else {
            if (opFlags[use_ramp_limits]) {
                const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode) + limiter_diff;

                if (opFlags[use_block_limits]) {
                    m_state[limiter_diff - 1] = m_state[0];
                }
                m_state[diffOffset] = m_state[0];
                m_state[diffOffset + 1] = m_state[0];
            } else {
                if (opFlags[use_block_limits]) {
                    if (opFlags[differential_output]) {
                        const index_t diffOffset =
                            offsets.getDiffOffset(cLocalSolverMode) + limiter_diff;
                        Block::rootCheck(inputs,
                                         emptyStateData,
                                         cLocalSolverMode,
                                         check_level_t::reversable_only);
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

void Block::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    step(time, inputs[0]);
}

static IOdata kNullVec;

double Block::step(coreTime time, double input)
{
    if (!opFlags[use_state]) {
        m_state[limiter_alg + limiter_diff] = (input + bias) * K;

        if (opFlags[has_limits]) {
            if (opFlags[use_ramp_limits]) {
                const int offset = offsets.getDiffOffset(cLocalSolverMode);
                const double ramp = (m_state[offset + 1] - m_state[offset]) / (time - prevTime);
                const double cramp = rLimiter->clampOutputRamp(ramp);
                if (cramp == ramp) {
                    m_state[offset] = m_state[offset + 1];
                } else {
                    m_state[offset] += cramp * (time - prevTime);
                }
            } else {
                rootCheck({input},
                          emptyStateData,
                          cLocalSolverMode,
                          check_level_t::reversable_only);
                m_state[0] = vLimiter->clampOutput(m_state[1]);
            }
        }
    } else {
        if (opFlags[use_ramp_limits]) {
            const int offset = offsets.getDiffOffset(cLocalSolverMode);
            const double ramp = (m_state[offset + 1] - m_state[offset]) / (time - prevTime);
            const double cramp = rLimiter->clampOutputRamp(ramp);
            if (cramp == ramp) {
                m_state[offset] = m_state[offset + 1];
            } else {
                m_state[offset] += ramp * (time - prevTime);
            }
        } else {
            if (opFlags[use_block_limits]) {
                const auto offset = opFlags[differential_output] ?
                    (offsets.getDiffOffset(cLocalSolverMode)) + 1 :
                    1;
                rootCheck(kNullVec,
                          emptyStateData,
                          cLocalSolverMode,
                          check_level_t::reversable_only);
                m_state[offset - 1] = vLimiter->clampOutput(m_state[offset]);
            }
        }
    }
    prevTime = time;
    const auto offset =
        opFlags[differential_output] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
    return m_state[offset];
}

double Block::getBlockOutput(const stateData& stateDataValue,
                             const solverMode& solverModeValue) const
{
    auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
    return opFlags[differential_output] ? *locations.diffStateLoc : *locations.algStateLoc;
}

double Block::getBlockOutput() const
{
    const auto offset =
        opFlags[differential_output] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
    return m_state[offset];
}

double Block::getBlockDoutDt(const stateData& stateDataValue,
                             const solverMode& solverModeValue) const
{
    if (opFlags[differential_output]) {
        auto locations = offsets.getLocations(stateDataValue, solverModeValue, this);
        return *locations.dstateLoc;
    }
    return 0.0;
}

double Block::getBlockDoutDt() const
{
    if (opFlags[differential_output]) {
        const auto offset =
            opFlags[differential_output] ? (offsets.getDiffOffset(cLocalSolverMode)) : 0;
        return m_dstate_dt[offset];
    }
    return 0.0;
}

void Block::blockResidual(double input,
                          double didt,
                          const stateData& stateDataValue,
                          double resid[],
                          const solverMode& solverModeValue)
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

void Block::limiterResidElements(double input,
                                 double didt,
                                 const stateData& stateDataValue,
                                 double resid[],
                                 const solverMode& solverModeValue)
{
    if (opFlags[differential_output]) {
        auto offset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        double testValue = getTestRate(didt, stateDataValue.dstate_dt[offset]);

        if (limiter_diff > 0) {
            if (opFlags[use_ramp_limits]) {
                --offset;
                resid[offset] = rLimiter->deriv(testValue);
                testValue = resid[offset];
                resid[offset] -= stateDataValue.dstate_dt[offset];
            }
            if (opFlags[use_block_limits]) {
                resid[offset - 1] =
                    vLimiter->deriv(testValue) - stateDataValue.dstate_dt[offset - 1];
            }
        }
    } else {
        auto offset = offsets.getAlgOffset(solverModeValue) + limiter_alg;
        const double testValue = getTestValue(input, stateDataValue.state[offset]);
        if (opFlags[has_limits]) {
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
void Block::residual(const IOdata& inputs,
                     const stateData& stateDataValue,
                     double resid[],
                     const solverMode& solverModeValue)
{
    blockResidual(inputs[0], getRateInput(inputs), stateDataValue, resid, solverModeValue);
}

bool Block::hasValueState() const
{
    return (!((opFlags[use_state]) || (opFlags[use_direct])));
}
double Block::getTestValue(double input, double currentState) const
{
    double testVal;
    if (opFlags[use_state]) {
        testVal = currentState;
    } else if (opFlags[use_direct]) {
        testVal = input * K;
    } else {
        testVal = (input + bias) * K;
    }
    return testVal;
}

double Block::getTestRate(double didt, double currentStateRate) const
{
    double testRate;
    if (opFlags[use_state]) {
        testRate = currentStateRate;
    } else {
        testRate = didt * K;
    }
    return testRate;
}

void Block::blockAlgebraicUpdate(double input,
                                 const stateData& stateDataValue,
                                 double update[],
                                 const solverMode& solverModeValue)
{
    if (opFlags[differential_output]) {
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
    if (opFlags[has_limits]) {
        update[offset - 1] = vLimiter->output(testValue);
    }
}

void Block::algebraicUpdate(const IOdata& inputs,
                            const stateData& stateDataValue,
                            double update[],
                            const solverMode& solverModeValue,
                            double /*alpha*/)
{
    blockAlgebraicUpdate(inputs[0], stateDataValue, update, solverModeValue);
}

void Block::blockDerivative(double /*input*/,
                            double didt,
                            const stateData& stateDataValue,
                            double deriv[],
                            const solverMode& solverModeValue)
{
    if (opFlags[differential_output]) {
        auto offset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        double testValue = getTestRate(didt, stateDataValue.dstate_dt[offset]);
        if (hasValueState()) {
            deriv[offset] = testValue;
        }
        if (limiter_diff > 0) {
            if (opFlags[use_ramp_limits]) {
                --offset;
                deriv[offset] = rLimiter->output(testValue);
                testValue = deriv[offset];
            }
            if (opFlags[use_block_limits]) {
                deriv[offset - 1] = vLimiter->deriv(testValue);
            }
        }
    }
}
// residual
void Block::derivative(const IOdata& inputs,
                       const stateData& stateDataValue,
                       double deriv[],
                       const solverMode& solverModeValue)
{
    blockDerivative(inputs[0], getRateInput(inputs), stateDataValue, deriv, solverModeValue);
}

void Block::blockJacobianElements(double /*input*/,
                                  double /*didt*/,
                                  const stateData& stateDataValue,
                                  matrixData<double>& matrixDataValue,
                                  index_t argLoc,
                                  const solverMode& solverModeValue)
{
    if ((opFlags[differential_output]) && (hasDifferential(solverModeValue))) {
        auto offset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        if (hasValueState()) {
            matrixDataValue.assignCheckCol(offset, argLoc, K * stateDataValue.cj);
            matrixDataValue.assign(offset, offset, -stateDataValue.cj);
        }
        if (limiter_diff > 0) {
            if (opFlags[use_ramp_limits]) {
                --offset;
                matrixDataValue.assign(offset, offset, -stateDataValue.cj);
                if (opFlags[use_direct]) {
                    matrixDataValue.assignCheckCol(
                        offset, argLoc, K * stateDataValue.cj * rLimiter->DoutDin());
                } else {
                    matrixDataValue.assign(
                        offset, offset + 1, stateDataValue.cj * rLimiter->DoutDin());
                }
            }
            if (opFlags[use_block_limits]) {
                --offset;
                matrixDataValue.assign(offset, offset, -stateDataValue.cj);

                if ((opFlags[use_direct]) && (!opFlags[use_ramp_limits])) {
                    matrixDataValue.assignCheckCol(
                        offset, argLoc, K * stateDataValue.cj * vLimiter->DoutDin());
                } else {
                    matrixDataValue.assign(
                        offset, offset + 1, stateDataValue.cj * vLimiter->DoutDin());
                }
            }
        }
    }
    // Now do the algebraic states if needed
    if ((!opFlags[differential_output]) && (hasAlgebraic(solverModeValue))) {
        auto offset = offsets.getAlgOffset(solverModeValue) + limiter_alg;
        if (hasValueState()) {
            matrixDataValue.assignCheckCol(offset, argLoc, K);
            matrixDataValue.assign(offset, offset, -1.0);
        }
        if (limiter_alg > 0) {
            --offset;
            matrixDataValue.assign(offset, offset, -1.0);
            if (opFlags[use_direct]) {
                matrixDataValue.assign(offset, argLoc, K * vLimiter->DoutDin());
            } else {
                matrixDataValue.assign(offset, offset + 1, vLimiter->DoutDin());
            }
        }
    }
}

void Block::jacobianElements(const IOdata& inputs,
                             const stateData& stateDataValue,
                             matrixData<double>& matrixDataValue,
                             const IOlocs& inputLocs,
                             const solverMode& solverModeValue)
{
    blockJacobianElements(inputs[0],
                          getRateInput(inputs),
                          stateDataValue,
                          matrixDataValue,
                          inputLocs[0],
                          solverModeValue);
}

double Block::getLimiterTestValue(double input,
                                  const stateData& stateDataValue,
                                  const solverMode& solverModeValue)
{
    auto offset = (opFlags[differential_output]) ? offsets.getDiffOffset(solverModeValue) :
                                                   offsets.getAlgOffset(solverModeValue);
    auto stateVal = (stateDataValue.empty()) ? m_state[1] : stateDataValue.state[offset + 1];
    if (hasValueState() || opFlags[use_ramp_limits]) {
        return stateVal;
    }
    return getTestValue(input, stateVal);
}

void Block::rootTest(const IOdata& inputs,
                     const stateData& stateDataValue,
                     double roots[],
                     const solverMode& solverModeValue)
{
    if (!opFlags[has_limits]) {
        return;
    }
    int rootOffset = offsets.getRootOffset(solverModeValue);
    if (opFlags[use_ramp_limits]) {
        auto doffset = offsets.getDiffOffset(solverModeValue) + limiter_diff;
        const double testRate = getTestRate(inputs[1], stateDataValue.dstate_dt[doffset]);
        const double testValue = getTestValue(inputs[0], stateDataValue.state[doffset]);
        roots[rootOffset] =
            rLimiter->limitCheck(stateDataValue.state[doffset], testValue, testRate);
        ++rootOffset;
    }

    if (opFlags[use_block_limits]) {
        const double value = getLimiterTestValue(inputs[0], stateDataValue, solverModeValue);
        roots[rootOffset] = vLimiter->limitCheck(value);
    }
}

change_code Block::rootCheck(const IOdata& inputs,
                             const stateData& stateDataValue,
                             const solverMode& solverModeValue,
                             check_level_t /*level*/)
{
    change_code ret = change_code::no_change;
    if (!opFlags[has_limits]) {
        return ret;
    }
    const double* stateValues =
        ((!stateDataValue.empty()) ? stateDataValue.state : m_state.data());
    const double* stateDerivatives =
        ((!stateDataValue.empty()) ? stateDataValue.dstate_dt : m_dstate_dt.data());
    if (opFlags[use_ramp_limits]) {
        auto doffset = offsets.getDiffOffset(solverModeValue);
        const double testRate = getTestRate(getRateInput(inputs), stateDerivatives[doffset]);
        const double testValue = getTestValue(inputs[0], stateValues[doffset]);
        const double limitValue =
            rLimiter->limitCheck(stateValues[doffset], testValue, testRate);
        if (limitValue < 0.0) {
            rLimiter->changeLimitActivation(testRate);
            ret = change_code::non_state_change;
        }
    }
    if (opFlags[use_block_limits]) {
        const double value = getLimiterTestValue(inputs[0], stateDataValue, solverModeValue);
        const double limitValue = vLimiter->limitCheck(value);
        if (limitValue < 0.0) {
            vLimiter->changeLimitActivation(value);
            ret = change_code::non_state_change;
        }
    }

    return ret;
}

void Block::rootTrigger(coreTime /*time*/,
                        const IOdata& inputs,
                        const std::vector<int>& rootMask,
                        const solverMode& solverModeValue)
{
    if (!opFlags[has_limits]) {
        return;
    }
    auto roffset = offsets.getRootOffset(solverModeValue);

    if (opFlags[use_ramp_limits]) {
        if (rootMask[roffset] != 0) {
            auto doffset = offsets.getDiffOffset(cLocalSolverMode);
            const double testRate = getTestRate(getRateInput(inputs), m_dstate_dt[doffset]);
            rLimiter->changeLimitActivation(testRate);
            // ret = change_code::non_state_change;
        }
        ++roffset;
    }
    if (opFlags[use_block_limits]) {
        if (rootMask[roffset] == 0) {
            return;
        }
        const double value = getLimiterTestValue(
            inputs.empty() ? m_state[0] : inputs[0], emptyStateData, solverModeValue);

        vLimiter->changeLimitActivation(value);
        m_state[0] = vLimiter->output(value);
    }
}

void Block::setFlag(std::string_view flag, bool val)
{
    if (flag == "use_limits") {
        if (!opFlags[dyn_initialized]) {
            opFlags[has_limits] = val;
            opFlags[use_block_limits] = val;
            opFlags[use_ramp_limits] = val;
        }
    } else if (flag == "simplified") {
        if (opFlags[dyn_initialized]) {
            if (opFlags[simplified] != val) {
                // this is probably not the best thing to
                // be changing after initialization
                opFlags[simplified] = val;
                dynObjectInitializeA(prevTime, 0);
                alert(this, STATE_COUNT_CHANGE);
                logging::warning(this,
                                 "changing object state computations during simulation "
                                 "triggers solver reset");
            }
        } else {
            opFlags[simplified] = val;
        }
    } else if (flag == "use_direct") {
        if (!opFlags[dyn_initialized]) {
            opFlags[use_direct] = val;
        }
    } else if (flag == "differential_input") {
        if (!opFlags[dyn_initialized]) {
            opFlags[differential_input] = val;
        }
    } else if (flag == "use_ramp_limits") {
        if (!opFlags[dyn_initialized]) {
            opFlags[use_ramp_limits] = val;
        }
    } else {
        GridSubModel::setFlag(flag, val);
    }
}

// set parameters
void Block::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}
void Block::set(std::string_view param, double val, units::unit unitType)
{
    // param   = gridDynSimulation::toLower(param);

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

double Block::get(std::string_view param, units::unit unitType) const
{
    if (param == "maxstepsize") {
        return kBigNum;
    }
    return GridSubModel::get(param, unitType);
}

void Block::valLimiterUpdate()
{
    if (!opFlags[dyn_initialized]) {
        opFlags.set(use_block_limits);
    } else {
        if (vLimiter) {
            vLimiter->setLimits(Omin, Omax);
        }
    }
}

void Block::rampLimiterUpdate()
{
    if (!opFlags[dyn_initialized]) {
        opFlags.set(use_ramp_limits);
    } else {
        if (rLimiter) {
            rLimiter->setLimits(rampMin, rampMax);
        }
    }
}

stringVec Block::localStateNames() const
{
    stringVec stNames;
    if (opFlags[use_block_limits]) {
        stNames.emplace_back("block_limits");
    }
    if (opFlags[use_ramp_limits]) {
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

std::unique_ptr<Block> make_block(const std::string& blockstr)
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
    std::unique_ptr<Block> ret;
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
        ret = std::make_unique<Block>(gain);
    } else if ((fstr == "der") || (fstr == "derivative")) {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::derivativeBlock>();
        } else {
            ret = std::make_unique<blocks::derivativeBlock>(inputs[0]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if ((fstr == "integral") || (fstr == "integrator")) {
        ret = std::make_unique<blocks::integralBlock>(gain);
    } else if (fstr == "control") {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::controlBlock>();
        } else if (inputs.size() == 1) {
            ret = std::make_unique<blocks::controlBlock>(inputs[0]);
        } else {
            ret = std::make_unique<blocks::controlBlock>(inputs[0], inputs[1]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "delay") {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::delayBlock>();
        } else {
            ret = std::make_unique<blocks::delayBlock>(inputs[0]);
        }
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "deadband") {
        if (inputs.empty()) {
            ret = std::make_unique<blocks::deadbandBlock>();
        } else {
            ret = std::make_unique<blocks::deadbandBlock>(inputs[0]);
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

        ret = std::make_unique<blocks::pidBlock>(
            proportionalGain, integralGain, derivativeGain);
        if (gain != 1.0) {
            ret->set("gain", gain);
        }
    } else if (fstr == "function") {
        if (argstr.empty()) {
            ret = std::make_unique<blocks::functionBlock>();
        } else {
            ret = std::make_unique<blocks::functionBlock>(std::string{argstr});
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
