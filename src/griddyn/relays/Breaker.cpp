/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Breaker.h"

#include "../GridBus.h"
#include "../GridSecondary.h"
#include "../Link.h"
#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "../measurement/GrabberSet.h"
#include "../measurement/GridGrabbers.h"
#include "../measurement/StateGrabber.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixDataSparse.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::relays {
using units::convert;
using units::puA;
Breaker::Breaker(const std::string& objName): Relay(objName), mUseCti(extra_bool)
{
    opFlags.set(CONTINUOUS_FLAG);
}

CoreObject* Breaker::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<Breaker, Relay>(this, obj);
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

void Breaker::setFlag(std::string_view flag, bool val)
{
    if (flag == "nondirectional") {
        opFlags.set(NONDIRECTIONAL_FLAG, val);
    } else {
        Relay::setFlag(flag, val);
    }
}
/*
std::string commDestName;
std::uint64_t commDestId=0;
std::string commType;
*/
void Breaker::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void Breaker::set(std::string_view param, double val, units::unit unitType)
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

void Breaker::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    auto tripEvent = std::make_shared<Event>();
    auto recloseEvent = std::make_shared<Event>();
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            makeCondition("current" + std::to_string(m_terminal), ">=", mLimit, m_sourceObject)));
        tripEvent->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        tripEvent->setValue(1.0);
        // action 2 to re-close switch
        recloseEvent->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        recloseEvent->setValue(0.0);
        mBus = static_cast<Link*>(m_sourceObject)->getBus(m_terminal);
    } else {
        add(std::shared_ptr<Condition>(
            makeCondition("sqrt(p^2+q^2)/@bus:v", ">=", mLimit, m_sourceObject)));
        opFlags.set(NONLINK_SOURCE_FLAG);
        tripEvent->setTarget(m_sinkObject, "status");
        tripEvent->setValue(0.0);
        // action 2 to re-enable object
        recloseEvent->setTarget(m_sinkObject, "status");
        recloseEvent->setValue(0.0);
        mBus = static_cast<GridBus*>(m_sourceObject->find("bus"));
    }

    add(std::move(tripEvent));
    add(std::move(recloseEvent));
    // now make the Condition for the I2T condition
    auto upperCtiCondition = std::make_shared<Condition>();
    auto lowerCtiCondition = std::make_shared<Condition>();

    auto ctiGrabber = std::make_unique<CustomGrabber>();
    ctiGrabber->setGrabberFunction("I2T", [this](CoreObject* /*unused*/) { return mCti; });

    auto ctiStateGrabber = std::make_unique<CustomStateGrabber>(this);
    ctiStateGrabber->setGrabberFunction(
        [](CoreObject* obj, const StateData& stateDataRef, const SolverMode& sMode) -> double {
            return stateDataRef.state[static_cast<Breaker*>(obj)->offsets.getDiffOffset(sMode)];
        });

    auto ctiGrabberSet =
        std::make_shared<GrabberSet>(std::move(ctiGrabber), std::move(ctiStateGrabber));
    upperCtiCondition->setConditionLHS(ctiGrabberSet);

    lowerCtiCondition->setConditionLHS(std::move(ctiGrabberSet));

    upperCtiCondition->setConditionRHS(1.0);
    lowerCtiCondition->setConditionRHS(-0.5);
    upperCtiCondition->setComparison(ComparisonType::GT);
    lowerCtiCondition->setComparison(ComparisonType::LT);

    add(std::move(upperCtiCondition));
    add(std::move(lowerCtiCondition));
    setConditionStatus(1, ConditionStatus::DISABLED);
    setConditionStatus(2, ConditionStatus::DISABLED);

    Relay::dynObjectInitializeA(time0, flags);
}

void Breaker::conditionTriggered(index_t conditionNum, CoreTime triggeredTime)
{
    if (conditionNum == 0) {
        opFlags.set(OVERLIMIT_FLAG);
        setConditionStatus(0, ConditionStatus::DISABLED);
        if (mRecloserTap == 0.0) {
            if (mMinClearingTime <= kMin_Res) {
                tripBreaker(triggeredTime);
            } else {
                nextUpdateTime = triggeredTime + mMinClearingTime;
                alert(this, UPDATE_TIME_CHANGE);
            }
        } else {
            mCti = 0;
            setConditionStatus(1, ConditionStatus::ACTIVE);
            setConditionStatus(2, ConditionStatus::ACTIVE);
            alert(this, JAC_COUNT_INCREASE);
            mUseCti = true;
        }
    } else if (conditionNum == 1) {
        assert(opFlags[OVERLIMIT_FLAG]);
        tripBreaker(triggeredTime);
    } else if (conditionNum == 2) {
        assert(opFlags[OVERLIMIT_FLAG]);

        setConditionStatus(1, ConditionStatus::DISABLED);
        setConditionStatus(2, ConditionStatus::DISABLED);
        setConditionStatus(0, ConditionStatus::ACTIVE);
        alert(this, JAC_COUNT_DECREASE);
        opFlags.reset(OVERLIMIT_FLAG);
        mUseCti = false;
    }
}

void Breaker::updateA(CoreTime time)
{
    if (opFlags[BREAKER_TRIPPED_FLAG]) {
        if (time >= nextUpdateTime) {
            resetBreaker(time);
        }
    } else if (opFlags[OVERLIMIT_FLAG]) {
        if (time >= nextUpdateTime) {
            if (checkCondition(0)) {  // still over the limit->trip the breaker
                tripBreaker(time);
            } else {
                opFlags.reset(OVERLIMIT_FLAG);
                setConditionStatus(0, ConditionStatus::ACTIVE);
            }
        }
    } else {
        Relay::updateA(time);
    }
    lastUpdateTime = time;
}

StateSizes Breaker::localStateSizes(const SolverMode& sMode) const
{
    StateSizes stateSizeSet;
    if ((!isAlgebraicOnly(sMode)) && (mRecloserTap > 0)) {
        stateSizeSet.diffSize = 1;
    }
    return stateSizeSet;
}

count_t Breaker::localJacobianCount(const SolverMode& sMode) const
{
    if ((!isAlgebraicOnly(sMode)) && (mRecloserTap > 0)) {
        return 12;
    }
    return 0;
}

void Breaker::timestep(CoreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
{
    prevTime = time;
    if (mLimit < kBigNum / 2.0) {
        const double conditionValue = getConditionValue(0);
        if (conditionValue > mLimit) {
            opFlags.set(BREAKER_TRIPPED_FLAG);
            disable();
            alert(this, BREAKER_TRIP_CURRENT);
        }
    }
}

void Breaker::jacobianElements(const IOdata& /*inputs*/,
                               const StateData& stateDataRef,
                               MatrixData<double>& jacobian,
                               const IOlocs& /*inputLocs*/,
                               const SolverMode& sMode)
{
    if (mUseCti) {
        MatrixDataSparse<double> localJacobian;
        IOdata out;
        auto voltageOffset = mBus->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        auto inputs = mBus->getOutputs(noInputs, stateDataRef, sMode);
        auto inputLocs = mBus->getOutputLocs(sMode);
        if (opFlags[NONLINK_SOURCE_FLAG]) {
            auto* gridSecondaryObject = static_cast<GridSecondary*>(m_sourceObject);
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
        const double apparentPower = std::hypot(out[POUT_LOCATION], out[QOUT_LOCATION]);
        const double inverseScale = 1.0 / (apparentPower * voltage);
        const double dIdP = out[POUT_LOCATION] * inverseScale;
        const double dIdQ = out[QOUT_LOCATION] * inverseScale;

        localJacobian.scaleRow(POUT_LOCATION, dIdP);
        localJacobian.scaleRow(QOUT_LOCATION, dIdQ);
        localJacobian.translateRow(POUT_LOCATION, offset);
        localJacobian.translateRow(QOUT_LOCATION, offset);

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

void Breaker::setState(CoreTime time,
                       const double state[],
                       const double /*dstateDt*/[],
                       const SolverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        mCti = state[offset];
    }
    prevTime = time;
}

void Breaker::residual(const IOdata& /*inputs*/,
                       const StateData& stateDataRef,
                       double resid[],
                       const SolverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        const double* dst = stateDataRef.dstate_dt + offset;

        if (!opFlags[NONLINK_SOURCE_FLAG]) {
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

void Breaker::guessState(const CoreTime /*time*/,
                         double state[],
                         double dstateDt[],
                         const SolverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        const double currentMagnitude = getConditionValue(0);
        state[offset] = mCti;
        double temp;
        if (currentMagnitude > mLimit) {
            temp = pow(currentMagnitude - mLimit, 1.5);
            dstateDt[offset] = 1.0 / (mRecloserTap / temp + mMinClearingTime);
        } else {
            temp = pow(mLimit - currentMagnitude + 1e-8, 1.5);
            dstateDt[offset] = -1.0 / (mRecloserTap / temp + mMinClearingTime);
        }
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        state[offset] = 0;
        dstateDt[offset] = 0;
    }
}

void Breaker::getStateName(stringVec& stNames,
                           const SolverMode& sMode,
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

void Breaker::tripBreaker(CoreTime time)
{
    alert(this, BREAKER_TRIP_CURRENT);
    logging::normal(this, "breaker {} tripped on {}", m_terminal, m_sourceObject->getName());
    triggerAction(0);
    opFlags.set(BREAKER_TRIPPED_FLAG);
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

void Breaker::resetBreaker(CoreTime time)
{
    ++mRecloseAttempts;
    mLastRecloseTime = time;
    alert(this, BREAKER_RECLOSE);
    logging::normal(this, "breaker {} reset on {}", m_terminal, m_sourceObject->getName());
    opFlags.reset(BREAKER_TRIPPED_FLAG);
    // timestep (time, SolverMode::pFlow);
    triggerAction(1);  // reclose the breaker
    nextUpdateTime = maxTime;
    if (!opFlags[NONLINK_SOURCE_FLAG]) {  // do a recompute power
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
            setConditionStatus(1, ConditionStatus::ACTIVE);
            setConditionStatus(2, ConditionStatus::ACTIVE);
            alert(this, JAC_COUNT_INCREASE);
            mUseCti = true;
        }
    } else {
        opFlags.reset(OVERLIMIT_FLAG);
        setConditionStatus(0, ConditionStatus::ACTIVE);
        mUseCti = false;
    }

    alert(this, UPDATE_TIME_CHANGE);
}

}  // namespace griddyn::relays
