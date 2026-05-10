/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "breaker.h"

#include "../Link.h"
#include "../events/Event.h"
#include "../gridBus.h"
#include "../gridSecondary.h"
#include "../measurement/Condition.h"
#include "../measurement/grabberSet.h"
#include "../measurement/gridGrabbers.h"
#include "../measurement/stateGrabber.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixDataSparse.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
using units::convert;
using units::puA;
breaker::breaker(const std::string& objName): Relay(objName), mUseCti(extra_bool)
{
    opFlags.set(continuous_flag);
}

coreObject* breaker::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<breaker, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->mLimit = mLimit;
    nobj->mMinClearingTime = mMinClearingTime;
    nobj->mRecloseTime1 = mRecloseTime1;
    nobj->mRecloseTime2 = mRecloseTime2;
    nobj->mRecloserTap = mRecloserTap;
    nobj->mRecloserResetTime = mRecloserResetTime;
    nobj->mLastRecloseTime = -mLastRecloseTime;
    nobj->mMaxRecloseAttempts = mMaxRecloseAttempts;
    nobj->mLimit = mLimit;
    nobj->m_terminal = m_terminal;
    nobj->mRecloseAttempts = mRecloseAttempts;
    nobj->mCti = mCti;

    nobj->mVoltageBase = mVoltageBase;
    return nobj;
}

void breaker::setFlag(std::string_view flag, bool val)
{
    if (flag == "nondirectional") {
        opFlags.set(nondirectional_flag, val);
    } else {
        Relay::setFlag(flag, val);
    }
}
/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void breaker::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void breaker::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "reclosetime") {
        mRecloseTime1 = val;
        mRecloseTime2 = val;
    } else if (param == "reclosetime1") {
        mRecloseTime1 = val;
    } else if (param == "reclosetime2") {
        mRecloseTime2 = val;
    } else if ((param == "maxrecloseattempts") || (param == "reclosers")) {
        mMaxRecloseAttempts = static_cast<decltype(mMaxRecloseAttempts)>(val);
    } else if ((param == "minclearingtime") || (param == "cleartime")) {
        mMinClearingTime = val;
    } else if (param == "limit") {
        mLimit = convert(val, unitType, puA, systemBasePower, mVoltageBase);
    } else if ((param == "reclosertap") || (param == "tap")) {
        mRecloserTap = val;
    } else if (param == "terminal") {
        m_terminal = static_cast<index_t>(val);
    } else if ((param == "recloserresettime") || (param == "resettime")) {
        mRecloserResetTime = val;
    } else {
        Relay::set(param, val, unitType);
    }
}

void breaker::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    auto tripEvent = std::make_shared<Event>();
    auto recloseEvent = std::make_shared<Event>();
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            make_condition("current" + std::to_string(m_terminal), ">=", mLimit, m_sourceObject)));
        tripEvent->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        tripEvent->setValue(1.0);
        // action 2 to re-close switch
        recloseEvent->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        recloseEvent->setValue(0.0);
        mBus = static_cast<Link*>(m_sourceObject)->getBus(m_terminal);
    } else {
        add(std::shared_ptr<Condition>(
            make_condition("sqrt(p^2+q^2)/@bus:v", ">=", mLimit, m_sourceObject)));
        opFlags.set(nonlink_source_flag);
        tripEvent->setTarget(m_sinkObject, "status");
        tripEvent->setValue(0.0);
        // action 2 to re-enable object
        recloseEvent->setTarget(m_sinkObject, "status");
        recloseEvent->setValue(0.0);
        mBus = static_cast<gridBus*>(m_sourceObject->find("bus"));
    }

    add(std::move(tripEvent));
    add(std::move(recloseEvent));
    // now make the Condition for the I2T condition
    auto upperCtiCondition = std::make_shared<Condition>();
    auto lowerCtiCondition = std::make_shared<Condition>();

    auto ctiGrabber = std::make_unique<customGrabber>();
    ctiGrabber->setGrabberFunction("I2T", [this](coreObject* /*unused*/) { return mCti; });

    auto ctiStateGrabber = std::make_unique<customStateGrabber>(this);
    ctiStateGrabber->setGrabberFunction(
        [](coreObject* obj, const stateData& stateDataRef, const solverMode& sMode) -> double {
            return stateDataRef.state[static_cast<breaker*>(obj)->offsets.getDiffOffset(sMode)];
        });

    auto ctiGrabberSet =
        std::make_shared<grabberSet>(std::move(ctiGrabber), std::move(ctiStateGrabber));
    upperCtiCondition->setConditionLHS(ctiGrabberSet);

    lowerCtiCondition->setConditionLHS(std::move(ctiGrabberSet));

    upperCtiCondition->setConditionRHS(1.0);
    lowerCtiCondition->setConditionRHS(-0.5);
    upperCtiCondition->setComparison(comparison_type::gt);
    lowerCtiCondition->setComparison(comparison_type::lt);

    add(std::move(upperCtiCondition));
    add(std::move(lowerCtiCondition));
    setConditionStatus(1, condition_status_t::disabled);
    setConditionStatus(2, condition_status_t::disabled);

    Relay::dynObjectInitializeA(time0, flags);
}

void breaker::conditionTriggered(index_t conditionNum, coreTime triggeredTime)
{
    if (conditionNum == 0) {
        opFlags.set(overlimit_flag);
        setConditionStatus(0, condition_status_t::disabled);
        if (mRecloserTap == 0.0) {
            if (mMinClearingTime <= kMin_Res) {
                tripBreaker(triggeredTime);
            } else {
                nextUpdateTime = triggeredTime + mMinClearingTime;
                alert(this, UPDATE_TIME_CHANGE);
            }
        } else {
            mCti = 0;
            setConditionStatus(1, condition_status_t::active);
            setConditionStatus(2, condition_status_t::active);
            alert(this, JAC_COUNT_INCREASE);
            mUseCti = true;
        }
    } else if (conditionNum == 1) {
        assert(opFlags[overlimit_flag]);
        tripBreaker(triggeredTime);
    } else if (conditionNum == 2) {
        assert(opFlags[overlimit_flag]);

        setConditionStatus(1, condition_status_t::disabled);
        setConditionStatus(2, condition_status_t::disabled);
        setConditionStatus(0, condition_status_t::active);
        alert(this, JAC_COUNT_DECREASE);
        opFlags.reset(overlimit_flag);
        mUseCti = false;
    }
}

void breaker::updateA(coreTime time)
{
    if (opFlags[breaker_tripped_flag]) {
        if (time >= nextUpdateTime) {
            resetBreaker(time);
        }
    } else if (opFlags[overlimit_flag]) {
        if (time >= nextUpdateTime) {
            if (checkCondition(0)) {  // still over the limit->trip the breaker
                tripBreaker(time);
            } else {
                opFlags.reset(overlimit_flag);
                setConditionStatus(0, condition_status_t::active);
            }
        }
    } else {
        Relay::updateA(time);
    }
    lastUpdateTime = time;
}

stateSizes breaker::LocalStateSizes(const solverMode& sMode) const
{
    stateSizes stateSizeSet;
    if ((!isAlgebraicOnly(sMode)) && (mRecloserTap > 0)) {
        stateSizeSet.diffSize = 1;
    }
    return stateSizeSet;
}

count_t breaker::LocalJacobianCount(const solverMode& sMode) const
{
    if ((!isAlgebraicOnly(sMode)) && (mRecloserTap > 0)) {
        return 12;
    }
    return 0;
}

void breaker::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& /*sMode*/)
{
    prevTime = time;
    if (mLimit < kBigNum / 2.0) {
        const double conditionValue = getConditionValue(0);
        if (conditionValue > mLimit) {
            opFlags.set(breaker_tripped_flag);
            disable();
            alert(this, BREAKER_TRIP_CURRENT);
        }
    }
}

void breaker::jacobianElements(const IOdata& /*inputs*/,
                               const stateData& stateDataRef,
                               matrixData<double>& jacobian,
                               const IOlocs& /*inputLocs*/,
                               const solverMode& sMode)
{
    if (mUseCti) {
        matrixDataSparse<double> localJacobian;
        IOdata out;
        auto voltageOffset = mBus->getOutputLoc(sMode, voltageInLocation);
        auto inputs = mBus->getOutputs(noInputs, stateDataRef, sMode);
        auto inputLocs = mBus->getOutputLocs(sMode);
        if (opFlags[nonlink_source_flag]) {
            auto* gridSecondaryObject = static_cast<gridSecondary*>(m_sourceObject);
            out = gridSecondaryObject->getOutputs(inputs, stateDataRef, sMode);
            gridSecondaryObject->outputPartialDerivatives(inputs,
                                                          stateDataRef,
                                                          localJacobian,
                                                          sMode);
            gridSecondaryObject->ioPartialDerivatives(
                inputs, stateDataRef, localJacobian, inputLocs, sMode);
        } else {
            auto* lnk = static_cast<Link*>(m_sourceObject);
            auto busId = mBus->getID();
            lnk->updateLocalCache(noInputs, stateDataRef, sMode);
            out = lnk->getOutputs(busId, stateDataRef, sMode);
            lnk->outputPartialDerivatives(busId, stateDataRef, localJacobian, sMode);
            lnk->ioPartialDerivatives(busId, stateDataRef, localJacobian, inputLocs, sMode);
        }

        auto offset = offsets.getDiffOffset(sMode);

        const double currentMagnitude = getConditionValue(0, stateDataRef, sMode);
        const double voltage = mBus->getVoltage(stateDataRef, sMode);
        const double apparentPower = std::hypot(out[PoutLocation], out[QoutLocation]);
        const double inverseScale = 1.0 / (apparentPower * voltage);
        const double dIdP = out[PoutLocation] * inverseScale;
        const double dIdQ = out[QoutLocation] * inverseScale;

        localJacobian.scaleRow(PoutLocation, dIdP);
        localJacobian.scaleRow(QoutLocation, dIdQ);
        localJacobian.translateRow(PoutLocation, offset);
        localJacobian.translateRow(QoutLocation, offset);

        localJacobian.assignCheck(offset, voltageOffset, -apparentPower / (voltage * voltage));
        double dRdI;
        if (currentMagnitude > mLimit) {
            dRdI = pow((mRecloserTap / (pow(currentMagnitude - mLimit, 1.5)) + mMinClearingTime),
                       -2.0) *
                (1.5 * mRecloserTap / (pow(currentMagnitude - mLimit, 2.5)));
        } else {
            dRdI = -pow((mRecloserTap / (pow(mLimit - currentMagnitude + 1e-8, 1.5)) +
                         mMinClearingTime),
                        -2.0) *
                (1.5 * mRecloserTap / (pow(mLimit - currentMagnitude + 1e-8, 2.5)));
        }

        localJacobian.scaleRow(offset, dRdI);

        jacobian.merge(localJacobian);

        jacobian.assign(offset, offset, -stateDataRef.cj);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        jacobian.assign(offset, offset, stateDataRef.cj);
    }
}

void breaker::setState(coreTime time,
                       const double state[],
                       const double /*dstate_dt*/[],
                       const solverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        mCti = state[offset];
    }
    prevTime = time;
}

void breaker::residual(const IOdata& /*inputs*/,
                       const stateData& stateDataRef,
                       double resid[],
                       const solverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        const double* dst = stateDataRef.dstate_dt + offset;

        if (!opFlags[nonlink_source_flag]) {
            static_cast<Link*>(m_sourceObject)->updateLocalCache(noInputs, stateDataRef, sMode);
        }
        const double currentMagnitude = getConditionValue(0, stateDataRef, sMode);
        double temp;
        if (currentMagnitude > mLimit) {
            temp = pow(currentMagnitude - mLimit, 1.5);
            resid[offset] = 1.0 / (mRecloserTap / temp + mMinClearingTime) - *dst;
            assert(!std::isnan(resid[offset]));
        } else {
            temp = pow(mLimit - currentMagnitude + 1e-8, 1.5);
            resid[offset] = -1.0 / (mRecloserTap / temp + mMinClearingTime) - *dst;
            assert(!std::isnan(resid[offset]));
        }
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        resid[offset] = stateDataRef.dstate_dt[offset];
    }
}

void breaker::guessState(const coreTime /*time*/,
                         double state[],
                         double dstate_dt[],
                         const solverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        const double currentMagnitude = getConditionValue(0);
        state[offset] = mCti;
        double temp;
        if (currentMagnitude > mLimit) {
            temp = pow(currentMagnitude - mLimit, 1.5);
            dstate_dt[offset] = 1.0 / (mRecloserTap / temp + mMinClearingTime);
        } else {
            temp = pow(mLimit - currentMagnitude + 1e-8, 1.5);
            dstate_dt[offset] = -1.0 / (mRecloserTap / temp + mMinClearingTime);
        }
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        state[offset] = 0;
        dstate_dt[offset] = 0;
    }
}

void breaker::getStateName(stringVec& stNames,
                           const solverMode& sMode,
                           const std::string& prefix) const
{
    if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        if (static_cast<size_t>(offset) >= stNames.size()) {
            stNames.resize(static_cast<size_t>(offset) + 1);
        }
        if (prefix.empty()) {
            stNames[offset] = getName() + ":trigger_proximity";
        } else {
            stNames[offset] = prefix + "::" + getName() + ":trigger_proximity";
        }
    }
}

void breaker::tripBreaker(coreTime time)
{
    alert(this, BREAKER_TRIP_CURRENT);
    logging::normal(this, "breaker {} tripped on {}", m_terminal, m_sourceObject->getName());
    triggerAction(0);
    opFlags.set(breaker_tripped_flag);
    mUseCti = false;
    if (time > mLastRecloseTime + mRecloserResetTime) {
        mRecloseAttempts = 0;
    }
    if ((mRecloseAttempts == 0) && (mRecloseAttempts < mMaxRecloseAttempts)) {
        nextUpdateTime = time + mRecloseTime1;
        alert(this, UPDATE_TIME_CHANGE);
    } else if (mRecloseAttempts < mMaxRecloseAttempts) {
        nextUpdateTime = time + mRecloseTime2;
        alert(this, UPDATE_TIME_CHANGE);
    }
}

void breaker::resetBreaker(coreTime time)
{
    ++mRecloseAttempts;
    mLastRecloseTime = time;
    alert(this, BREAKER_RECLOSE);
    logging::normal(this, "breaker {} reset on {}", m_terminal, m_sourceObject->getName());
    opFlags.reset(breaker_tripped_flag);
    // timestep (time, solverMode::pFlow);
    triggerAction(1);  // reclose the breaker
    nextUpdateTime = maxTime;
    if (!opFlags[nonlink_source_flag]) {  // do a recompute power
        static_cast<Link*>(m_sourceObject)->updateLocalCache();
    }
    if (checkCondition(0)) {
        if (mRecloserTap <= kMin_Res) {
            if (mMinClearingTime <= kMin_Res) {
                tripBreaker(time);
            } else {
                nextUpdateTime = time + mMinClearingTime;
            }
        } else {
            mCti = 0;
            setConditionStatus(1, condition_status_t::active);
            setConditionStatus(2, condition_status_t::active);
            alert(this, JAC_COUNT_INCREASE);
            mUseCti = true;
        }
    } else {
        opFlags.reset(overlimit_flag);
        setConditionStatus(0, condition_status_t::active);
        mUseCti = false;
    }

    alert(this, UPDATE_TIME_CHANGE);
}

}  // namespace griddyn::relays
