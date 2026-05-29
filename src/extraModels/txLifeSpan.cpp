/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "txLifeSpan.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "griddyn/Link.h"
#include "griddyn/blocks/IntegralBlock.h"
#include "griddyn/events/Event.h"
#include "griddyn/measurement/Condition.h"
#include "griddyn/measurement/GrabberSet.h"
#include "griddyn/measurement/GridGrabbers.h"
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace griddyn::extra {
txLifeSpan::txLifeSpan(const std::string& objName): Sensor(objName)
{
    opFlags.reset(continuous_flag);  // this is a not a continuous model everything is slow so
                                     // no need to make it continuous
    outputStrings = {{"remaininglife", "liferemaining"}, {"lossoflife"}, {"rate", "rateofloss"}};
    m_outputSize = 3;
}

CoreObject* txLifeSpan::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<txLifeSpan, Sensor>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->mInitialLife = mInitialLife;
    nobj->mAgingConstant = mAgingConstant;
    nobj->mBaseTemp = mBaseTemp;
    nobj->mAgingFactor = mAgingFactor;
    return nobj;
}

void txLifeSpan::setFlag(std::string_view flag, bool val)
{
    if ((flag == "useiec") || (flag == "iec")) {
        opFlags.set(useIECmethod, val);
    } else if ((flag == "useieee") || (flag == "ieee")) {
        opFlags.set(useIECmethod, !val);
    } else if (flag == "no_discconect") {
        opFlags.set(no_disconnect, val);
    } else {
        Sensor::setFlag(flag, val);
    }
}

void txLifeSpan::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Sensor::set(param, val);
    }
}

void txLifeSpan::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "initial") || (param == "initiallife")) {
        mInitialLife = units::convert(val, unitType, units::hr);
    } else if (param == "basetemp") {
        mBaseTemp = units::convert(val, unitType, units::degC);
    } else if ((param == "agingrate") || (param == "agingconstant")) {
        mAgingConstant = val;
    } else {
        Sensor::set(param, val, unitType);
    }
}

double txLifeSpan::get(std::string_view param, units::unit unitType) const
{
    return Sensor::get(param, unitType);
}

void txLifeSpan::add(CoreObject* /*obj*/)
{
    throw(UnrecognizedObjectException(this));
}

void txLifeSpan::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (m_sourceObject == nullptr) {
        Sensor::dynObjectInitializeA(time0, flags);
        return;
    }

    if (updatePeriod > negTime) {  // set the period to the period of the simulation to at least
                                   // 1/5 the winding time constant
        coreTime simulationStep = getRoot()->get("steptime");
        if (simulationStep < timeZero) {
            simulationStep = 1.0;
        }
        const coreTime modelTimestep = 120.0;  // update once per minute
        updatePeriod = simulationStep * std::floor(modelTimestep / simulationStep);
        if (updatePeriod < simulationStep) {
            updatePeriod = simulationStep;
        }
    }
    if (!opFlags[dyn_initialized]) {
        Sensor::setFlag("sampled", true);
        if (inputStrings.empty()) {
            // assume we are connected to a temperature sensor
            Sensor::set("input0", "hot_spot");
        }
        auto* lifeIntegrator = new blocks::IntegralBlock(1.0 / 3600);  // add a gain so the
                                                                       // output is in hours
        Sensor::add(lifeIntegrator);
        lifeIntegrator->parentSetFlag(separate_processing, true, this);

        Sensor::set("output0", std::to_string(mInitialLife) + "-block0");
        Sensor::set("output1", "block0");

        auto rateGrabber = std::make_shared<CustomGrabber>();
        rateGrabber->setGrabberFunction("rate", [this](CoreObject*) -> double {
            return mAgingAccelerationFactor;
        });
        Sensor::add(rateGrabber);

        Sensor::set("output2", "input1");
        if (m_sinkObject != nullptr) {
            auto generatedEvent = std::make_unique<Event>();
            generatedEvent->setTarget(m_sinkObject, "g");
            generatedEvent->setValue(100.0);
            Relay::add(std::shared_ptr<Event>(std::move(generatedEvent)));

            generatedEvent = std::make_unique<Event>();
            generatedEvent->setTarget(m_sinkObject, "switch1");
            generatedEvent->setValue(1.0);
            Relay::add(std::shared_ptr<Event>(std::move(generatedEvent)));

            generatedEvent = std::make_unique<Event>();
            generatedEvent->setTarget(m_sinkObject, "switch2");
            generatedEvent->setValue(1.0);
            Relay::add(std::shared_ptr<Event>(std::move(generatedEvent)));

            auto cond = makeCondition("output0", "<", 0, this);
            Relay::add(std::shared_ptr<Condition>(std::move(cond)));

            setActionTrigger(0, 0);
            if (!opFlags[no_disconnect]) {
                setActionTrigger(1, 0);
                setActionTrigger(2, 0);
            }
        }
    }
    Sensor::dynObjectInitializeA(time0, flags);
}
void txLifeSpan::dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet)
{
    IOdata iset{0.0};
    filterBlocks[0]->dynInitializeB(iset, iset, iset);
    Sensor::dynObjectInitializeB(inputs,
                                 desiredOutput,
                                 fieldSet);  // skip over sensor::dynInitializeB since we are
                                             // initializing the blocks here
}

void txLifeSpan::updateA(coreTime time)
{
    if (time == prevTime) {
        return;
    }
    const double temperature = dataSources[0]->grabData();
    if (!opFlags[useIECmethod]) {
        mAgingAccelerationFactor = mAgingFactor *
            exp((mAgingConstant / (mBaseTemp + 273.0)) - (mAgingConstant / (temperature + 273.0)));
    } else {
        mAgingAccelerationFactor = mAgingFactor * exp2((temperature - mBaseTemp + 12) / 6.0);
    }

    filterBlocks[0]->step(time, mAgingAccelerationFactor);
    Sensor::updateA(time);
    prevTime = time;
}

void txLifeSpan::timestep(coreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
{
    updateA(time);
}

void txLifeSpan::actionTaken(index_t actionNumber,
                             index_t /*conditionNum*/,
                             ChangeCode /*actionReturn*/,
                             coreTime /*actionTime*/)
{
    if (m_sinkObject != nullptr) {
        if (actionNumber == 0) {
            logging::normal(this, "{} lifespan exceeded fault produced", m_sinkObject->getName());
        } else if (actionNumber == 1) {
            logging::normal(this, "{} lifespan exceeded breakers tripped", m_sinkObject->getName());
        }
    }
}

}  // namespace griddyn::extra
