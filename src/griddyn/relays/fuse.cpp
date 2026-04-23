/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fuse.h"

#include "../Link.h"
#include "../events/Event.h"
#include "../events/eventQueue.h"
#include "../gridBus.h"
#include "../gridSecondary.h"
#include "../measurement/Condition.h"
#include "../measurement/grabberSet.h"
#include "../measurement/gridGrabbers.h"
#include "../measurement/stateGrabber.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixDataSparse.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <print>
#include <string>
#include <utility>

namespace griddyn::relays {
using units::convert;
using units::puA;
fuse::fuse(const std::string& objName): Relay(objName), useI2T(extra_bool)
{
    opFlags.set(continuous_flag);
}

coreObject* fuse::clone(coreObject* obj) const
{
    auto nobj = cloneBase<fuse, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->limit = limit;
    nobj->mp_I2T = mp_I2T;
    nobj->minBlowTime = minBlowTime;
    nobj->Vbase = Vbase;
    nobj->m_terminal = m_terminal;
    return nobj;
}

void fuse::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        Relay::setFlag(flag, val);
    }
}

void fuse::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void fuse::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "limit") {
        limit = convert(val, unitType, puA, systemBasePower, Vbase);
    } else if (param == "i2t") {
        mp_I2T = convert(val, unitType, puA, systemBasePower, Vbase);
    } else if (param == "terminal") {
        m_terminal = static_cast<int>(val);
    } else if (param == "minblowtime") {
        if (val > 0.001) {
            minBlowTime = val;
        } else {
            LOG_WARNING("minimum blow time must be greater or equal to 0.001");
            throw(invalidParameterValue(std::string{param}));
        }
    } else {
        Relay::set(param, val, unitType);
    }
}

void fuse::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    auto ge = std::make_shared<Event>();

    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            make_condition("current" + std::to_string(m_terminal), ">=", limit, m_sourceObject)));
        ge->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        ge->setValue(1.0);
        bus = static_cast<Link*>(m_sourceObject)->getBus(m_terminal);
    } else {
        add(std::shared_ptr<Condition>(
            make_condition("sqrt(p^2+q^2)/@bus:v", ">=", limit, m_sourceObject)));
        opFlags.set(nonlink_source_flag);
        ge->setTarget(m_sinkObject, "status");
        ge->setValue(0.0);
        bus = static_cast<gridBus*>(m_sourceObject->find("bus"));
    }

    add(std::move(ge));
    // now make the Condition for the I2T condition
    auto gc = std::make_unique<Condition>();
    auto gc2 = std::make_unique<Condition>();

    auto cg = std::make_unique<customGrabber>();
    cg->setGrabberFunction("I2T", [this](coreObject*) { return cI2T; });

    auto cgst = std::make_unique<customStateGrabber>(this);
    cgst->setGrabberFunction(
        [](coreObject* obj, const stateData& sD, const solverMode& sMode) -> double {
            return sD.state[static_cast<fuse*>(obj)->offsets.getDiffOffset(sMode)];
        });

    // this one needs to be shared since I use it twice
    auto gset = std::make_shared<grabberSet>(std::move(cg), std::move(cgst));
    gc->setConditionLHS(gset);

    gc2->setConditionLHS(std::move(gset));
    gc->setConditionRHS(mp_I2T);
    gc2->setConditionRHS(-mp_I2T / 2.0);

    gc->setComparison(comparison_type::gt);
    gc2->setComparison(comparison_type::lt);

    add(std::shared_ptr<Condition>(std::move(gc)));
    add(std::shared_ptr<Condition>(std::move(gc2)));
    setConditionStatus(1, condition_status_t::disabled);
    setConditionStatus(2, condition_status_t::disabled);

    cI2T = 0;

    // add the event for setting up the fuse evaluation
    auto ge2 = std::make_unique<functionEventAdapter>([this]() { return setupFuseEvaluation(); });
    add(std::shared_ptr<functionEventAdapter>(std::move(ge2)));
    if (mp_I2T <= 0.0) {
        setActionTrigger(1, 0, 0.0);
    } else {
        setActionTrigger(1, 0, minBlowTime);
    }

    // add the event for blowing the fuse after i2T is exceeded
    auto ge3 = std::make_unique<functionEventAdapter>([this]() { return blowFuse(); });
    add(std::shared_ptr<functionEventAdapter>(std::move(ge3)));
    setActionTrigger(2, 1, 0.0);

    Relay::dynObjectInitializeA(time0, flags);
}

void fuse::conditionTriggered(index_t conditionNum, coreTime /*triggerTime*/)
{
    if (conditionNum == 2) {
        assert(opFlags[overlimit_flag]);

        setConditionStatus(1, condition_status_t::disabled);
        setConditionStatus(2, condition_status_t::disabled);
        setConditionStatus(0, condition_status_t::active);
        alert(this, JAC_COUNT_DECREASE);
        opFlags.reset(overlimit_flag);
        useI2T = false;
    }
}

change_code fuse::blowFuse()
{
    opFlags.set(overlimit_flag);
    setConditionStatus(0, condition_status_t::disabled);
    setConditionStatus(1, condition_status_t::disabled);
    setConditionStatus(2, condition_status_t::disabled);
    alert(this, FUSE_BLOWN_CURRENT);
    LOG_NORMAL("Fuse " + std::to_string(m_terminal) + " blown on object " +
               m_sourceObject->getName());
    opFlags.set(fuse_blown_flag);
    change_code cchange = change_code::non_state_change;
    if (mp_I2T > 0.0) {
        alert(this, JAC_COUNT_DECREASE);
        cchange = change_code::jacobian_change;
    }
    return std::max(triggerAction(0), cchange);
}

change_code fuse::setupFuseEvaluation()
{
    if (mp_I2T <= 0.0) {
        return blowFuse();
    }

    opFlags.set(overlimit_flag);
    setConditionStatus(0, condition_status_t::disabled);
    double I = getConditionValue(0);
    cI2T = I2Tequation(I) * minBlowTime;
    if (cI2T > mp_I2T) {
        return blowFuse();
    }

    setConditionStatus(1, condition_status_t::active);
    setConditionStatus(2, condition_status_t::active);
    alert(this, JAC_COUNT_INCREASE);
    useI2T = true;
    return change_code::jacobian_change;
}

stateSizes fuse::LocalStateSizes(const solverMode& sMode) const
{
    stateSizes SS;
    if ((!isAlgebraicOnly(sMode)) && (mp_I2T > 0.0)) {
        SS.diffSize = 1;
    }
    return SS;
}

count_t fuse::LocalJacobianCount(const solverMode& sMode) const
{
    if ((!isAlgebraicOnly(sMode)) && (mp_I2T > 0.0)) {
        return 12;
    }
    return 0;
}

void fuse::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& /*sMode*/)
{
    if (limit < kBigNum / 2.0) {
        double val = getConditionValue(0);
        if (val > limit) {
            opFlags.set(fuse_blown_flag);
            disable();
            alert(this, FUSE1_BLOWN_CURRENT);
        }
    }
    prevTime = time;
}

void fuse::converge(coreTime time,
                    double state[],
                    double dstate_dt[],
                    const solverMode& sMode,
                    converge_mode /*mode*/,
                    double /*tol*/)
{
    guessState(time, state, dstate_dt, sMode);
}

void fuse::jacobianElements(const IOdata& /*inputs*/,
                            const stateData& sD,
                            matrixData<double>& md,
                            const IOlocs& /*inputLocs*/,
                            const solverMode& sMode)
{
    // TODO(phlpt): Replace matrixDataSparse here with a translation matrix.
    if (useI2T) {
        matrixDataSparse<double> d;
        IOdata out;
        auto Voffset = bus->getOutputLoc(sMode, voltageInLocation);
        auto inputs = bus->getOutputs(noInputs, sD, sMode);
        auto inputLocs = bus->getOutputLocs(sMode);
        if (opFlags[nonlink_source_flag]) {
            auto gs = static_cast<gridSecondary*>(m_sourceObject);
            out = gs->getOutputs(inputs, sD, sMode);
            gs->outputPartialDerivatives(inputs, sD, d, sMode);
            gs->ioPartialDerivatives(inputs, sD, d, inputLocs, sMode);
        } else {
            auto lnk = static_cast<Link*>(m_sourceObject);
            auto bid = bus->getID();
            lnk->updateLocalCache(noInputs, sD, sMode);
            out = lnk->getOutputs(bid, sD, sMode);
            lnk->outputPartialDerivatives(bid, sD, d, sMode);
            lnk->ioPartialDerivatives(bid, sD, d, inputLocs, sMode);
        }

        double I = getConditionValue(0, sD, sMode);

        double voltage = bus->getVoltage(sD, sMode);

        double apparentPower = std::hypot(out[PoutLocation], out[QoutLocation]);
        double temp = 1.0 / (apparentPower * voltage);
        double dIdP = out[PoutLocation] * temp;
        double dIdQ = out[QoutLocation] * temp;
        d.scaleRow(PoutLocation, dIdP);
        d.scaleRow(QoutLocation, dIdQ);

        auto offset = offsets.getDiffOffset(sMode);
        d.translateRow(PoutLocation, offset);
        d.translateRow(QoutLocation, offset);
        d.assignCheck(offset, Voffset, -apparentPower / (voltage * voltage));

        d.scaleRow(offset, 2.0 * I);

        md.merge(d);

        md.assign(offset, offset, -sD.cj);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        md.assign(offset, offset, -sD.cj);
    }
}

void fuse::setState(coreTime time,
                    const double state[],
                    const double /*dstate_dt*/[],
                    const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        cI2T = state[offset];
    }
    prevTime = time;
}

double fuse::I2Tequation(double current)
{
    return (current * current - limit * limit);
}

void fuse::residual(const IOdata& /*inputs*/,
                    const stateData& sD,
                    double resid[],
                    const solverMode& sMode)
{
    if (useI2T) {
        auto offset = offsets.getDiffOffset(sMode);
        const double* dst = sD.dstate_dt + offset;

        if (!opFlags[nonlink_source_flag]) {
            static_cast<Link*>(m_sourceObject)->updateLocalCache(noInputs, sD, sMode);
        }
        double I1 = getConditionValue(0, sD, sMode);
        resid[offset] = I2Tequation(I1) - *dst;
        std::println("tt={}::I1={},limit={}, r[{}]={} deriv={}",
                     static_cast<double>(sD.time),
                     I1,
                     limit,
                     offset,
                     resid[offset],
                     *dst);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        resid[offset] = -sD.dstate_dt[offset];
    }
}

void fuse::guessState(const coreTime /*time*/,
                      double state[],
                      double dstate_dt[],
                      const solverMode& sMode)
{
    if (useI2T) {
        auto offset = offsets.getDiffOffset(sMode);

        double I1 = getConditionValue(0);
        state[offset] = cI2T;
        dstate_dt[offset] = I2Tequation(I1);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        state[offset] = 0;
        dstate_dt[offset] = 0;
    }
}

void fuse::getStateName(stringVec& stNames,
                        const solverMode& sMode,
                        const std::string& prefix) const
{
    if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        if (offset >= static_cast<index_t>(stNames.size())) {
            stNames.resize(offset + 1);
        }
        if (prefix.empty()) {
            stNames[offset] = getName() + ":I2T";
        } else {
            stNames[offset] = prefix + "::" + getName() + ":I2T";
        }
    }
}
}  // namespace griddyn::relays
