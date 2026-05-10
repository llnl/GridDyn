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
    auto ge = std::make_shared<Event>();
    auto ge2 = std::make_shared<Event>();
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            make_condition("current" + std::to_string(m_terminal), ">=", mLimit, m_sourceObject)));
        ge->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        ge->setValue(1.0);
        // action 2 to re-close switch
        ge2->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        ge2->setValue(0.0);
        mBus = static_cast<Link*>(m_sourceObject)->getBus(m_terminal);
    } else {
        add(std::shared_ptr<Condition>(
            make_condition("sqrt(p^2+q^2)/@bus:v", ">=", mLimit, m_sourceObject)));
        opFlags.set(nonlink_source_flag);
        ge->setTarget(m_sinkObject, "status");
        ge->setValue(0.0);
        // action 2 to re-enable object
        ge2->setTarget(m_sinkObject, "status");
        ge2->setValue(0.0);
        mBus = static_cast<gridBus*>(m_sourceObject->find("bus"));
    }

    add(std::move(ge));
    add(std::move(ge2));
    // now make the Condition for the I2T condition
    auto gc = std::make_shared<Condition>();
    auto gc2 = std::make_shared<Condition>();

    auto cg = std::make_unique<customGrabber>();
    cg->setGrabberFunction("I2T", [this](coreObject* /*unused*/) { return mCti; });

    auto cgst = std::make_unique<customStateGrabber>(this);
    cgst->setGrabberFunction(
        [](coreObject* obj, const stateData& sD, const solverMode& sMode) -> double {
            return sD.state[static_cast<breaker*>(obj)->offsets.getDiffOffset(sMode)];
        });

    auto gset = std::make_shared<grabberSet>(std::move(cg), std::move(cgst));
    gc->setConditionLHS(gset);

    gc2->setConditionLHS(std::move(gset));  // done with gset don't need it after this point

    gc->setConditionRHS(1.0);
    gc2->setConditionRHS(-0.5);
    gc->setComparison(comparison_type::gt);
    gc2->setComparison(comparison_type::lt);

    add(std::move(gc));
    add(std::move(gc2));
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
    stateSizes SS;
    if ((!isAlgebraicOnly(sMode)) && (mRecloserTap > 0)) {
        SS.diffSize = 1;
    }
    return SS;
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
        double val = getConditionValue(0);
        if (val > mLimit) {
            opFlags.set(breaker_tripped_flag);
            disable();
            alert(this, BREAKER_TRIP_CURRENT);
        }
    }
}

void breaker::jacobianElements(const IOdata& /*inputs*/,
                               const stateData& sD,
                               matrixData<double>& md,
                               const IOlocs& /*inputLocs*/,
                               const solverMode& sMode)
{
    if (mUseCti) {
        matrixDataSparse<double> d;
        IOdata out;
        auto voltageOffset = mBus->getOutputLoc(sMode, voltageInLocation);
        auto inputs = mBus->getOutputs(noInputs, sD, sMode);
        auto inputLocs = mBus->getOutputLocs(sMode);
        if (opFlags[nonlink_source_flag]) {
            auto* gs = static_cast<gridSecondary*>(m_sourceObject);
            out = gs->getOutputs(inputs, sD, sMode);
            gs->outputPartialDerivatives(inputs, sD, d, sMode);
            gs->ioPartialDerivatives(inputs, sD, d, inputLocs, sMode);
        } else {
            auto* lnk = static_cast<Link*>(m_sourceObject);
            auto busId = mBus->getID();
            lnk->updateLocalCache(noInputs, sD, sMode);
            out = lnk->getOutputs(busId, sD, sMode);
            lnk->outputPartialDerivatives(busId, sD, d, sMode);
            lnk->ioPartialDerivatives(busId, sD, d, inputLocs, sMode);
        }

        auto offset = offsets.getDiffOffset(sMode);

        double I = getConditionValue(0, sD, sMode);

        double voltage = mBus->getVoltage(sD, sMode);

        double apparentPower = std::hypot(out[PoutLocation], out[QoutLocation]);
        double temp = 1.0 / (apparentPower * voltage);
        double dIdP = out[PoutLocation] * temp;
        double dIdQ = out[QoutLocation] * temp;

        d.scaleRow(PoutLocation, dIdP);
        d.scaleRow(QoutLocation, dIdQ);
        d.translateRow(PoutLocation, offset);
        d.translateRow(QoutLocation, offset);

        d.assignCheck(offset, voltageOffset, -apparentPower / (voltage * voltage));
        double dRdI;
        if (I > mLimit) {
            dRdI = pow((mRecloserTap / (pow(I - mLimit, 1.5)) + mMinClearingTime), -2.0) *
                (1.5 * mRecloserTap / (pow(I - mLimit, 2.5)));
        } else {
            dRdI = -pow((mRecloserTap / (pow(mLimit - I + 1e-8, 1.5)) + mMinClearingTime), -2.0) *
                (1.5 * mRecloserTap / (pow(mLimit - I + 1e-8, 2.5)));
        }

        d.scaleRow(offset, dRdI);

        md.merge(d);

        md.assign(offset, offset, -sD.cj);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        md.assign(offset, offset, sD.cj);
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
                       const stateData& sD,
                       double resid[],
                       const solverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        const double* dst = sD.dstate_dt + offset;

        if (!opFlags[nonlink_source_flag]) {
            static_cast<Link*>(m_sourceObject)->updateLocalCache(noInputs, sD, sMode);
        }
        double I1 = getConditionValue(0, sD, sMode);
        double temp;
        if (I1 > mLimit) {
            temp = pow(I1 - mLimit, 1.5);
            resid[offset] = 1.0 / (mRecloserTap / temp + mMinClearingTime) - *dst;
            assert(!std::isnan(resid[offset]));
        } else {
            temp = pow(mLimit - I1 + 1e-8, 1.5);
            resid[offset] = -1.0 / (mRecloserTap / temp + mMinClearingTime) - *dst;
            assert(!std::isnan(resid[offset]));
        }

        // printf("tt=%f::I1=%f, r[%d]=%f, stv=%f\n", sD.time, I1, offset, 1.0 / (recloserTap /
        // temp + minClearingTime),sD.state[offset]);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        resid[offset] = sD.dstate_dt[offset];
    }
}

void breaker::guessState(const coreTime /*time*/,
                         double state[],
                         double dstate_dt[],
                         const solverMode& sMode)
{
    if (mUseCti) {
        auto offset = offsets.getDiffOffset(sMode);
        double I1 = getConditionValue(0);
        state[offset] = mCti;
        double temp;
        if (I1 > mLimit) {
            temp = pow(I1 - mLimit, 1.5);
            dstate_dt[offset] = 1.0 / (mRecloserTap / temp + mMinClearingTime);
        } else {
            temp = pow(mLimit - I1 + 1e-8, 1.5);
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
        if (offset >= static_cast<index_t>(stNames.size())) {
            stNames.resize(offset + 1);
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
