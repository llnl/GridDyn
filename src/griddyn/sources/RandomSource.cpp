/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RandomSource.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/GridRandom.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace griddyn::sources {
const ModelDescription& RandomSource::getModelDescription() const
{
    static const auto description = ModelDescription::define<RandomSource>(
        "RandomSource",
        &RampSource::getModelDescription(),
        {flag({"repeated"}, &RandomSource::opFlags, REPEATED_FLAG, "Repeated"),
         flag({"proportional"}, &RandomSource::opFlags, PROPORTIONAL_FLAG, "Proportional"),
         command(
             {"trigger_dist", "time_dist"}, units::defunit, "Trigger dist", ModelFieldType::STRING),
         command({"size_dist", "change_dist"}, units::defunit, "Size dist", ModelFieldType::STRING),
         command({"seed"}, units::defunit, "Seed", ModelFieldType::STRING),
         command({"min_t"}, units::defunit, "Min t"),
         parameter({"max_t", "param2_t"},
                   &RandomSource::param2_t,
                   units::defunit,
                   "Parameter 2 for time distribution"),
         parameter({"min_l", "param1_l", "mean_l"},
                   &RandomSource::param1_L,
                   units::defunit,
                   "Parameter 1 for level distribution"),
         parameter({"max_l", "param2_l", "stdev_l"},
                   &RandomSource::param2_L,
                   units::defunit,
                   "Parameter 2 for level distribution"),
         command({"mean_t"}, units::defunit, "Mean t"),
         command({"scale_t"}, units::defunit, "Scale t"),
         parameter({"param1_t"},
                   &RandomSource::param1_t,
                   units::defunit,
                   "Parameter 1 for time distribution"),
         parameter({"zbias"},
                   &RandomSource::zbias,
                   units::defunit,
                   "A factor describing the preference of changes to trend toward zero mean"),
         command({"interpolate"}, units::defunit, "Interpolate", ModelFieldType::BOOLEAN),
         command({"step"}, units::defunit, "Step", ModelFieldType::BOOLEAN)});
    return description;
}

double RandomSource::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            RandomSource::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return RampSource::get(param, unitType);
}

bool RandomSource::getFlag(std::string_view flag) const
{
    if (const auto value = RandomSource::getModelDescription().tryGetFlagLocal(*this, flag)) {
        return *value;
    }
    return RampSource::getFlag(flag);
}

void RandomSource::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = RandomSource::getModelDescription();
    getParamString<RandomSource, RampSource>(this,
                                             pstr,
                                             description.numericNames(),
                                             description.stringNames(),
                                             description.flagNames(),
                                             pstype);
}

RandomSource::RandomSource(const std::string& objName, double startVal):
    RampSource(objName, startVal), param1_L(startVal)
{
}

RandomSource::~RandomSource() = default;

CoreObject* RandomSource::clone(CoreObject* obj) const
{
    auto* src = cloneBase<RandomSource, RampSource>(this, obj);
    if (src == nullptr) {
        return obj;
    }
    src->param1_t = param1_t;
    src->param2_t = param2_t;
    src->param1_L = param1_L;
    src->param2_L = param2_L;
    src->opFlags.reset(TRIGGERED_FLAG);
    src->zbias = zbias;
    src->opFlags.reset(OBJECT_ARMED_FLAG);
    src->keyTime = keyTime;
    src->timeDistribution = timeDistribution;
    src->valDistribution = valDistribution;
    src->timeGenerator =
        std::make_unique<utilities::GridRandom>(timeDistribution, param1_t, param2_t);
    src->valGenerator =
        std::make_unique<utilities::GridRandom>(valDistribution, param1_L, param2_L);
    return src;
}

// set properties
void RandomSource::set(std::string_view param, std::string_view val)
{
    if ((param == "trigger_dist") || (param == "time_dist")) {
        timeDistribution = val;
        if (opFlags[DYN_INITIALIZED]) {
            timeGenerator->setDistribution(utilities::getDist(timeDistribution));
        }
    } else if ((param == "size_dist") || (param == "change_dist")) {
        valDistribution = val;
        if (opFlags[DYN_INITIALIZED]) {
            valGenerator->setDistribution(utilities::getDist(valDistribution));
        }
    } else if (param == "seed") {
        utilities::GridRandom::setSeed();
    } else {
        RampSource::set(param, val);
    }
}

void RandomSource::setFlag(std::string_view flag, bool val)
{
    if (RandomSource::getModelDescription().trySetFlagLocal(*this, flag, val)) {
        return;
    }
    /*
independentFlag=OBJECT_FLAG3,
INTERPOLATE_FLAG=OBJECT_FLAG4,
REPEATED_FLAG=OBJECT_FLAG5,
PROPORTIONAL_FLAG=OBJECT_FLAG6,
TRIGGERED_FLAG=OBJECT_FLAG7,
armedFlag=OBJECT_FLAG8,*/
    if (flag == "interpolate") {
        opFlags.set(INTERPOLATE_FLAG, val);
        if (!val) {
            mp_dOdt = 0.0;
        }
    } else if (flag == "step") {
        opFlags.set(INTERPOLATE_FLAG, !val);
        if (val) {
            mp_dOdt = 0.0;
        }
    } else {
        Source::setFlag(flag, val);
    }
}

void RandomSource::set(std::string_view param, double val, units::unit unitType)
{
    if (RandomSource::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        return;
    }
    if (param == "min_t") {
        if (val <= 0.0) {
            throw(InvalidParameterValue(param));
        }
        param1_t = val;
        timeParamUpdate();
    } else if (param == "mean_t") {
        if (val <= 0.0) {
            logging::warning(this, "mean_t parameter must be > 0");
            throw(InvalidParameterValue(param));
        }
        param1_t = val;
    } else if (param == "scale_t") {
        if (val <= 0.0) {
            logging::warning(this, "scale_t parameter must be > 0");
            throw(InvalidParameterValue(param));
        }
        param2_t = val;
    } else if (param == "seed") {
        utilities::GridRandom::setSeed(static_cast<int>(val));
    } else {
        // I am purposely skipping over the RampLoad the functionality is needed but the access
        // is not
        RampSource::set(param, val, unitType);
    }
}

void RandomSource::reset(ResetLevels /*level*/)
{
    opFlags.reset(TRIGGERED_FLAG);
    opFlags.reset(OBJECT_ARMED_FLAG);
    offset = 0.0;
}

void RandomSource::pFlowObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    reset();
    keyTime = time0;
    timeGenerator = std::make_unique<utilities::GridRandom>(timeDistribution, param1_t, param2_t);
    valGenerator = std::make_unique<utilities::GridRandom>(valDistribution, param1_L, param2_L);
    const CoreTime triggerTime = time0 + ntime();

    if (opFlags[INTERPOLATE_FLAG]) {
        nextStep(triggerTime);
    }
    nextUpdateTime = triggerTime;
    opFlags.set(OBJECT_ARMED_FLAG);
}

void RandomSource::updateOutput(CoreTime time)
{
    if (time >= nextUpdateTime) {
        updateA(time);
    }

    RampSource::updateOutput(time);
}

// Repeats only when multiple pending trigger times must be consumed.
// NOLINTNEXTLINE(misc-no-recursion)
void RandomSource::updateA(CoreTime time)
{
    if (time < nextUpdateTime) {
        return;
    }

    lastUpdateTime = nextUpdateTime;
    opFlags.set(TRIGGERED_FLAG);
    auto triggerTime = lastUpdateTime + ntime();
    if (opFlags[INTERPOLATE_FLAG]) {
        RampSource::setState(nextUpdateTime, nullptr, nullptr, cLocalSolverMode);
        if (opFlags[REPEATED_FLAG]) {
            nextStep(triggerTime);
            nextUpdateTime = triggerTime;
            RampSource::setState(time, nullptr, nullptr, cLocalSolverMode);
        } else {
            RampSource::clearRamp();
            nextUpdateTime = maxTime;
            opFlags.set(OBJECT_ARMED_FLAG, false);
            prevTime = time;
            keyTime = time;
        }
    } else {
        const double rval = nval();

        m_output = (opFlags[PROPORTIONAL_FLAG]) ? m_output + (rval * m_output) : m_output + rval;

        if (opFlags[REPEATED_FLAG]) {
            nextUpdateTime = triggerTime;
        } else {
            nextUpdateTime = maxTime;
            opFlags.reset(OBJECT_ARMED_FLAG);
        }
        prevTime = time;
        keyTime = time;
    }
    if (nextUpdateTime <= time) {
        updateA(time);
    }
    m_tempOut = m_output;
}

CoreTime RandomSource::ntime()
{
    CoreTime newTime = maxTime;
    do {
        newTime = timeGenerator->generate();
    } while (newTime < 0.0);

    return newTime;
}

double RandomSource::nval()
{
    double nextVal = valGenerator->generate();
    nextVal += computeBiasAdjust();

    offset = offset + nextVal;
    return nextVal;
}

void RandomSource::nextStep(CoreTime triggerTime)
{
    const double rval = nval();
    const double nextVal =
        (opFlags[PROPORTIONAL_FLAG]) ? m_output + (rval * m_output) : m_output + rval;
    if (opFlags[INTERPOLATE_FLAG]) {
        mp_dOdt = (nextVal - m_output) / (triggerTime - keyTime);
    } else {
        mp_dOdt = 0.0;
    }
}

void RandomSource::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    while (time >= nextUpdateTime) {
        updateA(nextUpdateTime);
    }

    RampSource::timestep(time, inputs, sMode);
}

void RandomSource::timeParamUpdate()
{
    if (opFlags[DYN_INITIALIZED]) {
        timeGenerator->setParameters(param1_t, param2_t);
    }
}
void RandomSource::valParamUpdate()
{
    if (opFlags[DYN_INITIALIZED]) {
        valGenerator->setParameters(param1_L, param2_L);
    }
}

double RandomSource::computeBiasAdjust()
{
    if (zbias == 0.0) {
        return 0.0;
    }
    double bias = 0.0;
    switch (valGenerator->getDistribution()) {
        case utilities::GridRandom::DistributionType::UNIFORM:
            bias = -(param2_L - param1_L) * zbias * (offset);
            break;
        case utilities::GridRandom::DistributionType::EXPONENTIAL:  // load varies in a
                                                                    // biexponential pattern
            bias = ((offset / param1_L) * zbias) - 0.5;

            break;
        case utilities::GridRandom::DistributionType::NORMAL:
            bias = -param2_L * zbias * (offset);
            break;
        default:
            break;
    }
    return bias;
}
}  // namespace griddyn::sources
