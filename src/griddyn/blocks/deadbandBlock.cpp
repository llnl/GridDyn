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
    Block(objName), deadbandHigh(db), deadbandLow(-db)
{
    opFlags.set(use_state);
}

coreObject* deadbandBlock::clone(coreObject* obj) const
{
    auto nobj = cloneBase<deadbandBlock, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->deadbandHigh = deadbandHigh;
    nobj->deadbandLow = deadbandLow;
    nobj->deadbandLevel = deadbandLevel;
    nobj->rampUpband = rampUpband;
    nobj->rampDownband = rampDownband;
    nobj->resetHigh = resetHigh;
    nobj->resetLow = resetLow;
    return nobj;
}

void deadbandBlock::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    Block::dynObjectInitializeA(time0, flags);
    if (deadbandLow < deadbandHigh)  // this means it was set to some value
    {
        opFlags[has_roots] = true;
        offsets.local().local.algRoots++;
        opFlags.set(has_alg_roots);
        opFlags[uses_deadband] = true;
    }
    if (resetHigh < -kHalfBigNum)  // this means we are using default
    {
        resetHigh = deadbandHigh - 1e-6;
    }
    if (resetLow > kHalfBigNum)  // this means we are using default
    {
        resetLow = deadbandLow + 1e-6;
    }
}
// initial conditions
void deadbandBlock::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        m_state[limiter_alg] = deadbandLevel;
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
        dbstate = deadbandstate_t::normal;
        double ival = m_state[limiter_alg] / K;
        if (std::abs(ival - deadbandLevel) > 1e-6) {
            if (opFlags[uses_shiftedoutput]) {
                dbstate = deadbandstate_t::shifted;
                if (ival > deadbandLevel) {
                    fieldSet[0] = (deadbandHigh - deadbandLevel) + ival;
                } else {
                    fieldSet[0] = ival - (deadbandLevel - deadbandLow);
                }
            } else if ((ival > deadbandHigh + rampUpband) || (ival < deadbandLow - rampUpband)) {
                dbstate = deadbandstate_t::outside;
                fieldSet[0] = ival;
            } else if (rampUpband > 0) {
                dbstate = deadbandstate_t::rampup;
                if (ival > deadbandLevel) {
                    fieldSet[0] = deadbandHigh +
                        (ival - deadbandLevel) / (deadbandHigh + rampUpband - deadbandLevel) *
                            rampUpband;
                } else {
                    fieldSet[0] = deadbandLow -
                        (deadbandLevel - ival) / (deadbandLevel - deadbandLow - rampUpband) *
                            rampUpband;
                }
            } else {
                dbstate = deadbandstate_t::outside;
                fieldSet[0] = ival;
            }
        } else {
            fieldSet[0] = deadbandLevel;
        }
        fieldSet[0] -= bias;
    }
}

double deadbandBlock::computeValue(double input) const
{
    double out = input;
    switch (dbstate) {
        case deadbandstate_t::normal:
            out = deadbandLevel;
            break;
        case deadbandstate_t::outside:
            out = input;
            break;
        case deadbandstate_t::shifted:
            if (input > deadbandLevel) {
                out = input - (deadbandHigh - deadbandLevel);
            } else {
                out = input + (deadbandLevel - deadbandLow);
            }
            break;
        case deadbandstate_t::rampup:
            if (input > deadbandLevel) {
                double tband = deadbandHigh - deadbandLevel + rampUpband;
                out = deadbandLevel + (input - deadbandHigh) / rampUpband * tband;
            } else {
                double tband = deadbandLevel - deadbandLow + rampUpband;
                out = deadbandLevel - (deadbandLow - input) / rampUpband * tband;
            }
            break;
        case deadbandstate_t::rampdown:
            if (input > deadbandLevel) {
                double tband = deadbandHigh - deadbandLevel + rampDownband;
                out = deadbandLevel + (input - deadbandHigh) / rampDownband * tband;
            } else {
                double tband = deadbandLevel - deadbandLow + rampDownband;
                out = deadbandLevel - (deadbandLow - input) / rampDownband * tband;
            }
            break;
    }
    return out;
}

double deadbandBlock::computeDoutDin(double input) const
{
    double out = 0.0;
    switch (dbstate) {
        case deadbandstate_t::normal:
            break;
        case deadbandstate_t::outside:
        case deadbandstate_t::shifted:
            out = 1.0;
            break;
        case deadbandstate_t::rampup:
            if (input > deadbandLevel) {
                double tband = deadbandHigh - deadbandLevel + rampUpband;
                out = tband / rampUpband;
            } else {
                double tband = deadbandLevel - deadbandLow + rampUpband;
                out = tband / rampUpband;
            }
            break;
        case deadbandstate_t::rampdown:
            if (input > deadbandLevel) {
                double tband = deadbandHigh - deadbandLevel + rampDownband;
                out = tband / rampDownband;
            } else {
                double tband = deadbandLevel - deadbandLow + rampDownband;
                out = tband / rampDownband;
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
        double ival = input + bias;
        deriv[offset] = K * computeDoutDin(ival) * didt;

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
        double ival = input + bias;
        update[offset] = K * computeValue(ival);
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
        double dido = K * computeDoutDin(input + bias);

        if (argLoc != kNullLocation) {
            md.assign(offset, argLoc, dido);
        }
        if (limiter_alg > 0) {
            Block::blockJacobianElements(input, didt, sD, md, argLoc, sMode);
        }
    } else if ((opFlags[differential_input]) && (hasDifferential(sMode))) {
        auto offset = offsets.getDiffOffset(sMode) + limiter_diff;
        md.assign(offset, offset, -sD.cj);
        double dido = K * computeDoutDin(input + bias);

        if (argLoc != kNullLocation) {
            md.assign(offset, argLoc, dido * sD.cj);
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

        double ival = inputs[0] + bias;
        // double prevInput = ival;
        switch (dbstate) {
            case deadbandstate_t::normal:
                roots[rootOffset] = std::min(deadbandHigh - ival, ival - deadbandLow);
                if (ival > deadbandHigh) {
                    opFlags.set(dbtrigger_high);
                }
                break;
            case deadbandstate_t::outside:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] = ival - resetHigh + tolerance;
                } else {
                    roots[rootOffset] = resetLow - ival + tolerance;
                }
                break;
            case deadbandstate_t::shifted:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] = ival - deadbandHigh + tolerance;
                } else {
                    roots[rootOffset] = deadbandLow - ival + tolerance;
                }
                break;

            case deadbandstate_t::rampup:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] =
                        std::min(deadbandHigh + rampUpband - ival, ival - deadbandHigh) + tolerance;
                } else {
                    roots[rootOffset] =
                        std::min(deadbandLow - ival, ival - deadbandLow - rampUpband) + tolerance;
                }
                break;
            case deadbandstate_t::rampdown:
                if (opFlags[dbtrigger_high]) {
                    roots[rootOffset] =
                        std::min(ival - resetHigh - rampDownband, resetHigh - ival) + tolerance;
                } else {
                    roots[rootOffset] =
                        std::min(ival - resetLow, resetLow + rampDownband - ival) + tolerance;
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
    // auto cstate = dbstate;
    if (opFlags[uses_deadband]) {
        if (rootMask[rootOffset] == 0) {
            return;
        }

        switch (dbstate) {
            case deadbandstate_t::normal:
                if (opFlags[uses_shiftedoutput]) {
                    dbstate = deadbandstate_t::shifted;
                } else if (rampUpband > 0.0) {
                    dbstate = deadbandstate_t::rampup;
                } else {
                    dbstate = deadbandstate_t::outside;
                }
                break;
            case deadbandstate_t::outside:
                if (rampDownband > 0.0) {
                    dbstate = deadbandstate_t::rampdown;
                } else {
                    dbstate = deadbandstate_t::normal;
                }
                break;
            case deadbandstate_t::shifted:
                dbstate = deadbandstate_t::normal;
                break;

            case deadbandstate_t::rampup:
                if ((prevInput >= deadbandHigh + rampUpband) ||
                    (prevInput <= deadbandLow - rampUpband)) {
                    dbstate = deadbandstate_t::outside;
                } else {
                    dbstate = deadbandstate_t::normal;
                }
                break;
            case deadbandstate_t::rampdown:
                if ((prevInput >= resetHigh) || (prevInput <= resetLow)) {
                    dbstate = deadbandstate_t::outside;
                } else {
                    dbstate = deadbandstate_t::normal;
                }
                break;
        }
        if (dbstate == deadbandstate_t::normal) {
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
    auto cstate = dbstate;
    if (opFlags[uses_deadband]) {
        double ival = inputs[0] + bias;
        switch (dbstate) {
            case deadbandstate_t::normal:
                if (std::min(deadbandHigh - ival, ival - deadbandLow) < -tolerance) {
                    if (opFlags[uses_shiftedoutput]) {
                        dbstate = deadbandstate_t::shifted;
                    } else if (rampUpband > 0.0) {
                        dbstate = deadbandstate_t::rampup;
                    } else {
                        dbstate = deadbandstate_t::outside;
                    }
                    if (ival >= deadbandHigh) {
                        opFlags.set(dbtrigger_high);
                    } else {
                        opFlags.reset(dbtrigger_high);
                    }
                }
                break;
            case deadbandstate_t::outside:
                if (opFlags[dbtrigger_high]) {
                    if (ival < resetHigh) {
                        if (rampDownband > 0.0) {
                            if (ival < resetHigh - rampDownband) {
                                dbstate = deadbandstate_t::normal;
                            } else {
                                dbstate = deadbandstate_t::rampdown;
                            }
                        } else {
                            dbstate = deadbandstate_t::normal;
                        }
                    }
                } else {
                    if (ival > resetLow) {
                        if (rampDownband > 0.0) {
                            if (ival > resetLow + rampDownband) {
                                dbstate = deadbandstate_t::normal;
                            } else {
                                dbstate = deadbandstate_t::rampdown;
                            }
                        } else {
                            dbstate = deadbandstate_t::normal;
                        }
                    }
                }
                break;
            case deadbandstate_t::shifted:
                if ((ival < deadbandHigh - tolerance) && (ival > deadbandLow + tolerance)) {
                    dbstate = deadbandstate_t::normal;
                }
                break;

            case deadbandstate_t::rampup:
                if (opFlags[dbtrigger_high]) {
                    if (ival > deadbandHigh + rampUpband + tolerance) {
                        dbstate = deadbandstate_t::outside;
                    } else if (ival < deadbandHigh - tolerance) {
                        dbstate = deadbandstate_t::normal;
                    }
                } else {
                    if (ival < deadbandLow - rampUpband - tolerance) {
                        dbstate = deadbandstate_t::outside;
                    } else if (ival > deadbandLow + tolerance) {
                        dbstate = deadbandstate_t::normal;
                        ret = change_code::parameter_change;
                    }
                }
                break;
            case deadbandstate_t::rampdown:
                if (opFlags[dbtrigger_high]) {
                    if (ival > deadbandHigh + rampDownband + tolerance) {
                        dbstate = deadbandstate_t::outside;
                    } else if (ival < deadbandHigh - tolerance) {
                        dbstate = deadbandstate_t::normal;
                    }
                } else {
                    if (ival < deadbandLow - rampDownband - tolerance) {
                        dbstate = deadbandstate_t::outside;
                    } else if (ival > deadbandLow + tolerance) {
                        dbstate = deadbandstate_t::normal;
                    }
                }
                break;
        }
        if (dbstate != cstate) {
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
    if (cstate != dbstate) {
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
        resetLow = deadbandLevel;
        resetHigh = deadbandLevel;
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
        deadbandLevel = val;
    } else if ((param == "deadband") || (param == "db")) {
        deadbandHigh = deadbandLevel + val;
        deadbandLow = deadbandLevel - val;
    } else if ((param == "deadbandhigh") || (param == "dbhigh") || (param == "high")) {
        if (val > deadbandLevel) {
            deadbandHigh = val;
        } else {
            deadbandHigh = deadbandLevel + val;
        }
    } else if (param == "tolerance") {
        tolerance = val;
    } else if ((param == "deadbandlow") || (param == "dblow") || (param == "low")) {
        if (val < deadbandLevel) {
            deadbandLow = val;
        } else {
            deadbandLow = deadbandLevel - val;
        }
    } else if ((param == "rampband") || (param == "ramp")) {
        rampUpband = val;
        rampDownband = val;
    } else if ((param == "rampupband") || (param == "rampup")) {
        rampUpband = val;
    } else if ((param == "rampdownband") || (param == "rampdown")) {
        rampDownband = val;
    } else if ((param == "resetlevel") || (param == "reset")) {
        resetHigh = deadbandLevel + val;
        resetLow = deadbandLevel - val;
    } else if (param == "resethigh") {
        resetHigh = val;
    } else if (param == "resetlow") {
        resetLow = val;
    } else {
        Block::set(param, val, unitType);
    }
}
}  // namespace griddyn::blocks
