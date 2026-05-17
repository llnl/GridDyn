/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pmu.h"

#include "../Link.h"
#include "../blocks/delayBlock.h"
#include "../blocks/filteredDerivativeBlock.h"
#include "../comms/Communicator.h"
#include "../comms/controlMessage.h"
#include "../events/Event.h"
#include "../gridBus.h"
#include "../measurement/Condition.h"
#include "../measurement/grabberSet.h"
#include "../measurement/gridGrabbers.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
pmu::pmu(const std::string& objName): sensor(objName)
{
    outputStrings = {{"voltage"}, {"angle"}, {"frequency"}, {"rocof"}};
}

CoreObject* pmu::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<pmu, sensor>(this, obj);
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

void pmu::setFlag(std::string_view flag, bool val)
{
    if ((flag == "transmit") || (flag == "transmitactive") || (flag == "transmit_active")) {
        opFlags.set(TRANSMIT_ACTIVE, val);
    } else if ((flag == "three_phase") || (flag == "3phase") || (flag == "three_phase_active")) {
        opFlags.set(three_phase_capable, val);
    } else if ((flag == "current_active") || (flag == "current")) {
        opFlags.set(CURRENT_ACTIVE, val);
    } else {
        sensor::setFlag(flag, val);
    }
}

void pmu::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        sensor::set(param, val);
    }
}

using units::convert;
using units::unit;

void pmu::set(std::string_view param, double val, unit unitType)
{
    if ((param == "tv") || (param == "voltagedelay")) {
        mVoltageFilterTime = val;
        if (opFlags[dyn_initialized]) {
        }
    } else if ((param == "ttheta") || (param == "tangle") || (param == "angledelay")) {
        mAngleFilterTime = val;
        if (opFlags[dyn_initialized]) {
        }
    } else if (param == "trocof") {
        mRocofFilterTime = val;
        if (opFlags[dyn_initialized]) {
        }
    } else if ((param == "tcurrent") || (param == "tI") || (param == "currentdelay")) {
        mCurrentFilterTime = val;
        if (opFlags[dyn_initialized]) {
        }
    } else if ((param == "transmitrate") || (param == "rate")) {
        mTransmissionPeriod = (val >= kMin_Res) ? 1.0 / val : kBigNum;
    } else if ((param == "transmitperiod") || (param == "period")) {
        mTransmissionPeriod = convert(val, unitType, units::second);
    } else if (param == "samplerate") {
        mSampleRate = val;
    } else {
        sensor::set(param, val, unitType);
    }
}

double pmu::get(std::string_view param, units::unit unitType) const
{
    if ((param == "tv") || (param == "voltagedelay")) {
        return mVoltageFilterTime;
    }
    if ((param == "ttheta") || (param == "tangle") || (param == "angledelay")) {
        return mAngleFilterTime;
    }
    if ((param == "tcurrent") || (param == "tI") || (param == "currentdelay")) {
        return mCurrentFilterTime;
    }
    if (param == "trocof") {
        return mRocofFilterTime;
    }
    if ((param == "transmitrate") || (param == "rate")) {
        return 1.0 / mTransmissionPeriod;
    }
    if (param == "transmitperiod") {
        return mTransmissionPeriod;
    }
    if (param == "samplerate") {
        return mSampleRate;
    }
    return sensor::get(param, unitType);
}

void pmu::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
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
        if (static_cast<GridComponent*>(m_sourceObject)->checkFlag(three_phase_capable)) {
            if (!opFlags[THREE_PHASE_SET]) {
                opFlags[THREE_PHASE_ACTIVE] = true;
            }
        } else {
            opFlags[THREE_PHASE_ACTIVE] = false;
        }
    }

    if (dynamic_cast<gridBus*>(m_sourceObject) != nullptr) {
        // no way to get current from a bus
        opFlags[CURRENT_ACTIVE] = false;
    }
    generateOutputNames();
    createFilterBlocks();
    sensor::dynObjectInitializeA(time0, flags);
}

void pmu::generateOutputNames()
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
void pmu::createFilterBlocks()
{
    // 4 different scenarios
    if (opFlags[THREE_PHASE_ACTIVE]) {
        if (opFlags[CURRENT_ACTIVE]) {  // NOLINT
            // three phase voltage and current
        } else {
            // three phase voltage
        }
    } else {
        auto* vBlock = new blocks::delayBlock(mVoltageFilterTime);
        vBlock->setName("voltage");
        add(vBlock);
        vBlock = new blocks::delayBlock(mAngleFilterTime);
        vBlock->setName("angle");
        add(vBlock);
        set("input0", "voltage");
        set("input1", "angle");
        set("blockinput0", 0);
        set("blockinput1", 1);
        setupOutput(0, "block0");
        setupOutput(1, "block1");
        if (opFlags[CURRENT_ACTIVE]) {
            vBlock = new blocks::delayBlock(mCurrentFilterTime);
            vBlock->setName("current_real");
            add(vBlock);
            vBlock = new blocks::delayBlock(mCurrentFilterTime);
            vBlock->setName("current_reactive");
            add(vBlock);
            set("input2", "current_real");
            set("input3", "current_reactive");
            set("blockinput2", 2);
            set("blockinput3", 3);
            setupOutput(2, "block2");
            setupOutput(3, "block3");
        }
        auto* fblock = new blocks::filteredDerivativeBlock("freq");
        fblock->set("t1", mAngleFilterTime);
        fblock->set("t2", mRocofFilterTime);
        add(fblock);
        set("blockinput" + std::to_string(fblock->locIndex), 1);
        setupOutput(fblock->locIndex, "block" + std::to_string(fblock->locIndex));
        setupOutput(fblock->locIndex + 1, "blockderiv" + std::to_string(fblock->locIndex));
    }
}

void pmu::updateA(coreTime time)
{
    sensor::updateA(time);
    if (time >= mNextTransmitTime) {
        generateAndTransmitMessage();
        mNextTransmitTime = mLastTransmitTime + mTransmissionPeriod;
        if (mNextTransmitTime <= time) {
            mNextTransmitTime = time + mTransmissionPeriod;
        }
    }
}

coreTime pmu::updateB()
{
    sensor::updateB();
    if (nextUpdateTime > mNextTransmitTime) {
        nextUpdateTime = mNextTransmitTime;
    }
    return nextUpdateTime;
}

void pmu::generateAndTransmitMessage() const
{
    if (opFlags[use_commLink]) {
        const auto& oname = outputNames();

        auto message =
            std::make_shared<commMessage>(comms::controlMessagePayload::GET_RESULT_MULTIPLE);

        auto* payload = message->getPayload<comms::controlMessagePayload>();
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

        cManager.send(std::move(message));
    }
}

}  // namespace griddyn::relays

