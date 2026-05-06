/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sensor.h"

#include "../Block.h"
#include "../Link.h"  //some special features for links
#include "../comms/Communicator.h"
#include "../comms/controlMessage.h"
#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "../measurement/grabberSet.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "utilities/matrixDataSparse.hpp"
#include "utilities/matrixDataTranslate.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::ensureSizeAtLeast;
using units::unit_cast_from_string;

sensor::sensor(const std::string& objName): Relay(objName)
{
    opFlags.set(continuous_flag);
    opFlags.set(late_b_initialize);
}

coreObject* sensor::clone(coreObject* obj) const
{
    auto nobj = cloneBase<sensor, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->m_terminal = m_terminal;
    nobj->inputStrings = inputStrings;
    nobj->blockInputs = blockInputs;
    nobj->outputs = outputs;
    nobj->outputStrings = outputStrings;
    nobj->outputMode = outputMode;
    nobj->processStatus = processStatus;
    // clone the dataSources

    for (index_t kk = 0; kk < static_cast<index_t>(dataSources.size()); ++kk) {
        if (dataSources[kk]) {
            if (static_cast<index_t>(nobj->dataSources.size()) > kk) {
                if (nobj->dataSources[kk]) {
                    dataSources[kk]->cloneTo(nobj->dataSources[kk].get());
                } else {
                    nobj->dataSources[kk] = dataSources[kk]->clone();
                }
            } else {
                nobj->dataSources.push_back(dataSources[kk]->clone());
            }
        } else {
            if (static_cast<index_t>(nobj->dataSources.size()) <= kk) {
                nobj->dataSources.push_back(nullptr);
            }
        }
    }

    for (index_t kk = 0; kk < static_cast<index_t>(outGrabbers.size()); ++kk) {
        if (outGrabbers[kk]) {
            if (static_cast<index_t>(nobj->outGrabbers.size()) > kk) {
                if (nobj->outGrabbers[kk]) {
                    outGrabbers[kk]->cloneTo(nobj->outGrabbers[kk].get());
                } else {
                    nobj->outGrabbers[kk] = outGrabbers[kk]->clone();
                }
            } else {
                nobj->outGrabbers.push_back(outGrabbers[kk]->clone());
            }
            nobj->outGrabbers[kk]->updateObject(nobj);
        } else {
            if (static_cast<index_t>(nobj->outGrabbers.size()) <= kk) {
                nobj->outGrabbers.push_back(nullptr);
            }
        }
    }

    return nobj;
}

void sensor::add(coreObject* obj)
{
    if (dynamic_cast<Block*>(obj) != nullptr) {
        add(static_cast<Block*>(obj));
    } else {
        Relay::add(obj);
    }
}

void sensor::add(Block* blk)
{
    if (blk->locIndex != kNullLocation) {
        ensureSizeAtLeast(filterBlocks,
                          static_cast<size_t>(blk->locIndex) + 1,
                          static_cast<Block*>(nullptr));
        filterBlocks[blk->locIndex] = blk;
    } else {
        filterBlocks.push_back(blk);
        blk->locIndex = static_cast<index_t>(filterBlocks.size() - 1);
    }
    ensureSizeAtLeast(blockInputs, filterBlocks.size(), -1);
    addSubObject(blk);
    blk->parentSetFlag(separate_processing, true, this);
}

void sensor::add(std::shared_ptr<grabberSet> dGr)
{
    if (dGr) {
        auto cnum = inputStrings.size();
        dataSources.resize(cnum + 1);
        inputStrings.emplace_back("#");

        if (!(dGr->stateCapable())) {
            opFlags.set(continuous_flag, false);
        }
        dataSources[cnum] = std::move(dGr);
    }
}

void sensor::add(std::shared_ptr<gridGrabber> dGr)
{
    add(std::make_shared<grabberSet>(std::move(dGr), nullptr));
}

std::shared_ptr<grabberSet> sensor::getGrabberSet(index_t grabberNum)
{
    if (grabberNum < static_cast<index_t>(dataSources.size())) {
        return dataSources[grabberNum];
    }
    throw(std::out_of_range("invalid index"));
}

void sensor::setFlag(std::string_view flag, bool val)
{
    if ((flag == "direct_io") || (flag == "direct")) {
        opFlags.set(direct_IO, val);
    } else if (flag == "sampled") {
        opFlags[sampled_only] = val;
        opFlags[continuous_flag] = !val;
    } else if (flag == "continuous") {
        opFlags[sampled_only] = !val;
        opFlags[continuous_flag] = val;
    } else {
        Relay::setFlag(flag, val);
    }
}

void sensor::set(std::string_view param, std::string_view val)
{
    std::string iparam;
    int num = gmlc::utilities::stringOps::trailingStringInt(param, iparam);
    if (iparam == "input") {
        auto sp = gmlc::utilities::stringOps::splitline(val);
        gmlc::utilities::stringOps::trim(sp);
        if (num >= 0) {
            if (num + sp.size() - 1 >= inputStrings.size()) {
                inputStrings.resize(num + sp.size());
            }
            for (auto& istr : sp) {
                inputStrings[num] = istr + '_';
                ++num;
            }
        } else {
            inputStrings.reserve(inputStrings.size() + sp.size());
            for (auto& istr : sp) {
                inputStrings.push_back(istr);
            }
        }
        if (opFlags[direct_IO]) {
            if (num >= 0) {
                ensureSizeAtLeast(outputStrings, num + sp.size());

                for (auto& istr : sp) {
                    outputStrings[num] = {gmlc::utilities::stringOps::getTailString(istr, ':')};
                    ++num;
                }
            } else {
                outputStrings.reserve(outputStrings.size() + sp.size());
                for (auto& istr : sp) {
                    outputStrings.push_back({gmlc::utilities::stringOps::getTailString(istr, ':')});
                }
            }
            m_outputSize = static_cast<count_t>(outputStrings.size());
        }
        m_inputSize = static_cast<count_t>(inputStrings.size());
    } else if (param == "condition") {
        add(std::shared_ptr<Condition>(make_condition(std::string{val}, this)));
    } else if (iparam == "filter") {
        auto blk = make_block(std::string{val});
        if (blk) {
            if (num >= 0) {
                blk->locIndex = num;
            }
            add(blk.release());
        } else {
            throw(invalidParameterValue(param));
        }
    } else if ((iparam == "outputname") || (param == "outputnames") || (param == "outputstring")) {
        if (num >= 0) {
            if (num >= static_cast<int>(outputs.size())) {
                outputStrings.resize(static_cast<size_t>(num) + 1);
            }
            outputStrings[num] = gmlc::utilities::stringOps::splitline(val);
        } else {
            auto sep = gmlc::utilities::stringOps::splitlineQuotes(val);
            gmlc::utilities::stringOps::trim(sep);

            for (auto& outputStr : sep) {
                outputStrings.push_back(gmlc::utilities::stringOps::splitline(outputStr));
            }
        }
        m_outputSize = static_cast<count_t>(outputStrings.size());
    } else if ((iparam == "output") || (param == "outputs")) {
        setupOutput(num, std::string{val});
    } else if ((iparam == "blockinput") || (iparam == "process")) {
        auto seq = gmlc::utilities::str2vector<int>(std::string{val}, -1, ",: ");
        if (num >= 0) {
            if (seq.size() == 2u) {
                ensureSizeAtLeast(blockInputs, static_cast<size_t>(seq[1]) + 1, -1);
                blockInputs[seq[1]] = seq[0];
            } else {
                throw(invalidParameterValue(param));
            }
        } else {
            ensureSizeAtLeast(blockInputs, seq.size(), -1);
            for (index_t kk = 0; kk < static_cast<index_t>(seq.size()); ++kk) {
                blockInputs[kk] = seq[kk];
            }
        }
    } else {
        Relay::set(param, val);
    }
}

void sensor::setupOutput(index_t num, const std::string& outputString)
{
    if (num >= 0) {
        auto el = outputString.find_first_of('=');
        std::string v2 = (el != std::string::npos) ? outputString.substr(0, el) : outputString;
        std::string sval = (el != std::string::npos) ? outputString.substr(el + 1) : "";
        ensureSizeAtLeast(outputs, num + 1, -1);
        ensureSizeAtLeast(outputStrings, num + 1);
        ensureSizeAtLeast(outputMode, num + 1, outputMode_t::block);
        ensureSizeAtLeast(outGrabbers, num + 1);
        m_outputSize = static_cast<count_t>(outputs.size());
        if (!sval.empty()) {
            outputStrings[num] = {sval};
        }
        auto outputValue = gmlc::utilities::numeric_conversionComplete<int>(v2, -1);
        if (outputValue >= 0) {
            outputs[num] = outputValue;
            outputMode[num] = outputMode_t::block;
        } else {
            int knum = gmlc::utilities::stringOps::trailingStringInt(v2, sval, 0);
            if (sval == "input") {
                outputs[num] = knum;
                outputMode[num] = outputMode_t::direct;
            } else if (sval == "block") {
                outputs[num] = knum;
                outputMode[num] = outputMode_t::block;
            } else if (sval == "blockderiv") {
                outputs[num] = knum;
                outputMode[num] = outputMode_t::blockderiv;
            } else {
                outGrabbers[num] = std::make_shared<grabberSet>(v2, this);

                outputMode[num] = outputMode_t::processed;
            }
        }
    } else {
        auto sep = gmlc::utilities::stringOps::splitline(outputString);
        gmlc::utilities::stringOps::trim(sep);
        for (auto& v : sep) {
            setupOutput(static_cast<index_t>(outputs.size()), v);
        }
    }
}

void sensor::set(std::string_view param, double val, units::unit unitType)
{
    std::string iparam;
    int num = gmlc::utilities::stringOps::trailingStringInt(param, iparam, -1);
    if (param == "terminal") {
        m_terminal = static_cast<int>(val);
    } else if ((iparam == "blockinput") || (iparam == "process")) {
        if (num < 0) {
            blockInputs.push_back(static_cast<int>(val));
        } else {
            ensureSizeAtLeast(blockInputs, num + 1, -1);
            blockInputs[num] = static_cast<int>(val);
        }
    } else if (param == "direct") {
        opFlags.set(direct_IO, (val > 0.1));
    } else if (iparam == "output") {
        if (static_cast<int>(val) < 0) {
            throw(invalidParameterValue(param));
        }
        if (num < 0) {
            outputs.push_back(static_cast<int>(val));
        } else {
            if (num < static_cast<int>(outputs.size())) {
                outputs[num] = static_cast<int>(val);
            }
        }
    } else {
        Relay::set(param, val, unitType);
    }
}

double sensor::get(std::string_view param, units::unit unitType) const
{
    index_t ind = lookupOutputIndex(param);
    if (ind != kNullLocation) {
        return getOutput(ind);
    }

    if (param == "terminal") {
        return static_cast<double>(m_terminal);
    }
    return Relay::get(param, unitType);
}

void sensor::updateObject(coreObject* obj, object_update_mode mode)
{
    for (auto& ds : dataSources) {
        if (ds) {
            ds->updateObject(obj, mode);
        }
    }

    Relay::updateObject(obj, mode);
}

void sensor::getObjects(std::vector<coreObject*>& objects) const
{
    for (auto& ds : dataSources) {
        if (ds) {
            ds->getObjects(objects);
        }
    }

    Relay::getObjects(objects);
}

void sensor::generateInputGrabbers()
{
    auto iSize = static_cast<int>(inputStrings.size());
    ensureSizeAtLeast(dataSources, iSize);
    for (int ii = 0; ii < iSize; ++ii) {
        if (inputStrings[ii].front() == '#')  // escape hatch for previously loaded grabbers
        {
            continue;
        }
        auto istr = inputStrings[ii];
        if (istr.back() == '_') {
            istr.pop_back();
        }
        auto cloc = istr.find_first_of(':');

        if (cloc == std::string::npos) {  // if there is a colon assume the input is fully specified
            if ((opFlags[link_type_source]) && (isdigit(istr.back()) == 0)) {
                if (m_terminal > 0) {
                    gmlc::utilities::stringOps::appendInteger(istr, m_terminal);
                }
            }
        }
        coreObject* target_obj = (m_sourceObject != nullptr) ? m_sourceObject : getParent();

        dataSources[ii] = std::make_shared<grabberSet>(istr, target_obj, opFlags[sampled_only]);
    }
}
using cm = comms::controlMessagePayload;
void sensor::receiveMessage(std::uint64_t sourceID, std::shared_ptr<commMessage> message)
{
    auto* payload = message->getPayload<cm>();

    double val;

    switch (message->getMessageType()) {
        case cm::SET:
            // only local set
            try {
                set(gmlc::utilities::convertToLowerCase(payload->m_field),
                    payload->m_value,
                    units::unit_cast_from_string(payload->m_units));
                if (!opFlags[no_message_reply])  // unless told not to respond return with the
                {
                    auto gres = std::make_shared<commMessage>(cm::SET_SUCCESS);
                    assert(gres->getPayload<cm>());
                    gres->getPayload<cm>()->m_actionID =
                        (payload->m_actionID > 0) ? payload->m_actionID : instructionCounter;
                    commLink->transmit(sourceID, std::move(gres));
                }
            }
            catch (const std::invalid_argument&) {
                if (!opFlags[no_message_reply])  // unless told not to respond return with the
                {
                    auto gres = std::make_shared<commMessage>(cm::SET_FAIL);
                    assert(gres->getPayload<cm>());
                    gres->getPayload<cm>()->m_actionID =
                        (payload->m_actionID > 0) ? payload->m_actionID : instructionCounter;
                    commLink->transmit(sourceID, std::move(gres));
                }
            }

            break;
        case cm::GET: {
            val = get(gmlc::utilities::convertToLowerCase(payload->m_field),
                      units::unit_cast_from_string(payload->m_units));
            auto reply = std::make_shared<commMessage>(cm::GET_RESULT);
            auto rep = reply->getPayload<cm>();
            assert(rep);
            rep->m_field = payload->m_field;
            rep->m_value = val;
            rep->m_time = prevTime;
            commLink->transmit(sourceID, std::move(reply));

            break;
        }
        case cm::SET_SUCCESS:
        case cm::SET_FAIL:
        case cm::GET_RESULT:
        case cm::SET_SCHEDULED:
        case cm::GET_SCHEDULED:
        case cm::CANCEL_FAIL:
        case cm::CANCEL_SUCCESS:
        case cm::GET_RESULT_MULTIPLE:
        case cm::CANCEL:
            break;
        case cm::GET_MULTIPLE: {
            auto reply = std::make_shared<commMessage>(cm::GET_RESULT_MULTIPLE);
            auto rep = reply->getPayload<cm>();
            if (rep == nullptr) {
                break;
            }
            rep->multiValues.resize(0);
            rep->multiFields = payload->multiFields;
            for (const auto& fieldName : payload->multiFields) {
                val = get(gmlc::utilities::convertToLowerCase(fieldName),
                          units::unit_cast_from_string(payload->m_units));
                rep->multiValues.push_back(val);
            }
            rep->m_time = prevTime;
            commLink->transmit(sourceID, std::move(reply));
            break;
        }
        case cm::GET_PERIODIC:
            break;
        default:
            break;
    }
}

static const IOdata kNullVec;

double
    sensor::getBlockOutput(const stateData& sD, const solverMode& sMode, index_t blockNumber) const
{
    double ret = kNullVal;
    if (isLocal(sMode)) {
        if (isValidIndex(blockNumber, filterBlocks)) {
            ret = filterBlocks[blockNumber]->getBlockOutput();
        }
    } else {
        if (isValidIndex(blockNumber, filterBlocks)) {
            ret = filterBlocks[blockNumber]->getBlockOutput(sD, sMode);
        }
    }
    return ret;
}

double sensor::getBlockDerivOutput(const stateData& sD,
                                   const solverMode& sMode,
                                   index_t blockNumber) const
{
    double ret = kNullVal;
    if (isLocal(sMode)) {
        if (isValidIndex(blockNumber, filterBlocks)) {
            ret = filterBlocks[blockNumber]->getBlockDoutDt();
        }
    } else {
        if (isValidIndex(blockNumber, filterBlocks)) {
            ret = filterBlocks[blockNumber]->getBlockDoutDt(sD, sMode);
        }
    }
    return ret;
}

double sensor::getInput(const stateData& sD, const solverMode& sMode, index_t inputNumber) const
{
    double ret = kNullVal;
    if (isLocal(sMode)) {
        if (isValidIndex(inputNumber, dataSources)) {
            ret = dataSources[inputNumber]->grabData();
        }
    } else {
        if (isValidIndex(inputNumber, dataSources)) {
            ret = dataSources[inputNumber]->grabData(sD, sMode);
        }
    }
    return ret;
}

void sensor::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        opFlags.set(link_type_source);
    }

    if (dynamic_cast<Link*>(m_sinkObject) != nullptr) {
        opFlags.set(link_type_sink);
    }
    generateInputGrabbers();
    // check if we need to go to sampled mode
    if (opFlags[continuous_flag]) {
        for (auto& dgs : dataSources) {
            if ((dgs) && (!dgs->stateCapable())) {
                // TODO(phlpt): Support partially continuous sensors.
                opFlags.set(continuous_flag, false);
                logging::warning(
                    this,
                    "not all data sources support continuous operation , reverting to sampled mode");
                break;
            }
        }
    }

    if (!opFlags[continuous_flag]) {
        opFlags.set(sampled_only);
        for (auto& fb : filterBlocks) {
            fb->setFlag("sampled_only", true);
        }
    }

    return Relay::dynObjectInitializeA(time0, flags);
}

void sensor::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& /*desiredOutput*/,
                                  IOdata& fieldSet)
{
    if (filterBlocks.empty())  // no filter blocks, use direct output
    {
        opFlags.set(direct_IO);
    }
    for (size_t kk = 0; kk < filterBlocks.size(); ++kk) {
        if (blockInputs[kk] < 0) {
            if (dataSources.size() > kk) {
                blockInputs[kk] = static_cast<int>(kk);
            } else if (dataSources.size() == 1) {
                blockInputs[kk] = 0;
            } else {
                logging::warning(this, "unable to uniquely determine filter block input");
            }
        }
    }

    if (outputs.empty()) {
        if (opFlags[direct_IO]) {
            for (int kk = 0; kk < static_cast<int>(dataSources.size()); ++kk) {
                outputs.push_back(kk);
                outputMode.push_back(outputMode_t::direct);
                outGrabbers.push_back(nullptr);
            }
        } else {
            for (int kk = 0; kk < static_cast<int>(filterBlocks.size()); ++kk) {
                outputs.push_back(kk);
                outputMode.push_back(outputMode_t::block);
                outGrabbers.push_back(nullptr);
            }
        }
    }
    // do a verification check on the output codes
    int blkcnt = 0;
    int ocount = 0;
    const auto outputCount = static_cast<count_t>(m_outputSize);
    const auto filterBlockCount = static_cast<int>(filterBlocks.size());
    const auto dataSourceCount = static_cast<int>(dataSources.size());
    for (count_t kk = 0; kk < outputCount; ++kk) {
        switch (outputMode[kk]) {
            case outputMode_t::block:
                if ((outputs[kk] < 0) || (outputs[kk] >= filterBlockCount)) {
                    if (blkcnt < filterBlockCount) {
                        outputs[kk] = blkcnt;
                    } else {
                        outputs[kk] = 0;
                    }
                }
                ++blkcnt;
                break;
            case outputMode_t::blockderiv:
                if ((outputs[kk] < 0) || (outputs[kk] >= filterBlockCount)) {
                    if (blkcnt < filterBlockCount) {
                        outputs[kk] = blkcnt;
                    } else {
                        outputs[kk] = 0;
                    }
                }
                break;
            case outputMode_t::direct:
                if ((outputs[kk] < 0) || (outputs[kk] >= dataSourceCount)) {
                    if (ocount < dataSourceCount) {
                        outputs[kk] = ocount;
                    } else {
                        outputs[kk] = 0;
                    }
                }
                ++ocount;
            case outputMode_t::processed:
            default:
                break;
        }
    }

    // initialization of the blocks
    auto sz = static_cast<count_t>(filterBlocks.size());
    for (index_t kk = 0; kk < sz; ++kk) {
        double cv = getBlockInput(kk, noInputs);
        filterBlocks[kk]->blockInitialize(cv, kNullVal);
    }
    m_outputSize = static_cast<index_t>(outputMode.size());

    // generate the output
    fieldSet = getOutputs(inputs, emptyStateData, cLocalSolverMode);
}

void sensor::updateA(coreTime time)
{
    if (time >= m_nextSampleTime) {
        auto blks = static_cast<index_t>(filterBlocks.size());
        for (index_t kk = 0; kk < blks; ++kk) {
            double inputFB = getBlockInput(kk, noInputs);
            // make sure the process can be handled in states
            // printf("%f block %d input=%f\n ", static_cast<double>(time), static_cast<int>(kk),
            // inputFB);
            filterBlocks[kk]->step(time, inputFB);
        }
    }
    Relay::updateA(time);
}

void sensor::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    auto blks = static_cast<index_t>(filterBlocks.size());
    for (index_t kk = 0; kk < blks; ++kk) {
        double inputFB = getBlockInput(kk, inputs);
        // make sure the process can be handled in states
        filterBlocks[kk]->step(time, inputFB);
    }
    Relay::timestep(time, inputs, sMode);
}

void sensor::jacobianElements(const IOdata& inputs,
                              const stateData& sD,
                              matrixData<double>& md,
                              const IOlocs& /*inputLocs*/,
                              const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        matrixDataSparse<double> d2, dp;
        auto blks = static_cast<index_t>(filterBlocks.size());
        for (index_t kk = 0; kk < blks; ++kk) {
            // TODO(phlpt): This needs some help for performance and organization.
            double inputFB = getBlockInput(kk, inputs, sD, sMode);
            d2.clear();
            if (dataSources[blockInputs[kk]]->hasJacobian()) {
                dataSources[blockInputs[kk]]->outputPartialDerivatives(sD, d2, sMode);
            }

            // make sure the process can be handled in states

            if (d2.size() == 0) {
                filterBlocks[kk]->blockJacobianElements(inputFB, 0, sD, md, kNullLocation, sMode);
            } else {
                filterBlocks[kk]->blockJacobianElements(inputFB, 0, sD, dp, 0, sMode);
                dp.cascade(d2, 0);
                md.merge(dp);
            }
        }
    }
}

void sensor::residual(const IOdata& inputs,
                      const stateData& sD,
                      double resid[],
                      const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        auto blks = static_cast<index_t>(filterBlocks.size());
        for (index_t kk = 0; kk < blks; ++kk) {
            double inputFB = getBlockInput(kk, inputs, sD, sMode);
            // make sure the process can be handled in states
            // printf("state [4]=%f, state[5]=%f\n", sD.state[4], sD.state[5]);
            filterBlocks[kk]->blockResidual(inputFB, 0, sD, resid, sMode);
        }
    }
}

void sensor::algebraicUpdate(const IOdata& inputs,
                             const stateData& sD,
                             double update[],
                             const solverMode& sMode,
                             double /*alpha*/)
{
    if (algSize(sMode) > 0) {
        auto blks = static_cast<index_t>(filterBlocks.size());
        for (index_t kk = 0; kk < blks; ++kk) {
            double inputFB = getBlockInput(kk, inputs, sD, sMode);
            // make sure the process can be handled in states
            filterBlocks[kk]->blockAlgebraicUpdate(inputFB, sD, update, sMode);
        }
    }
}

void sensor::derivative(const IOdata& inputs,
                        const stateData& sD,
                        double deriv[],
                        const solverMode& sMode)
{
    if (diffSize(sMode) > 0) {
        auto blks = static_cast<index_t>(filterBlocks.size());
        for (index_t kk = 0; kk < blks; ++kk) {
            double inputFB = getBlockInput(kk, inputs, sD, sMode);
            // make sure the process can be handled in states
            filterBlocks[kk]->blockDerivative(inputFB, 0, sD, deriv, sMode);
        }
    }
}

double sensor::getBlockInput(index_t blockNum,
                             const IOdata& /*inputs*/,
                             const stateData& sD,
                             const solverMode& sMode) const
{
    double res = dataSources[blockInputs[blockNum]]->grabData(sD, sMode);
    return res;
}

double sensor::getBlockInput(index_t blockNum, const IOdata& /*inputs*/) const
{
    double res = dataSources[blockInputs[blockNum]]->grabData();
    return res;
}

const std::vector<stringVec>& sensor::outputNames() const
{
    if (static_cast<count_t>(outputStrings.size()) < m_outputSize) {
        // TODO(phlpt): Correct this before outputNames is reached.
        return gridComponent::outputNames();
    }
    return outputStrings;
}

IOdata sensor::getOutputs(const IOdata& inputs, const stateData& sD, const solverMode& sMode) const
{
    IOdata out(m_outputSize);
    for (index_t pp = 0; pp < m_outputSize; ++pp) {
        out[pp] = getOutput(inputs, sD, sMode, pp);
    }
    return out;
}

double sensor::getOutput(const IOdata& /*inputs*/,
                         const stateData& sD,
                         const solverMode& sMode,
                         index_t outNum) const
{
    if (!isValidIndex(outNum, outputMode)) {
        return kNullVal;
    }
    switch (outputMode[outNum]) {
        case outputMode_t::block:
            return filterBlocks[outputs[outNum]]->getOutput(noInputs, sD, sMode);
        case outputMode_t::blockderiv:
            return filterBlocks[outputs[outNum]]->getBlockDoutDt(sD, sMode);
        case outputMode_t::processed:
            return outGrabbers[outNum]->grabData(sD, sMode);
        case outputMode_t::direct:
            return dataSources[outputs[outNum]]->grabData(sD, sMode);
        default:
            return kNullVal;
    }
}

double sensor::getOutput(index_t outNum) const
{
    double out = kNullVal;

    if (!isValidIndex(outNum, outputMode)) {
        return out;
    }
    switch (outputMode[outNum]) {
        case outputMode_t::block:
            out = filterBlocks[outputs[outNum]]->getOutput();
            break;
        case outputMode_t::blockderiv:
            return filterBlocks[outputs[outNum]]->getBlockDoutDt();
        case outputMode_t::processed:
            out = outGrabbers[outNum]->grabData();
            break;
        case outputMode_t::direct:
            out = dataSources[outputs[outNum]]->grabData();
            break;
        default:
            break;
    }

    // printf("sensors %d, output %d val=%f\n", getUserID(),num, out);
    return out;
}

index_t sensor::getOutputLoc(const solverMode& sMode, index_t outNum) const
{
    if (!isValidIndex(outNum, outputMode)) {
        return kNullLocation;
    }
    switch (outputMode[outNum]) {
        case outputMode_t::block:
            return filterBlocks[outputs[outNum]]->getOutputLoc(sMode);
        case outputMode_t::processed:
        case outputMode_t::direct:
        default:
            return kNullLocation;
    }
}

// TODO(phlpt): Simplify this output-partial-derivatives path.
void sensor::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const solverMode& sMode)
{
    matrixDataTranslate<3, double> aDT(md);
    for (index_t pp = 0; pp < static_cast<index_t>(outputs.size()); ++pp) {
        switch (outputMode[pp]) {
            case outputMode_t::block:
                aDT.setTranslation(0, pp);
                filterBlocks[outputs[pp]]->outputPartialDerivatives(noInputs, sD, aDT, sMode);
                break;
            case outputMode_t::processed:
                // out[pp] = outGrabbers[pp]->grabData(sD, sMode);
                break;
            case outputMode_t::direct:
                // out[pp] = dataSources[outputs[pp]]->grabData();
                break;
            default:
                // out[pp] = kNullVal;
                break;
        }
    }
}

void sensor::rootTest(const IOdata& inputs,
                      const stateData& sD,
                      double roots[],
                      const solverMode& sMode)
{
    Relay::rootTest(inputs, sD, roots, sMode);
    if (stateSize(sMode) > 0) {
        IOdata localInputs(1);

        auto blks = static_cast<index_t>(filterBlocks.size());
        for (decltype(blks) kk = 0; kk < blks; ++kk) {
            localInputs[0] = getBlockInput(kk, inputs, sD, sMode);
            // printf("%d root test:%f block %d input=%f\n ", filterBlocks[kk]->getUserID(),
            // static_cast<double>(sD.time), static_cast<int>(kk), localInputs[0]);  make sure the
            // process can be handled in states
            filterBlocks[kk]->rootTest(localInputs, sD, roots, sMode);
        }
    }
}

void sensor::rootTrigger(coreTime time,
                         const IOdata& inputs,
                         const std::vector<int>& rootMask,
                         const solverMode& sMode)
{
    Relay::rootTrigger(time, inputs, rootMask, sMode);
    if (stateSize(sMode) > 0) {
        IOdata localInputs(1);

        auto blks = static_cast<index_t>(filterBlocks.size());
        for (decltype(blks) kk = 0; kk < blks; ++kk) {
            localInputs[0] = getBlockInput(kk, inputs);
            // make sure the process can be handled in states
            // printf("rootTrigger:%f block %d input=%f\n ", static_cast<double>(time),
            // static_cast<int>(kk), localInputs[0]);
            filterBlocks[kk]->rootTrigger(time, localInputs, rootMask, sMode);
        }
    }
}

change_code sensor::rootCheck(const IOdata& inputs,
                              const stateData& sD,
                              const solverMode& sMode,
                              check_level_t level)
{
    change_code ret = Relay::rootCheck(inputs, sD, sMode, level);
    if (stateSize(sMode) > 0) {
        IOdata localInputs(1);

        auto blks = static_cast<index_t>(filterBlocks.size());
        for (decltype(blks) kk = 0; kk < blks; ++kk) {
            localInputs[0] = getBlockInput(kk, inputs, sD, sMode);
            // printf("%d root check:%f block %d input=%f\n ", filterBlocks[kk]->getUserID(),
            // static_cast<double>(sD.time), static_cast<int>(kk), localInputs[0]);  make sure the
            // process can be handled in states
            auto iret = filterBlocks[kk]->rootCheck(localInputs, sD, sMode, level);
            ret = std::max(ret, iret);
        }
    }
    return ret;
}
}  // namespace griddyn
