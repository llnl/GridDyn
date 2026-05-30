/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Fuse.h"

#include "../GridBus.h"
#include "../GridSecondary.h"
#include "../Link.h"
#include "../events/Event.h"
#include "../events/EventQueue.h"
#include "../measurement/Condition.h"
#include "../measurement/GrabberSet.h"
#include "../measurement/GridGrabbers.h"
#include "../measurement/StateGrabber.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/matrixDataSparse.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <print>
#include <string>
#include <utility>

// NOLINTBEGIN
namespace griddyn::relays {
using units::convert;
using units::puA;
Fuse::Fuse(const std::string& objName): Relay(objName), useI2T(extra_bool)
{
    opFlags.set(continuousFlag);
}

CoreObject* Fuse::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<Fuse, Relay>(this, obj);
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

void Fuse::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        Relay::setFlag(flag, val);
    }
}

void Fuse::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Relay::set(param, val);
    }
}

void Fuse::set(std::string_view param, double val, units::unit unitType)
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
            logging::warning(this, "minimum blow time must be greater or equal to 0.001");
            throw(InvalidParameterValue(param));
        }
    } else {
        Relay::set(param, val, unitType);
    }
}

void Fuse::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    auto ge = std::make_shared<Event>();

    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(
            makeCondition("current" + std::to_string(m_terminal), ">=", limit, m_sourceObject)));
        ge->setTarget(m_sinkObject, "switch" + std::to_string(m_terminal));
        ge->setValue(1.0);
        bus = static_cast<Link*>(m_sourceObject)->getBus(m_terminal);
    } else {
        add(std::shared_ptr<Condition>(
            makeCondition("sqrt(p^2+q^2)/@bus:v", ">=", limit, m_sourceObject)));
        opFlags.set(nonlinkSourceFlag);
        ge->setTarget(m_sinkObject, "status");
        ge->setValue(0.0);
        bus = static_cast<GridBus*>(m_sourceObject->find("bus"));
    }

    add(std::move(ge));
    // now make the Condition for the I2T condition
    auto gc = std::make_unique<Condition>();
    auto gc2 = std::make_unique<Condition>();

    auto cg = std::make_unique<CustomGrabber>();
    cg->setGrabberFunction("I2T", [this](CoreObject*) { return cI2T; });

    auto cgst = std::make_unique<CustomStateGrabber>(this);
    cgst->setGrabberFunction(
        [](CoreObject* obj, const StateData& stateDataRef, const SolverMode& sMode) -> double {
            return stateDataRef.state[static_cast<Fuse*>(obj)->offsets.getDiffOffset(sMode)];
        });

    // this one needs to be shared since I use it twice
    auto gset = std::make_shared<GrabberSet>(std::move(cg), std::move(cgst));
    gc->setConditionLHS(gset);

    gc2->setConditionLHS(std::move(gset));
    gc->setConditionRHS(mp_I2T);
    gc2->setConditionRHS(-mp_I2T / 2.0);

    gc->setComparison(ComparisonType::GT);
    gc2->setComparison(ComparisonType::LT);

    add(std::shared_ptr<Condition>(std::move(gc)));
    add(std::shared_ptr<Condition>(std::move(gc2)));
    setConditionStatus(1, ConditionStatus::disabled);
    setConditionStatus(2, ConditionStatus::disabled);

    cI2T = 0;

    // add the event for setting up the fuse evaluation
    auto ge2 = std::make_unique<FunctionEventAdapter>([this]() { return setupFuseEvaluation(); });
    add(std::shared_ptr<FunctionEventAdapter>(std::move(ge2)));
    if (mp_I2T <= 0.0) {
        setActionTrigger(1, 0, 0.0);
    } else {
        setActionTrigger(1, 0, minBlowTime);
    }

    // add the event for blowing the fuse after i2T is exceeded
    auto ge3 = std::make_unique<FunctionEventAdapter>([this]() { return blowFuse(); });
    add(std::shared_ptr<FunctionEventAdapter>(std::move(ge3)));
    setActionTrigger(2, 1, 0.0);

    Relay::dynObjectInitializeA(time0, flags);
}

void Fuse::conditionTriggered(index_t conditionNum, coreTime /*triggerTime*/)
{
    if (conditionNum == 2) {
        assert(opFlags[overlimitFlag]);

        setConditionStatus(1, ConditionStatus::disabled);
        setConditionStatus(2, ConditionStatus::disabled);
        setConditionStatus(0, ConditionStatus::active);
        alert(this, JAC_COUNT_DECREASE);
        opFlags.reset(overlimitFlag);
        useI2T = false;
    }
}

ChangeCode Fuse::blowFuse()
{
    opFlags.set(overlimitFlag);
    setConditionStatus(0, ConditionStatus::disabled);
    setConditionStatus(1, ConditionStatus::disabled);
    setConditionStatus(2, ConditionStatus::disabled);
    alert(this, FUSE_BLOWN_CURRENT);
    logging::normal(this, "Fuse {} blown on object {}", m_terminal, m_sourceObject->getName());
    opFlags.set(fuseBlownFlag);
    ChangeCode cchange = ChangeCode::NON_STATE_CHANGE;
    if (mp_I2T > 0.0) {
        alert(this, JAC_COUNT_DECREASE);
        cchange = ChangeCode::JACOBIAN_CHANGE;
    }
    return std::max(triggerAction(0), cchange);
}

ChangeCode Fuse::setupFuseEvaluation()
{
    if (mp_I2T <= 0.0) {
        return blowFuse();
    }

    opFlags.set(overlimitFlag);
    setConditionStatus(0, ConditionStatus::disabled);
    double I = getConditionValue(0);
    cI2T = I2Tequation(I) * minBlowTime;
    if (cI2T > mp_I2T) {
        return blowFuse();
    }

    setConditionStatus(1, ConditionStatus::active);
    setConditionStatus(2, ConditionStatus::active);
    alert(this, JAC_COUNT_INCREASE);
    useI2T = true;
    return ChangeCode::JACOBIAN_CHANGE;
}

StateSizes Fuse::localStateSizes(const SolverMode& sMode) const
{
    StateSizes stateSizeSet;
    if ((!isAlgebraicOnly(sMode)) && (mp_I2T > 0.0)) {
        stateSizeSet.diffSize = 1;
    }
    return stateSizeSet;
}

count_t Fuse::localJacobianCount(const SolverMode& sMode) const
{
    if ((!isAlgebraicOnly(sMode)) && (mp_I2T > 0.0)) {
        return 12;
    }
    return 0;
}

void Fuse::timestep(coreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
{
    if (limit < kBigNum / 2.0) {
        double val = getConditionValue(0);
        if (val > limit) {
            opFlags.set(fuseBlownFlag);
            disable();
            alert(this, FUSE1_BLOWN_CURRENT);
        }
    }
    prevTime = time;
}

void Fuse::converge(coreTime time,
                    double state[],
                    double dstateDt[],
                    const SolverMode& sMode,
                    ConvergeMode /*mode*/,
                    double /*tol*/)
{
    guessState(time, state, dstateDt, sMode);
}

void Fuse::jacobianElements(const IOdata& /*inputs*/,
                            const StateData& stateDataRef,
                            matrixData<double>& jacobian,
                            const IOlocs& /*inputLocs*/,
                            const SolverMode& sMode)
{
    // TODO(phlpt): Replace matrixDataSparse here with a translation matrix.
    if (useI2T) {
        matrixDataSparse<double> localJacobian;
        IOdata out;
        auto voltageOffset = bus->getOutputLoc(sMode, voltageInLocation);
        auto inputs = bus->getOutputs(noInputs, stateDataRef, sMode);
        auto inputLocs = bus->getOutputLocs(sMode);
        if (opFlags[nonlinkSourceFlag]) {
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
            auto busId = bus->getID();
            lnk->updateLocalCache(noInputs, stateDataRef, sMode);
            out = lnk->getOutputs(busId, stateDataRef, sMode);
            lnk->outputPartialDerivatives(busId, stateDataRef, localJacobian, sMode);
            lnk->ioPartialDerivatives(busId, stateDataRef, localJacobian, inputLocs, sMode);
        }

        const double currentMagnitude = getConditionValue(0, stateDataRef, sMode);

        const double voltage = bus->getVoltage(stateDataRef, sMode);

        const double apparentPower = std::hypot(out[PoutLocation], out[QoutLocation]);
        const double inverseScale = 1.0 / (apparentPower * voltage);
        const double dIdP = out[PoutLocation] * inverseScale;
        const double dIdQ = out[QoutLocation] * inverseScale;
        localJacobian.scaleRow(PoutLocation, dIdP);
        localJacobian.scaleRow(QoutLocation, dIdQ);

        auto offset = offsets.getDiffOffset(sMode);
        localJacobian.translateRow(PoutLocation, offset);
        localJacobian.translateRow(QoutLocation, offset);
        localJacobian.assignCheck(offset, voltageOffset, -apparentPower / (voltage * voltage));

        localJacobian.scaleRow(offset, 2.0 * currentMagnitude);

        jacobian.merge(localJacobian);

        jacobian.assign(offset, offset, -stateDataRef.cj);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        jacobian.assign(offset, offset, -stateDataRef.cj);
    }
}

void Fuse::setState(coreTime time,
                    const double state[],
                    const double /*dstateDt*/[],
                    const SolverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        cI2T = state[offset];
    }
    prevTime = time;
}

double Fuse::I2Tequation(double current)
{
    return (current * current - limit * limit);
}

void Fuse::residual(const IOdata& /*inputs*/,
                    const StateData& stateDataRef,
                    double resid[],
                    const SolverMode& sMode)
{
    if (useI2T) {
        auto offset = offsets.getDiffOffset(sMode);
        const double* dst = stateDataRef.dstate_dt + offset;

        if (!opFlags[nonlinkSourceFlag]) {
            static_cast<Link*>(m_sourceObject)->updateLocalCache(noInputs, stateDataRef, sMode);
        }
        const double currentMagnitude = getConditionValue(0, stateDataRef, sMode);
        resid[offset] = I2Tequation(currentMagnitude) - *dst;
        std::println("tt={}::I1={},limit={}, r[{}]={} deriv={}",
                     static_cast<double>(stateDataRef.time),
                     currentMagnitude,
                     limit,
                     offset,
                     resid[offset],
                     *dst);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        resid[offset] = -stateDataRef.dstate_dt[offset];
    }
}

void Fuse::guessState(const coreTime /*time*/,
                      double state[],
                      double dstateDt[],
                      const SolverMode& sMode)
{
    if (useI2T) {
        auto offset = offsets.getDiffOffset(sMode);

        const double currentMagnitude = getConditionValue(0);
        state[offset] = cI2T;
        dstateDt[offset] = I2Tequation(currentMagnitude);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getDiffOffset(sMode);
        state[offset] = 0;
        dstateDt[offset] = 0;
    }
}

void Fuse::getStateName(stringVec& stNames,
                        const SolverMode& sMode,
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
// NOLINTEND
