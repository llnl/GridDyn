/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "deadbandBlock.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace griddyn::blocks {
deadbandBlock::deadbandBlock(const std::string& objName): Block(objName)
{
    opFlags.set(use_state);
}
deadbandBlock::deadbandBlock(double db, const std::string& objName):
    Block(objName), mDeadbandHigh(db), mDeadbandLow(-db)
{
    opFlags.set(use_state);
}

coreObject* deadbandBlock::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<deadbandBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mDeadbandHigh = mDeadbandHigh;
    nobj->mDeadbandLow = mDeadbandLow;
    nobj->mDeadbandLevel = mDeadbandLevel;
    nobj->mRampUpBand = mRampUpBand;
    nobj->mRampDownBand = mRampDownBand;
    nobj->mResetHigh = mResetHigh;
    nobj->mResetLow = mResetLow;
    return nobj;
}

void deadbandBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    Block::dynObjectInitializeA(time0, flags);
    if (mDeadbandLow < mDeadbandHigh)  // this means it was set to some value
    {
        opFlags[has_roots] = true;
        offsets.local().local.algRoots++;
        opFlags.set(has_alg_roots);
        opFlags[uses_deadband] = true;
    }
    if (mResetHigh < -kHalfBigNum)  // this means we are using default
    {
        mResetHigh = mDeadbandHigh - 1e-6;
    }
    if (mResetLow > kHalfBigNum)  // this means we are using default
    {
        mResetLow = mDeadbandLow + 1e-6;
    }
}
// initial conditions
void deadbandBlock::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        m_state[limiter_alg] = mDeadbandLevel;
        rootCheck(inputs, emptyStateData, cLocalSolverMode, check_level_t::reversable_only);
        m_state[limiter_alg] = K * computeValue(inputs[0] + bias);
        if (limiter_alg > 0) {
            Block::rootCheck(inputs,
                             emptyStateData,
                             cLocalSolverMode,
                             check_level_t::reversable_only);
        }
    } else {
        fieldSet.resize(1);
        if (limiter_alg > 0) {
            Block::rootCheck(inputs,
                             emptyStateData,
                             cLocalSolverMode,
                             check_level_t::reversable_only);
        }
        mDeadbandState = deadbandstate_t::normal;
        double ival = m_state[limiter_alg] / K;
        if (std::abs(ival - mDeadbandLevel) > 1e-6) {
            if (opFlags[uses_shiftedoutput]) {
                mDeadbandState = deadbandstate_t::shifted;
                if (ival > mDeadbandLevel) {
                    fieldSet[0] = (mDeadbandHigh - mDeadbandLevel) + ival;
                } else {
                    fieldSet[0] = ival - (mDeadbandLevel - mDeadbandLow);
                }
            } else if ((ival > mDeadbandHigh + mRampUpBand) ||
                       (ival < mDeadbandLow - mRampUpBand)) {
                mDeadbandState = deadbandstate_t::outside;
                fieldSet[0] = ival;
            } else if (mRampUpBand > 0) {
                mDeadbandState = deadbandstate_t::rampup;
                if (ival > mDeadbandLevel) {
                    fieldSet[0] = mDeadbandHigh +
                        (ival - mDeadbandLevel) / (mDeadbandHigh + mRampUpBand - mDeadbandLevel) *
                            mRampUpBand;
                } else {
                    fieldSet[0] = mDeadbandLow -
                        (mDeadbandLevel - ival) / (mDeadbandLevel - mDeadbandLow - mRampUpBand) *
                            mRampUpBand;
                }
            } else {
                mDeadbandState = deadbandstate_t::outside;
                fieldSet[0] = ival;
            }
        } else {
            fieldSet[0] = mDeadbandLevel;
        }
        fieldSet[0] -= bias;
    }
}

double deadbandBlock::computeValue(double input) const
{
    double out = input;
    switch (mDeadbandState) {
        case deadbandstate_t::normal:
            out = mDeadbandLevel;
            break;
        case deadbandstate_t::outside:
            out = input;
            break;
        case deadbandstate_t::shifted:
            if (input > mDeadbandLevel) {
                out = input - (mDeadbandHigh - mDeadbandLevel);
            } else {
                out = input + (mDeadbandLevel - mDeadbandLow);
            }
            break;
        case deadbandstate_t::rampup:
            if (input > mDeadbandLevel) {
                double transitionBand = mDeadbandHigh - mDeadbandLevel + mRampUpBand;
                out = mDeadbandLevel + (input - mDeadbandHigh) / mRampUpBand * transitionBand;
            } else {
                double transitionBand = mDeadbandLevel - mDeadbandLow + mRampUpBand;
                out = mDeadbandLevel - (mDeadbandLow - input) / mRampUpBand * transitionBand;
            }
            break;
        case deadbandstate_t::rampdown:
            if (input > mDeadbandLevel) {
                double transitionBand = mDeadbandHigh - mDeadbandLevel + mRampDownBand;
                out = mDeadbandLevel + (input - mDeadbandHigh) / mRampDownBand * transitionBand;
            } else {
                double transitionBand = mDeadbandLevel - mDeadbandLow + mRampDownBand;
                out = mDeadbandLevel - (mDeadbandLow - input) / mRampDownBand * transitionBand;
            }
            break;
    }
    return out;
}

double deadbandBlock::computeDoutDin(double input) const
{
    double out = 0.0;
    switch (mDeadbandState) {
        case deadbandstate_t::normal:
            break;
        case deadbandstate_t::outside:
        case deadbandstate_t::shifted:
            out = 1.0;
            break;
        case deadbandstate_t::rampup:
            if (input > mDeadbandLevel) {
                double transitionBand = mDeadbandHigh - mDeadbandLevel + mRampUpBand;
                out = transitionBand / mRampUpBand;
            } else {
                double transitionBand = mDeadbandLevel - mDeadbandLow + mRampUpBand;
                out = transitionBand / mRampUpBand;
            }
            break;
        case deadbandstate_t::rampdown:
            if (input > mDeadbandLevel) {
                double transitionBand = mDeadbandHigh - mDeadbandLevel + mRampDownBand;
                out = transitionBand / mRampDownBand;
            } else {
                double transitionBand = mDeadbandLevel - mDeadbandLow + mRampDownBand;
                out = transitionBand / mRampDownBand;
            }
            break;
    }
    return out;
}
double deadbandBlock::step(coreTime time, double input)
{
    rootCheck({input}, emptyStateData, cLocalSolverMode, check_level_t::reversable_only);
    m_state[limiter_alg] = K * computeValue(input + bias);
    if (limiter_alg > 0) {
        Block::step(time, input);
    } else {
        prevTime = time;
        m_output = m_state[0];
    }
    // printf("deadband input=%f, step out=%f, check out =%f\n",input, m_state[0],
    // m_state[limiter_alg]);
    return m_state[0];
}

void deadbandBlock::blockDerivative(double input,
                                    double didt,
                                    const stateData& sD,
                                    double deriv[],
                                    const solverMode& sMode)
{
    if (opFlags[differential_input]) {
        auto offset = offsets.getDiffOffset(sMode) + limiter_diff;
        double inputWithBias = input + bias;
        deriv[offset] = K * computeDoutDin(inputWithBias) * didt;

        if (limiter_diff > 0) {
            return Block::blockDerivative(input, didt, sD, deriv, sMode);
        }
    }
}

void deadbandBlock::blockAlgebraicUpdate(double input,
                                         const stateData& sD,
                                         double update[],
                                         const solverMode& sMode)
{
    if (!opFlags[differential_input]) {
        auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
        double inputWithBias = input + bias;
        update[offset] = K * computeValue(inputWithBias);
        // printf("db %f input=%f val=%f dbstate=%d\n", sD.time, ival,
        // update[offset], static_cast<int>(dbstate));
        if (limiter_alg > 0) {
            return Block::blockAlgebraicUpdate(input, sD, update, sMode);
        }
    }
}

void deadbandBlock::blockJacobianElements(double input,
                                          double didt,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          index_t argLoc,
                                          const solverMode& sMode)
{
    if ((!opFlags[differential_input]) && (hasAlgebraic(sMode))) {
        auto offset = offsets.getAlgOffset(sMode) + limiter_alg;
        md.assign(offset, offset, -1.0);
        double dInputOutput = K * computeDoutDin(input + bias);

        if (argLoc != kNullLocation) {
            md.assign(offset, argLoc, dInputOutput);
        }
        if (limiter_alg > 0) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }
    } else if ((opFlags[differential_input]) && (hasDifferential(sMode))) {
        auto offset = offsets.getDiffOffset(sMode) + limiter_diff;
        md.assign(offset, offset, -sD.cj);
        double dInputOutput = K * computeDoutDin(input + bias);

        if (argLoc != kNullLocation) {
            md.assign(offset, argLoc, dInputOutput * sD.cj);
        }

        if (limiter_diff > 0) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }
    }
}

void deadbandBlock::rootTest(const IOdata& inputs,
                             const stateData& sD,
                             double roots[],
                             const solverMode& sMode)
{
    if (limiter_alg + limiter_diff > 0) {
        Block::rootTest(inputs, sD, roots, sMode);
    }
    if (opFlags[uses_deadband]) {
        int rootOffset = offsets.getRootOffset(sMode) + limiter_alg + limiter_diff;

        double inputWithBias = inputs[0] + bias;
        // double prevInput = ival;
        switch (mDeadbandState) {
            case deadbandstate_t::normal:
                roots[rootOffset] =
                    std::min(mDeadbandHigh - inputWithBias, inputWithBias - mDeadbandLow);
                if (inputWithBias > mDeadbandHigh) {
                    opFlags.set(dbtrigger_high);
                }
                break;
            case deadbandstate_t::outside:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] = inputWithBias - mResetHigh + mTolerance;
                } else {
                    roots[rootOffset] = mResetLow - inputWithBias + mTolerance;
                }
                break;
            case deadbandstate_t::shifted:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] = inputWithBias - mDeadbandHigh + mTolerance;
                } else {
                    roots[rootOffset] = mDeadbandLow - inputWithBias + mTolerance;
                }
                break;

            case deadbandstate_t::rampup:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] = std::min(mDeadbandHigh + mRampUpBand - inputWithBias,
                                                 inputWithBias - mDeadbandHigh) +
                        mTolerance;
                } else {
                    roots[rootOffset] = std::min(mDeadbandLow - inputWithBias,
                                                 inputWithBias - mDeadbandLow - mRampUpBand) +
                        mTolerance;
                }
                break;
            case deadbandstate_t::rampdown:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] = std::min(inputWithBias - mResetHigh - mRampDownBand,
                                                 mResetHigh - inputWithBias) +
                        mTolerance;
                } else {
                    roots[rootOffset] = std::min(inputWithBias - mResetLow,
                                                 mResetLow + mRampDownBand - inputWithBias) +
                        mTolerance;
                }
                break;
        }
    }
}

void deadbandBlock::rootTrigger(coreTime time,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const solverMode& sMode)
{
    auto rootOffset = offsets.getRootOffset(sMode);
    if (limiter_alg + limiter_diff > 0) {
        if (rootMask[rootOffset] != 0) {
            Block::rootTrigger(time, inputs, rootMask, sMode);
        } else if (rootMask[rootOffset + limiter_alg + limiter_diff - 1] != 0) {
            Block::rootTrigger(time, inputs, rootMask, sMode);
        }
        rootOffset += limiter_alg + limiter_diff;
    }
    // auto cstate = mDeadbandState;
    if (opFlags[uses_deadband]) {
        if (rootMask[rootOffset] == 0) {
            return;
        }

        switch (mDeadbandState) {
            case deadbandstate_t::normal:
                if (opFlags[uses_shiftedoutput]) {
                    mDeadbandState = deadbandstate_t::shifted;
                } else if (mRampUpBand > 0.0) {
                    mDeadbandState = deadbandstate_t::rampup;
                } else {
                    mDeadbandState = deadbandstate_t::outside;
                }
                break;
            case deadbandstate_t::outside:
                if (mRampDownBand > 0.0) {
                    mDeadbandState = deadbandstate_t::rampdown;
                } else {
                    mDeadbandState = deadbandstate_t::normal;
                }
                break;
            case deadbandstate_t::shifted:
                mDeadbandState = deadbandstate_t::normal;
                break;

            case deadbandstate_t::rampup:
                if ((prevInput >= mDeadbandHigh + mRampUpBand) ||
                    (prevInput <= mDeadbandLow - mRampUpBand)) {
                    mDeadbandState = deadbandstate_t::outside;
                } else {
                    mDeadbandState = deadbandstate_t::normal;
                }
                break;
            case deadbandstate_t::rampdown:
                if ((prevInput >= mResetHigh) || (prevInput <= mResetLow)) {
                    mDeadbandState = deadbandstate_t::outside;
                } else {
                    mDeadbandState = deadbandstate_t::normal;
                }
                break;
        }
        if (mDeadbandState == deadbandstate_t::normal) {
            opFlags.reset(dbtrigger_high);
        }
        m_state[limiter_alg] = computeValue(prevInput);
        // if (dbstate != cstate)
        // {
        //      printf("rt--%f, %d::change deadband state from %d to %d\n",
        // static_cast<double>(time), getUserID(), static_cast<int>(cstate),
        // static_cast<int>(dbstate));
        // }
    }
}

change_code deadbandBlock::rootCheck(const IOdata& inputs,
                                     const stateData& sD,
                                     const solverMode& sMode,
                                     check_level_t /*level*/)
{
    change_code ret = change_code::no_change;
    auto currentState = mDeadbandState;
    if (opFlags[uses_deadband]) {
        double inputWithBias = inputs[0] + bias;
        switch (mDeadbandState) {
            case deadbandstate_t::normal:
                if (std::min(mDeadbandHigh - inputWithBias, inputWithBias - mDeadbandLow) <
                    -mTolerance) {
                    if (opFlags[uses_shiftedoutput]) {
                        mDeadbandState = deadbandstate_t::shifted;
                    } else if (mRampUpBand > 0.0) {
                        mDeadbandState = deadbandstate_t::rampup;
                    } else {
                        mDeadbandState = deadbandstate_t::outside;
                    }
                    if (inputWithBias >= mDeadbandHigh) {
                        opFlags.set(dbtrigger_high);
                    } else {
                        opFlags.reset(dbtrigger_high);
                    }
                }
                break;
            case deadbandstate_t::outside:
                if (opFlags[dbtrigger_high]) {
                    if (inputWithBias < mResetHigh) {
                        if (mRampDownBand > 0.0) {
                            if (inputWithBias < mResetHigh - mRampDownBand) {
                                mDeadbandState = deadbandstate_t::normal;
                            } else {
                                mDeadbandState = deadbandstate_t::rampdown;
                            }
                        } else {
                            mDeadbandState = deadbandstate_t::normal;
                        }
                    }
                } else {
                    if (inputWithBias > mResetLow) {
                        if (mRampDownBand > 0.0) {
                            if (inputWithBias > mResetLow + mRampDownBand) {
                                mDeadbandState = deadbandstate_t::normal;
                            } else {
                                mDeadbandState = deadbandstate_t::rampdown;
                            }
                        } else {
                            mDeadbandState = deadbandstate_t::normal;
                        }
                    }
                }
                break;
            case deadbandstate_t::shifted:
                if ((inputWithBias < mDeadbandHigh - mTolerance) &&
                    (inputWithBias > mDeadbandLow + mTolerance)) {
                    mDeadbandState = deadbandstate_t::normal;
                }
                break;

            case deadbandstate_t::rampup:
                if (opFlags[dbtrigger_high]) {
                    if (inputWithBias > mDeadbandHigh + mRampUpBand + mTolerance) {
                        mDeadbandState = deadbandstate_t::outside;
                    } else if (inputWithBias < mDeadbandHigh - mTolerance) {
                        mDeadbandState = deadbandstate_t::normal;
                    }
                } else {
                    if (inputWithBias < mDeadbandLow - mRampUpBand - mTolerance) {
                        mDeadbandState = deadbandstate_t::outside;
                    } else if (inputWithBias > mDeadbandLow + mTolerance) {
                        mDeadbandState = deadbandstate_t::normal;
                        ret = change_code::parameter_change;
                    }
                }
                break;
            case deadbandstate_t::rampdown:
                if (opFlags[dbtrigger_high]) {
                    if (inputWithBias > mDeadbandHigh + mRampDownBand + mTolerance) {
                        mDeadbandState = deadbandstate_t::outside;
                    } else if (inputWithBias < mDeadbandHigh - mTolerance) {
                        mDeadbandState = deadbandstate_t::normal;
                    }
                } else {
                    if (inputWithBias < mDeadbandLow - mRampDownBand - mTolerance) {
                        mDeadbandState = deadbandstate_t::outside;
                    } else if (inputWithBias > mDeadbandLow + mTolerance) {
                        mDeadbandState = deadbandstate_t::normal;
                    }
                }
                break;
        }
        if (mDeadbandState != currentState) {
            ret = change_code::parameter_change;
            //    printf("%f, %d::change deadband state from %d to %d from %f\n",
            // static_cast<double>(sD.time), getUserID(), static_cast<int>(cstate),
            // static_cast<int>(dbstate), ival);
        }
    }

    if (limiter_alg > 0) {
        auto iret = Block::rootCheck(inputs, sD, sMode, check_level_t::reversable_only);
        ret = std::max(ret, iret);
    }
    if (currentState != mDeadbandState) {
        // we may run through multiple categories so we need to
        // do this recursively
        auto iret = rootCheck(inputs, sD, sMode, check_level_t::reversable_only);
        ret = std::max(ret, iret);
    }
    return ret;
}

void deadbandBlock::setFlag(std::string_view flag, bool val)
{
    if (flag == "shifted") {
        opFlags.set(uses_shiftedoutput, val);
    } else if (flag == "unshifted") {
        opFlags.set(uses_shiftedoutput, !val);
    } else if (flag == "no_down_deadband") {
        mResetLow = mDeadbandLevel;
        mResetHigh = mDeadbandLevel;
    } else {
        Block::setFlag(flag, val);
    }
}
// set parameters
void deadbandBlock::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Block::set(param, val);
    }
}

void deadbandBlock::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "level") || (param == "dblevel") || (param == "deadbandlevel")) {
        mDeadbandLevel = val;
    } else if ((param == "deadband") || (param == "db")) {
        mDeadbandHigh = mDeadbandLevel + val;
        mDeadbandLow = mDeadbandLevel - val;
    } else if ((param == "deadbandhigh") || (param == "dbhigh") || (param == "high")) {
        if (val > mDeadbandLevel) {
            mDeadbandHigh = val;
        } else {
            mDeadbandHigh = mDeadbandLevel + val;
        }
    } else if (param == "tolerance") {
        mTolerance = val;
    } else if ((param == "deadbandlow") || (param == "dblow") || (param == "low")) {
        if (val < mDeadbandLevel) {
            mDeadbandLow = val;
        } else {
            mDeadbandLow = mDeadbandLevel - val;
        }
    } else if ((param == "rampband") || (param == "ramp")) {
        mRampUpBand = val;
        mRampDownBand = val;
    } else if ((param == "rampupband") || (param == "rampup")) {
        mRampUpBand = val;
    } else if ((param == "rampdownband") || (param == "rampdown")) {
        mRampDownBand = val;
    } else if ((param == "resetlevel") || (param == "reset")) {
        mResetHigh = mDeadbandLevel + val;
        mResetLow = mDeadbandLevel - val;
    } else if (param == "resethigh") {
        mResetHigh = val;
    } else if (param == "resetlow") {
        mResetLow = val;
    } else {
        Block::set(param, val, unitType);
    }
}
}  // namespace griddyn::blocks
