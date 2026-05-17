/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "txThermalModel.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/Link.h"
#include "griddyn/blocks/delayBlock.h"
#include "griddyn/events/Event.h"
#include "griddyn/measurement/Condition.h"
#include "griddyn/measurement/grabberSet.h"
#include "griddyn/measurement/gridGrabbers.h"
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::extra {
txThermalModel::txThermalModel(const std::string& objName): sensor(objName)
{
    opFlags.reset(continuous_flag);  // this is a not a continuous model
    outputStrings = {{"ambient", "ambientTemp", "airTemp"},
                     {"top_oil", "top_oil_temp"},
                     {"hot_spot", "hot_spot_temp"}};  // preset the outputNames
    m_outputSize = 3;
}

CoreObject* txThermalModel::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<txThermalModel, sensor>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->mOilTimeConstant = mOilTimeConstant;
    nobj->mRatedHotSpotRise = mRatedHotSpotRise;
    nobj->mRatedTopOilRise = mRatedTopOilRise;
    nobj->mWindingTimeConstant = mWindingTimeConstant;
    nobj->mLossRatio = mLossRatio;
    nobj->mOilExponent = mOilExponent;
    nobj->mWindingExponent = mWindingExponent;
    nobj->mAmbientTemp = mAmbientTemp;
    nobj->mTempRateOfChange = mTempRateOfChange;
    nobj->mRating = mRating;
    nobj->mRatedLoss = mRatedLoss;
    nobj->mThermalCapacity = mThermalCapacity;
    nobj->mRadiationConstant = mRadiationConstant;
    nobj->mAlarmTemp1 = mAlarmTemp1;
    nobj->mAlarmTemp2 = mAlarmTemp2;
    nobj->mCutoutTemp = mCutoutTemp;
    nobj->mAlarmDelay = mAlarmDelay;
    return nobj;
}

void txThermalModel::setFlag(std::string_view flag, bool val)
{
    if (flag == "auto") {
        opFlags.set(auto_parameter_load, val);
    } else if (flag == "parameter_updates") {
        opFlags.set(enable_parameter_updates, val);
    } else if (flag == "enable_alarms") {
        opFlags.set(enable_alarms, val);
    } else {
        sensor::setFlag(flag, val);
    }
}

void txThermalModel::set(std::string_view param, std::string_view val)
{
    if ((param == "txtype") || (param == "cooling")) {
        const auto normalizedValue = gmlc::utilities::convertToLowerCase(val);
        if (normalizedValue == "auto") {
            opFlags.set(auto_parameter_load);
        } else if (normalizedValue == "oa") {
            mRatedHotSpotRise = 25.0;
            mRatedTopOilRise = 55.0;
            mOilTimeConstant = 3.0 * 3600.0;
            mWindingTimeConstant = 5.0 * 60.0;
            mLossRatio = 3.2;
            mOilExponent = 0.8;
            mWindingExponent = 0.8;
        } else if (normalizedValue == "fa") {
            mRatedHotSpotRise = 30.0;
            mRatedTopOilRise = 50.0;
            mOilTimeConstant = 2.0 * 3600.0;
            mWindingTimeConstant = 5.0 * 60.0;
            mLossRatio = 4.5;
            mOilExponent = 0.8;
            mWindingExponent = 0.8;
        } else if (normalizedValue == "fahot") {
            mRatedHotSpotRise = 35.0;
            mRatedTopOilRise = 45.0;
            mOilTimeConstant = 1.25 * 3600.0;
            mWindingTimeConstant = 5.0 * 60.0;
            mLossRatio = 6.5;
            mOilExponent = 0.9;
            mWindingExponent = 0.8;
        } else if (normalizedValue == "ndfoa") {
            mRatedHotSpotRise = 35.0;
            mRatedTopOilRise = 45.0;
            mOilTimeConstant = 1.25 * 3600.0;
            mWindingTimeConstant = 5.0 * 60.0;
            mLossRatio = 6.5;
            mOilExponent = 1.0;
            mWindingExponent = 0.8;
        } else if (normalizedValue == "dfoa") {
            mRatedHotSpotRise = 35.0;
            mRatedTopOilRise = 45.0;
            mOilTimeConstant = 1.25 * 3600.0;
            mWindingTimeConstant = 5.0 * 60.0;
            mLossRatio = 6.5;
            mOilExponent = 1.0;
            mWindingExponent = 1.0;
        }
    } else {
        sensor::set(param, val);
    }
}

void txThermalModel::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "ambient") || (param == "ambienttemp")) {
        mAmbientTemp = units::convert(val, unitType, units::degC);
    } else if ((param == "dtempdt") || (param == "temp_rate_of_change")) {
        mTempRateOfChange = units::convert(val, unitType, units::degC);
    } else if ((param == "dths") || (param == "rated_hot_spot_rise") || (param == "dthsr")) {
        mRatedHotSpotRise = units::convert(val, unitType, units::degC);
    } else if ((param == "dttor") || (param == "rated_top_oil_rise") || (param == "dtto")) {
        mRatedTopOilRise = units::convert(val, unitType, units::degC);
    } else if ((param == "ttor") || (param == "oil_time_constant")) {
        mOilTimeConstant = units::convert(val, unitType, units::second);
    } else if ((param == "tgr") || (param == "winding_time_constant")) {
        mWindingTimeConstant = units::convert(val, unitType, units::second);
    } else if ((param == "alarmtemp") || (param == "alarmtemp1")) {
        mAlarmTemp1 = units::convert(val, unitType, units::degC);
        if (opFlags[dyn_initialized]) {
            getCondition(0)->setConditionRHS(mAlarmTemp1);
            setConditionStatus(0,
                               (mAlarmTemp1 > 0.1) ? condition_status_t::active :
                                                     condition_status_t::disabled);
        }
    } else if (param == "alarmtemp2") {
        mAlarmTemp2 = units::convert(val, unitType, units::degC);
        if (opFlags[dyn_initialized]) {
            getCondition(1)->setConditionRHS(mAlarmTemp2);
            setConditionStatus(1,
                               (mAlarmTemp2 > 0.1) ? condition_status_t::active :
                                                     condition_status_t::disabled);
        }
    } else if (param == "cutouttemp") {
        mCutoutTemp = units::convert(val, unitType, units::degC);
        if (opFlags[dyn_initialized]) {
            getCondition(2)->setConditionRHS(mCutoutTemp);
            setConditionStatus(2,
                               (mCutoutTemp > 0.1) ? condition_status_t::active :
                                                     condition_status_t::disabled);
        }
    } else if (param == "alarmdelay") {
        mAlarmDelay = units::convert(val, unitType, units::second);
        if (opFlags[dyn_initialized]) {
            setActionTrigger(0, 0, mAlarmDelay);
            setActionTrigger(1, 1, mAlarmDelay);
            setActionTrigger(2, 2, mAlarmDelay);
            setActionTrigger(3, 2, mAlarmDelay);
        }
    } else if ((param == "lr") || (param == "loss_ratio")) {
        mLossRatio = val;
    } else if ((param == "m") || (param == "winding_exponent")) {
        mWindingExponent = val;
    } else if ((param == "n") || (param == "oil_exponent")) {
        mOilExponent = val;
    } else {
        sensor::set(param, val, unitType);
    }
}

double txThermalModel::get(std::string_view param, units::unit unitType) const
{
    return sensor::get(param, unitType);
}

void txThermalModel::add(CoreObject* /*obj*/)
{
    throw(unrecognizedObjectException(this));
}

void txThermalModel::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (m_sourceObject == nullptr) {
        sensor::dynObjectInitializeA(time0, flags);
        return;
    }

    if (updatePeriod > kHalfBigNum) {  // set the period to the period of the simulation to at
                                       // least 1/5 the winding time constant
        double pstep = getRoot()->get("steptime");
        if (pstep < 0) {
            pstep = 1.0;
        }
        const double mtimestep = mWindingTimeConstant / 5.0;
        updatePeriod = pstep * std::floor(mtimestep / pstep);
        if (updatePeriod < pstep) {
            updatePeriod = pstep;
        }
    }

    mRating = m_sourceObject->get("rating");
    const double basePower = m_sourceObject->get("basepower");
    if (opFlags[auto_parameter_load]) {
        if (mRating * basePower < 2.5) {
            set("cooling", "oa");
        } else if (mRating * basePower < 10) {
            set("cooling", "fa");
        } else if (mRating * basePower < 100) {
            set("cooling", "fahot");
        } else if (mRating * basePower < 200) {
            set("cooling", "ndfoa");
        } else {
            set("cooling", "dfoa");
        }
    }
    const double resistance = m_sourceObject->get("r");
    const double conductance = m_sourceObject->get("g");  // get conductance
    mRatedLoss = (mRating * mRating * resistance) + conductance;  // loss is I^2*r+g in per unit;

    if (conductance > 0.0) {
        mLossRatio = mRatedLoss / conductance;
    }

    mRadiationConstant = mRatedLoss / mRatedTopOilRise;  // compute the radiation constant
    mThermalCapacity =
        mOilTimeConstant * mRatedLoss / mRatedTopOilRise;  // compute the thermal mass constant
    if (!opFlags[dyn_initialized]) {
        sensor::setFlag("sampled", true);
        sensor::set("input0", "current");
        sensor::set("input1", "loss");
        sensor::set("input2", "attached");

        auto* topOilDelayBlock = new blocks::delayBlock(mOilTimeConstant);
        auto* windingDelayBlock = new blocks::delayBlock(mWindingTimeConstant);

        sensor::add(topOilDelayBlock);
        sensor::add(windingDelayBlock);
        topOilDelayBlock->parentSetFlag(separate_processing, true, this);
        windingDelayBlock->parentSetFlag(separate_processing, true, this);
        auto ambientGrabber = std::make_shared<customGrabber>();
        ambientGrabber->setGrabberFunction("ambient", [this](CoreObject* /*unused*/) -> double {
            return mAmbientTemp;
        });
        sensor::add(ambientGrabber);

        m_outputSize = (m_outputSize > 3) ? m_outputSize : 3;

        outputMode.resize(m_outputSize);
        outputs.resize(m_outputSize);
        outGrabbers.resize(m_outputSize, nullptr);
        outputMode[0] = OutputMode::DIRECT;
        outputMode[1] = OutputMode::BLOCK;
        outputs[0] =
            3;  // the first input was setup as the current, second as the loss, 3rd as attached
        outputs[1] = 0;
        sensor::set("output2", "block0+block1");
        auto condition = make_condition("output1", ">", mAlarmTemp1, this);
        Relay::add(std::shared_ptr<Condition>(std::move(condition)));
        condition = make_condition("output1", ">", mAlarmTemp2, this);
        Relay::add(std::shared_ptr<Condition>(std::move(condition)));
        condition = make_condition("output1", ">", mCutoutTemp, this);
        Relay::add(std::shared_ptr<Condition>(std::move(condition)));

        Relay::set("action", "alarm temperature_alarm1");  // NOLINT
        Relay::set("action", "alarm temperature_alarm2");  // NOLINT
        auto generatedEvent = std::make_unique<Event>();
        generatedEvent->setTarget(m_sinkObject, "switch1");
        generatedEvent->setValue(1.0);

        Relay::add(std::shared_ptr<Event>(std::move(generatedEvent)));
        generatedEvent = std::make_unique<Event>();

        generatedEvent->setTarget(m_sinkObject, "switch2");
        generatedEvent->setValue(1.0);

        Relay::add(std::shared_ptr<Event>(std::move(generatedEvent)));
        // add the triggers
        setActionTrigger(0, 0, mAlarmDelay);
        setActionTrigger(1, 1, mAlarmDelay);
        setActionTrigger(2, 2, mAlarmDelay);
        setActionTrigger(3, 2, mAlarmDelay);

        if (mAlarmTemp1 <= 0.1) {
            setConditionStatus(0, condition_status_t::disabled);
        }
        if (mAlarmTemp2 <= 0.1) {
            setConditionStatus(1, condition_status_t::disabled);
        }
        if (mCutoutTemp <= 0.1) {
            setConditionStatus(2, condition_status_t::disabled);
        }
    }
    sensor::dynObjectInitializeA(time0, flags);
}

void txThermalModel::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    dataSources[0]->setGain(1.0 / mRating);
    dataSources[1]->setGain(1.0 / mRatedLoss);

    const double currentValue = dataSources[0]->grabData();
    const double lossMultiplier = dataSources[1]->grabData();
    const double attachedState = dataSources[2]->grabData();
    IOdata iset(1);
    if (attachedState > 0.1) {
        const double topOilRise = mRatedTopOilRise *
            pow(((currentValue * currentValue * mLossRatio) + 1) / (mLossRatio + 1), mOilExponent);
        const double windingRise = mRatedHotSpotRise * pow(lossMultiplier, mWindingExponent);

        iset[0] = topOilRise + mAmbientTemp;
        filterBlocks[0]->dynInitializeB(
            iset,
            iset,
            iset);  // I don't care what the result is so I use the same vector for all inputs
        iset[0] = windingRise;
        filterBlocks[1]->dynInitializeB(iset, iset, iset);
    } else {
        iset[0] = mAmbientTemp;
        filterBlocks[0]->dynInitializeB(iset, iset, iset);
        iset[0] = 0;
        filterBlocks[1]->dynInitializeB(iset, iset, iset);
    }
    // skip over sensor::dynInitializeB since the filter blocks are initialized here.
    sensor::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
}

void txThermalModel::updateA(coreTime time)
{
    auto deltaTime = time - prevTime;
    if (deltaTime == timeZero) {
        return;
    }
    const double attachedState = dataSources[2]->grabData();
    mAmbientTemp = mAmbientTemp + deltaTime * mTempRateOfChange;
    if (attachedState > 0.1) {
        const double currentValue = dataSources[0]->grabData();
        const double lossMultiplier = dataSources[1]->grabData();

        const double topOilRise = mRatedTopOilRise *
            pow(((currentValue * currentValue * mLossRatio) + 1) / (mLossRatio + 1), mOilExponent);
        const double windingRise = mRatedHotSpotRise * pow(lossMultiplier, mWindingExponent);

        // update the time constants if required
        if (mOilExponent != 1.0) {
            const double topOilCurrent = filterBlocks[0]->getOutput();
            const double currentRatio = (topOilCurrent - mAmbientTemp) / mRatedTopOilRise;
            const double targetRatio = topOilRise / mRatedTopOilRise;
            const double adjustedTopOilTimeConstant = mOilTimeConstant *
                ((currentRatio - targetRatio) /
                 (pow(currentRatio, 1.0 / mOilExponent) - pow(targetRatio, 1.0 / mOilExponent)));
            filterBlocks[0]->set("t1", adjustedTopOilTimeConstant);
        }
        if (mWindingExponent != 1.0) {
            const double windingCurrent = filterBlocks[1]->getOutput();
            const double currentRatio = windingCurrent / mRatedHotSpotRise;
            const double targetRatio = windingRise / mRatedHotSpotRise;
            const double adjustedWindingTimeConstant = mOilTimeConstant *
                ((currentRatio - targetRatio) /
                 (pow(currentRatio, 1.0 / mWindingExponent) -
                  pow(targetRatio, 1.0 / mWindingExponent)));
            filterBlocks[1]->set("t1", adjustedWindingTimeConstant);
        }

        filterBlocks[0]->step(time, mAmbientTemp + topOilRise);
        filterBlocks[1]->step(time, windingRise);
    } else {
        filterBlocks[0]->step(time, mAmbientTemp);
        filterBlocks[1]->step(time, 0);
    }
    // printf("%f:%s A=%f to(%f)=%f hs(%f)=%f\n",time, name.c_str(), ambientTemp,
    // DTtou+ambientTemp, o1, DTgu, o2);
    sensor::updateA(time);
    prevTime = time;
}

void txThermalModel::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& /*sMode*/)
{
    updateA(time);
}

}  // namespace griddyn::extra
