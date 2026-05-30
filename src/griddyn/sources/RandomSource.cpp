/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RandomSource.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/gridRandom.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace griddyn::sources {
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
    src->opFlags.reset(object_armed_flag);
    src->keyTime = keyTime;
    src->timeDistribution = timeDistribution;
    src->valDistribution = valDistribution;
    src->timeGenerator =
        std::make_unique<utilities::gridRandom>(timeDistribution, param1_t, param2_t);
    src->valGenerator =
        std::make_unique<utilities::gridRandom>(valDistribution, param1_L, param2_L);
    return src;
}

// set properties
void RandomSource::set(std::string_view param, std::string_view val)
{
    if ((param == "trigger_dist") || (param == "time_dist")) {
        timeDistribution = val;
        if (opFlags[dyn_initialized]) {
            timeGenerator->setDistribution(utilities::getDist(timeDistribution));
        }
    } else if ((param == "size_dist") || (param == "change_dist")) {
        valDistribution = val;
        if (opFlags[dyn_initialized]) {
            valGenerator->setDistribution(utilities::getDist(valDistribution));
        }
    } else if (param == "seed") {
        utilities::gridRandom::setSeed();
    } else {
        RampSource::set(param, val);
    }
}

void RandomSource::setFlag(std::string_view flag, bool val)
{
    /*
independentFlag=object_flag3,
INTERPOLATE_FLAG=object_flag4,
REPEATED_FLAG=object_flag5,
PROPORTIONAL_FLAG=object_flag6,
TRIGGERED_FLAG=object_flag7,
armedFlag=object_flag8,*/
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
    } else if (flag == "repeated") {
        opFlags.set(REPEATED_FLAG, val);
    } else if (flag == "proportional") {
        opFlags.set(PROPORTIONAL_FLAG, val);
    } else {
        Source::setFlag(flag, val);
    }
}

void RandomSource::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "min_t") {
        if (val <= 0.0) {
            throw(InvalidParameterValue(param));
        }
        param1_t = val;
        timeParamUpdate();
    } else if ((param == "max_t") || (param == "param2_t")) {
        param2_t = val;
    } else if ((param == "min_l") || (param == "param1_l") || (param == "mean_l")) {
        param1_L = val;
    } else if ((param == "max_l") || (param == "param2_l") || (param == "stdev_l")) {
        param2_L = val;
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
    } else if (param == "param1_t") {
        param1_t = val;
    } else if (param == "zbias") {
        zbias = val;
    } else if (param == "seed") {
        utilities::gridRandom::setSeed(static_cast<int>(val));
    } else {
        // I am purposely skipping over the RampLoad the functionality is needed but the access
        // is not
        RampSource::set(param, val, unitType);
    }
}

void RandomSource::reset(ResetLevels /*level*/)
{
    opFlags.reset(TRIGGERED_FLAG);
    opFlags.reset(object_armed_flag);
    offset = 0.0;
}

void RandomSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t /*flags*/)
{
    reset();
    keyTime = time0;
    timeGenerator = std::make_unique<utilities::gridRandom>(timeDistribution, param1_t, param2_t);
    valGenerator = std::make_unique<utilities::gridRandom>(valDistribution, param1_L, param2_L);
    const coreTime triggerTime = time0 + ntime();

    if (opFlags[INTERPOLATE_FLAG]) {
        nextStep(triggerTime);
    }
    nextUpdateTime = triggerTime;
    opFlags.set(object_armed_flag);
}

void RandomSource::updateOutput(coreTime time)
{
    if (time >= nextUpdateTime) {
        updateA(time);
    }

    RampSource::updateOutput(time);
}

// Repeats only when multiple pending trigger times must be consumed.
// NOLINTNEXTLINE(misc-no-recursion)
void RandomSource::updateA(coreTime time)
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
            opFlags.set(object_armed_flag, false);
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
            opFlags.reset(object_armed_flag);
        }
        prevTime = time;
        keyTime = time;
    }
    if (nextUpdateTime <= time) {
        updateA(time);
    }
    m_tempOut = m_output;
}

coreTime RandomSource::ntime()
{
    coreTime newTime = maxTime;
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

void RandomSource::nextStep(coreTime triggerTime)
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

void RandomSource::timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    while (time >= nextUpdateTime) {
        updateA(nextUpdateTime);
    }

    RampSource::timestep(time, inputs, sMode);
}

void RandomSource::timeParamUpdate()
{
    if (opFlags[dyn_initialized]) {
        timeGenerator->setParameters(param1_t, param2_t);
    }
}
void RandomSource::valParamUpdate()
{
    if (opFlags[dyn_initialized]) {
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
        case utilities::gridRandom::DistributionType::UNIFORM:
            bias = -(param2_L - param1_L) * zbias * (offset);
            break;
        case utilities::gridRandom::DistributionType::EXPONENTIAL:  // load varies in a
                                                                    // biexponential pattern
            bias = ((offset / param1_L) * zbias) - 0.5;

            break;
        case utilities::gridRandom::DistributionType::NORMAL:
            bias = -param2_L * zbias * (offset);
            break;
        default:
            break;
    }
    return bias;
}
}  // namespace griddyn::sources
