/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Pmu.h"

#include "../GridBus.h"
#include "../Link.h"
#include "../blocks/DelayBlock.h"
#include "../blocks/FilteredDerivativeBlock.h"
#include "../comms/Communicator.h"
#include "../comms/ControlMessage.h"
#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "../measurement/GrabberSet.h"
#include "../measurement/GridGrabbers.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
const ModelDescription& Pmu::getModelDescription() const
{
    static const auto description = ModelDescription::define<Pmu>(
        "Pmu",
        &Sensor::getModelDescription(),
        {flag({"transmit", "transmitactive", "transmit_active"},
              &Pmu::opFlags,
              TRANSMIT_ACTIVE,
              "Transmit"),
         flag({"current_active", "current"}, &Pmu::opFlags, CURRENT_ACTIVE, "Current active"),
         parameter({"tv", "voltagedelay"},
                   &Pmu::mVoltageFilterTime,
                   units::defunit,
                   "Filter time constant for the voltage measurement"),
         parameter({"ttheta", "tangle", "angledelay"},
                   &Pmu::mAngleFilterTime,
                   units::defunit,
                   "Filter time constant for the angle measurement"),
         parameter({"trocof"},
                   &Pmu::mRocofFilterTime,
                   units::defunit,
                   "Filter time constant for computing the ROCOF"),
         parameter({"tcurrent", "tI", "currentdelay"},
                   &Pmu::mCurrentFilterTime,
                   units::defunit,
                   "Filter time constant for the current measurement"),
         command({"transmitrate", "rate"}, units::defunit, "Transmitrate"),
         parameter({"transmitperiod", "period"},
                   &Pmu::mTransmissionPeriod,
                   units::second,
                   "The rate of data transmission"),
         parameter({"samplerate"}, &Pmu::mSampleRate, units::defunit, "The actual sample time"),
         command({"three_phase", "3phase", "three_phase_active"},
                 units::defunit,
                 "Three phase",
                 ModelFieldType::BOOLEAN)});
    return description;
}

bool Pmu::getFlag(std::string_view flag) const
{
    if (const auto value = Pmu::getModelDescription().tryGetFlagLocal(*this, flag)) {
        return *value;
    }
    return Sensor::getFlag(flag);
}

void Pmu::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = Pmu::getModelDescription();
    getParamString<Pmu, Sensor>(this,
                                pstr,
                                description.numericNames(),
                                description.stringNames(),
                                description.flagNames(),
                                pstype);
}

Pmu::Pmu(const std::string& objName): Sensor(objName)
{
    outputStrings = {{"voltage"}, {"angle"}, {"frequency"}, {"rocof"}};
}

CoreObject* Pmu::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<Pmu, Sensor>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->mVoltageFilterTime = mVoltageFilterTime;
    nobj->mAngleFilterTime = mAngleFilterTime;
    nobj->mRocofFilterTime = mRocofFilterTime;
    nobj->mCurrentFilterTime = mCurrentFilterTime;
    nobj->mTransmissionPeriod = mTransmissionPeriod;
    nobj->mSampleRate = mSampleRate;
    return nobj;
}

void Pmu::setFlag(std::string_view flag, bool val)
{
    if (Pmu::getModelDescription().trySetFlagLocal(*this, flag, val)) {
        return;
    }
    if ((flag == "three_phase") || (flag == "3phase") || (flag == "three_phase_active")) {
        opFlags.set(THREE_PHASE_SET, val);
        opFlags.set(THREE_PHASE_ACTIVE, val);
    } else {
        Sensor::setFlag(flag, val);
    }
}

void Pmu::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Sensor::set(param, val);
    }
}

using units::convert;
using units::unit;

void Pmu::set(std::string_view param, double val, unit unitType)
{
    if (isSetHandled(Pmu::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        return;
    }
    if ((param == "transmitrate") || (param == "rate")) {
        mTransmissionPeriod = (val >= kMin_Res) ? 1.0 / val : kBigNum;
    } else {
        Sensor::set(param, val, unitType);
    }
}

double Pmu::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = Pmu::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    if ((param == "transmitrate") || (param == "rate")) {
        return 1.0 / mTransmissionPeriod;
    }

    return Sensor::get(param, unitType);
}

void Pmu::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (m_sourceObject == nullptr) {
        // we know the parent is most likely an area so find the corresponding bus that matches
        // the user ID
        if (getUserID() != kNullLocation) {
            m_sourceObject = getParent()->getSubObject("bus", getUserID());
        }
    }
    // if the first check failed just try to find a bus
    if (m_sourceObject == nullptr) {
        m_sourceObject = getParent()->find("bus");
    }
    if (m_sourceObject == nullptr) {
        logging::warning(this, "no pmu target specified");
        disable();
        return;
    }
    // check for 3 phase sensors
    if (dynamic_cast<GridComponent*>(m_sourceObject) != nullptr) {
        if (static_cast<GridComponent*>(m_sourceObject)->checkFlag(THREE_PHASE_CAPABLE)) {
            if (!opFlags[THREE_PHASE_SET]) {
                opFlags[THREE_PHASE_ACTIVE] = true;
            }
        } else {
            opFlags[THREE_PHASE_ACTIVE] = false;
        }
    }

    if (dynamic_cast<GridBus*>(m_sourceObject) != nullptr) {
        // no way to get current from a bus
        opFlags[CURRENT_ACTIVE] = false;
    }
    generateOutputNames();
    createFilterBlocks();
    Sensor::dynObjectInitializeA(time0, flags);
}

void Pmu::generateOutputNames()
{
    // 4 different scenarios
    if (opFlags[THREE_PHASE_ACTIVE]) {
        if (opFlags[CURRENT_ACTIVE]) {
            // three phase voltage and current
            outputStrings = {{"voltageA"},
                             {"angleA"},
                             {"voltageB"},
                             {"angleB"},
                             {"voltageC"},
                             {"angleC"},
                             {"current_realA"},
                             {"current_imagA"},
                             {"current_realB"},
                             {"current_imagB"},
                             {"current_realC"},
                             {"current_imagC"},
                             {"frequency"},
                             {"rocof"}};
        } else {
            // three phase voltage
            outputStrings = {{"voltageA"},
                             {"angleA"},
                             {"voltageB"},
                             {"angleB"},
                             {"voltageC"},
                             {"angleC"},
                             {"frequency"},
                             {"rocof"}};
        }
    } else {
        if (opFlags[CURRENT_ACTIVE]) {
            // single phase voltage and current
            outputStrings = {{"voltage"},
                             {"angle"},
                             {"current_real"},
                             {"current_imag"},
                             {"frequency"},
                             {"rocof"}};
        } else {
            // single phase voltage
            outputStrings = {{"voltage", "v"},
                             {"angle", "ang", "theta"},
                             {"frequency", "freq", "f"},
                             {"rocof"}};
        }
    }
}
/** generate the filter blocks and inputs for the sensor object*/
void Pmu::createFilterBlocks()
{
    // 4 different scenarios
    if (opFlags[THREE_PHASE_ACTIVE]) {
        if (opFlags[CURRENT_ACTIVE]) {  // NOLINT
            // three phase voltage and current
        } else {
            // three phase voltage
        }
    } else {
        auto* vBlock = new blocks::DelayBlock(mVoltageFilterTime);
        vBlock->setName("voltage");
        add(vBlock);
        vBlock = new blocks::DelayBlock(mAngleFilterTime);
        vBlock->setName("angle");
        add(vBlock);
        set("input0", "voltage");
        set("input1", "angle");
        set("blockinput0", 0);
        set("blockinput1", 1);
        setupOutput(0, "block0");
        setupOutput(1, "block1");
        if (opFlags[CURRENT_ACTIVE]) {
            vBlock = new blocks::DelayBlock(mCurrentFilterTime);
            vBlock->setName("current_real");
            add(vBlock);
            vBlock = new blocks::DelayBlock(mCurrentFilterTime);
            vBlock->setName("current_reactive");
            add(vBlock);
            set("input2", "current_real");
            set("input3", "current_reactive");
            set("blockinput2", 2);
            set("blockinput3", 3);
            setupOutput(2, "block2");
            setupOutput(3, "block3");
        }
        auto* fblock = new blocks::FilteredDerivativeBlock("freq");
        fblock->set("t1", mAngleFilterTime);
        fblock->set("t2", mRocofFilterTime);
        add(fblock);
        set("blockinput" + std::to_string(fblock->locIndex), 1);
        setupOutput(fblock->locIndex, "block" + std::to_string(fblock->locIndex));
        setupOutput(fblock->locIndex + 1, "blockderiv" + std::to_string(fblock->locIndex));
    }
}

void Pmu::updateA(CoreTime time)
{
    Sensor::updateA(time);
    if (time >= mNextTransmitTime) {
        generateAndTransmitMessage();
        mNextTransmitTime = mLastTransmitTime + mTransmissionPeriod;
        if (mNextTransmitTime <= time) {
            mNextTransmitTime = time + mTransmissionPeriod;
        }
    }
}

CoreTime Pmu::updateB()
{
    Sensor::updateB();
    if (nextUpdateTime > mNextTransmitTime) {
        nextUpdateTime = mNextTransmitTime;
    }
    return nextUpdateTime;
}

void Pmu::generateAndTransmitMessage() const
{
    if (opFlags[USE_COMM_LINK]) {
        const auto& oname = outputNames();

        auto message =
            std::make_shared<CommMessage>(comms::ControlMessagePayload::GET_RESULT_MULTIPLE);

        auto* payload = message->getPayload<comms::ControlMessagePayload>();
        auto res = getOutputs(noInputs, emptyStateData, cLocalSolverMode);

        payload->multiFields.resize(res.size());
        payload->multiValues.resize(res.size());
        payload->multiUnits.resize(res.size());
        payload->m_time = prevTime;
        if (res.size() > static_cast<size_t>(std::numeric_limits<index_t>::max())) {
            throw std::out_of_range("pmu output count exceeds index_t range");
        }
        for (size_t outputIndex = 0; outputIndex < res.size(); ++outputIndex) {
            const auto unitIndex = static_cast<index_t>(outputIndex);
            payload->multiFields[outputIndex] = oname[outputIndex][0];
            payload->multiValues[outputIndex] = res[outputIndex];
            payload->multiUnits[outputIndex] = to_string(outputUnits(unitIndex));
        }

        cManager.send(message);
    }
}

}  // namespace griddyn::relays
