/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "blockSequence.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <compare>
#include <string>
#include <vector>

namespace griddyn::blocks {
blockSequence::blockSequence(const std::string& objName): Block(objName)
{
    opFlags[use_direct] = true;
}
CoreObject* blockSequence::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<blockSequence, Block>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void blockSequence::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    bool diffInput = opFlags[DIFFERENTIAL_INPUT_ACTUAL];
    if (sequence.empty()) {  // create a default sequence with all the blocks
        for (index_t kk = 0; std::cmp_less(kk, blocks.size()); ++kk) {
            sequence.push_back(kk);
        }
    }
    for (auto sequenceIndex : sequence) {
        if (diffInput) {
            blocks[sequenceIndex]->setFlag("differential_input", true);
        }
        blocks[sequenceIndex]->dynInitializeA(time0, flags);
        diffInput = blocks[sequenceIndex]->checkFlag(differential_output);
    }
    opFlags[differential_input] = diffInput;
    Block::dynObjectInitializeA(time0, flags);
    updateFlags();  // update the flags for the subObjects;
}
// initial conditions
void blockSequence::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    auto cnt = sequence.size();
    if (desiredOutput.empty()) {
        IOdata inAct{!inputs.empty() ? inputs[0] + bias : 0.0, getRateInput(inputs)};

        for (decltype(cnt) ii = 0; ii < cnt; ++ii) {
            // The second argument is intentionally noInputs because the sequence bootstraps its
            // desired output from the running intermediate state.
            // NOLINTNEXTLINE(readability-suspicious-call-argument)
            blocks[sequence[ii]]->dynInitializeB(inAct, noInputs, inAct);
        }
        Block::dynObjectInitializeB(inAct, desiredOutput, fieldSet);
    } else if (inputs.empty()) {
        IOdata inReq;
        inReq.resize(2);
        Block::dynObjectInitializeB(noInputs, desiredOutput, inReq);
        for (auto ii = static_cast<int>(cnt - 1); ii >= 0; --ii) {
            blocks[sequence[ii]]->dynInitializeB(noInputs, inReq, inReq);
        }
        fieldSet = inReq;
        fieldSet[0] -= bias;
    } else {  // we have inputs and outputs
        fieldSet = desiredOutput;  // the field is the output
    }
}

void blockSequence::add(CoreObject* obj)
{
    if (dynamic_cast<Block*>(obj) != nullptr) {
        add(static_cast<Block*>(obj));
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void blockSequence::add(Block* blk)
{
    if (blk->locIndex == kNullLocation) {
        blk->locIndex = static_cast<index_t>(blocks.size());
    }

    if (std::cmp_less(blocks.size(), blk->locIndex)) {
        blocks.resize(blk->locIndex + 1);
    }
    if (blocks[blk->locIndex] != nullptr) {
        remove(blocks[blk->locIndex]);
    }
    blocks[blk->locIndex] = blk;
    addSubObject(blk);
}

void blockSequence::updateLocalCache(const IOdata& /*inputs*/,
                                     const stateData& stateData,
                                     const solverMode& sMode)
{
    if (!stateData.updateRequired(seqID)) {
        return;
    }
    const auto sequenceSize = sequence.size();
    if (blockOutputs.size() != sequenceSize) {
        blockOutputs.resize(sequenceSize);
        blockDoutDt.resize(sequenceSize);
    }
    for (index_t kk = 0; std::cmp_less(kk, sequenceSize); ++kk) {
        const Block* block = blocks[sequence[kk]];
        blockOutputs[kk] = block->getBlockOutput(stateData, sMode);
        blockDoutDt[kk] =
            (block->checkFlag(differential_output)) ? block->getBlockDoutDt(stateData, sMode) : 0.0;
    }

    seqID = stateData.seqID;
}

double blockSequence::step(coreTime time, double input)
{
    // compute a core sample time then cycle through all the objects at that
    // sampling rate
    input = input + bias;
    const double drate = (input - prevInput) / (time - prevTime);
    while (prevTime < time) {
        const coreTime newTime = std::min(time, prevTime + sampleTime);
        double blockInput = prevInput + drate * (newTime - prevTime);
        for (auto& blkIn : sequence) {
            blockInput = blocks[blkIn]->step(newTime, blockInput);
        }
        prevTime = newTime;
    }
    return Block::step(time, input);
}

void blockSequence::blockResidual(double input,
                                  double didt,
                                  const stateData& stateData,
                                  double resid[],
                                  const solverMode& sMode)
{
    updateLocalCache(noInputs, stateData, sMode);
    auto cnt = static_cast<int>(sequence.size());
    double blockInput = input + bias;
    double indt = didt;
    for (int ii = 0; ii < cnt; ++ii) {
        blocks[sequence[ii]]->blockResidual(blockInput, indt, stateData, resid, sMode);
        blockInput = blockOutputs[sequence[ii]];
        indt = blockDoutDt[sequence[ii]];
    }
    limiterResidElements(blockInput, indt, stateData, resid, sMode);
}

void blockSequence::blockDerivative(double input,
                                    double didt,
                                    const stateData& stateData,
                                    double deriv[],
                                    const solverMode& sMode)
{
    updateLocalCache(noInputs, stateData, sMode);
    auto cnt = static_cast<int>(sequence.size());
    double blockInput = input + bias;
    double indt = didt;
    for (int ii = 0; ii < cnt; ++ii) {
        blocks[sequence[ii]]->blockDerivative(blockInput, indt, stateData, deriv, sMode);
        blockInput = blockOutputs[sequence[ii]];
        indt = blockDoutDt[sequence[ii]];
    }
    Block::blockDerivative(blockInput, indt, stateData, deriv, sMode);
}

void blockSequence::blockAlgebraicUpdate(double input,
                                         const stateData& stateData,
                                         double update[],
                                         const solverMode& sMode)
{
    updateLocalCache(noInputs, stateData, sMode);
    auto cnt = static_cast<int>(sequence.size());
    double blockInput = input + bias;
    for (int ii = 0; ii < cnt; ++ii) {
        blocks[sequence[ii]]->blockAlgebraicUpdate(blockInput, stateData, update, sMode);
        blockInput = blockOutputs[sequence[ii]];
    }
    Block::blockAlgebraicUpdate(input, stateData, update, sMode);
}

void blockSequence::blockJacobianElements(double input,
                                          double didt,
                                          const stateData& stateData,
                                          matrixData<double>& matrixDataRef,
                                          index_t argLoc,
                                          const solverMode& sMode)
{
    updateLocalCache(noInputs, stateData, sMode);
    const size_t cnt = sequence.size();
    double blockInput = input + bias;
    double indt = didt;
    index_t aLoc = argLoc;
    for (size_t ii = 0; ii < cnt; ++ii) {
        blocks[sequence[ii]]->blockJacobianElements(
            blockInput, indt, stateData, matrixDataRef, aLoc, sMode);
        blockInput = blockOutputs[sequence[ii]];
        indt = blockDoutDt[sequence[ii]];
        aLoc = blocks[sequence[ii]]->getOutputLoc(sMode, 0);
    }
    Block::blockJacobianElements(blockInput, indt, stateData, matrixDataRef, aLoc, sMode);
}

void blockSequence::rootTest(const IOdata& inputs,
                             const stateData& stateData,
                             double roots[],
                             const solverMode& sMode)
{
    updateLocalCache(noInputs, stateData, sMode);
    const size_t cnt = sequence.size();
    IOdata inAct{!inputs.empty() ? inputs[0] + bias : kNullVal, getRateInput(inputs)};

    for (size_t ii = 0; ii < cnt; ++ii) {
        blocks[sequence[ii]]->rootTest(inAct, stateData, roots, sMode);
        inAct[0] = blockOutputs[sequence[ii]];
        inAct[1] = blockDoutDt[sequence[ii]];
    }
    Block::rootTest(inAct, stateData, roots, sMode);
}

void blockSequence::rootTrigger(coreTime time,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const solverMode& sMode)
{
    const size_t cnt = sequence.size();
    IOdata inAct{inputs.empty() ? kNullVal : inputs[0] + bias, getRateInput(inputs)};

    for (size_t ii = 0; ii < cnt; ++ii) {
        blocks[sequence[ii]]->rootTrigger(time, inAct, rootMask, sMode);
        inAct[0] = blockOutputs[sequence[ii]];
        inAct[1] = blockDoutDt[sequence[ii]];
    }
    Block::rootTrigger(time, inAct, rootMask, sMode);
}

change_code blockSequence::rootCheck(const IOdata& inputs,
                                     const stateData& stateData,
                                     const solverMode& sMode,
                                     check_level_t level)
{
    change_code ret = change_code::no_change;
    updateLocalCache(noInputs, stateData, sMode);
    const size_t cnt = sequence.size();
    IOdata inAct{!inputs.empty() ? inputs[0] + bias : kNullVal, getRateInput(inputs)};

    for (size_t ii = 0; ii < cnt; ++ii) {
        auto lret = blocks[sequence[ii]]->rootCheck(inAct, stateData, sMode, level);
        inAct[0] = blockOutputs[sequence[ii]];
        inAct[1] = blockDoutDt[sequence[ii]];
        ret = std::max(ret, lret);
    }
    auto lret = Block::rootCheck(inAct, stateData, sMode, level);
    ret = std::max(ret, lret);
    return ret;
}

void blockSequence::setFlag(std::string_view flag, bool val)
{
    if (flag == "differential_input") {
        opFlags.set(DIFFERENTIAL_INPUT_ACTUAL, val);
    } else {
        Block::setFlag(flag, val);
    }
}
// set parameters
void blockSequence::set(std::string_view param, std::string_view val)
{
    if (param == "sequence") {
    } else {
        Block::set(param, val);
    }
}

void blockSequence::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Block::set(param, val, unitType);
    }
}

double blockSequence::subBlockOutput(index_t blockNum) const
{
    if (std::cmp_less(blockNum, blocks.size())) {
        return blocks[blockNum]->getBlockOutput();
    }
    return kNullVal;
}

double blockSequence::subBlockOutput(const std::string& blockname) const
{
    auto* blk = static_cast<Block*>(find(blockname));
    if (blk != nullptr) {
        return blk->getBlockOutput();
    }
    return kNullVal;
}

CoreObject* blockSequence::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "block") {
        if (std::cmp_less(num, blocks.size())) {
            return blocks[num];
        }
    }
    return gridComponent::getSubObject(typeName, num);
}

CoreObject* blockSequence::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName == "block") {
        for (const auto& blk : blocks) {
            if (blk->getUserID() == searchID) {
                return blk;
            }
        }
    }
    return gridComponent::findByUserID(typeName, searchID);
}

}  // namespace griddyn::blocks

