/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "randomSource.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/gridRandom.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using gmlc::utilities::convertToLowerCase;

namespace griddyn {
namespace sources {
    randomSource::randomSource(const std::string& objName, double startVal):
        rampSource(objName, startVal), param1_L(startVal)
    {
    }

    randomSource::~randomSource() = default;

    coreObject* randomSource::clone(coreObject* obj) const
    {
        auto src = cloneBase<randomSource, rampSource>(this, obj);
        if (src == nullptr) {
            return obj;
        }
        src->param1_t = param1_t;
        src->param2_t = param2_t;
        src->param1_L = param1_L;
        src->param2_L = param2_L;
        src->opFlags.reset(triggered_flag);
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
    void randomSource::set(const std::string& param, const std::string& val)
    {
        if ((param == "trigger_dist") || (param == "time_dist")) {
            timeDistribution = convertToLowerCase(val);
            if (opFlags[dyn_initialized]) {
                timeGenerator->setDistribution(utilities::getDist(timeDistribution));
            }
        } else if ((param == "size_dist") || (param == "change_dist")) {
            valDistribution = convertToLowerCase(val);
            if (opFlags[dyn_initialized]) {
                valGenerator->setDistribution(utilities::getDist(valDistribution));
            }
        } else if (param == "seed") {
            utilities::gridRandom::setSeed();
        } else {
            Source::set(param, val);
        }
    }

    void randomSource::setFlag(const std::string& flag, bool val)
    {
        /*
    independent_flag=object_flag3,
    interpolate_flag=object_flag4,
    repeated_flag=object_flag5,
    proportional_flag=object_flag6,
    triggered_flag=object_flag7,
    armed_flag=object_flag8,*/
        if (flag == "interpolate") {
            opFlags.set(interpolate_flag, val);
            if (!val) {
                mp_dOdt = 0.0;
            }
        } else if (flag == "step") {
            opFlags.set(interpolate_flag, !val);
            if (val) {
                mp_dOdt = 0.0;
            }
        } else if (flag == "repeated") {
            opFlags.set(repeated_flag, val);
        } else if (flag == "proportional") {
            opFlags.set(proportional_flag, val);
        } else {
            Source::setFlag(flag, val);
        }
    }

    void randomSource::set(const std::string& param, double val, units::unit unitType)
    {
        if (param == "min_t") {
            if (val <= 0.0) {
                throw(invalidParameterValue(param));
            }
            param1_t = val;
            timeParamUpdate();
        } else if (param == "max_t") {
            param2_t = val;
        } else if ((param == "min_l") || (param == "param1_l") || (param == "mean_l")) {
            param1_L = val;
        } else if ((param == "max_l") || (param == "param2_l") || (param == "stdev_l")) {
            param2_L = val;
        } else if (param == "mean_t") {
            if (val <= 0.0) {
                LOG_WARNING("mean_t parameter must be > 0");
                throw(invalidParameterValue(param));
            }
            param1_t = val;
        } else if (param == "scale_t") {
            if (val <= 0.0) {
                LOG_WARNING("scale_t parameter must be > 0");
                throw(invalidParameterValue(param));
            }
            param2_t = val;
        } else if (param == "param1_t") {
            param1_t = val;
        } else if (param == "param2_t") {
            param2_t = val;
        } else if (param == "zbias") {
            zbias = val;
        } else if (param == "seed") {
            utilities::gridRandom::setSeed(static_cast<int>(val));
        } else {
            // I am purposely skipping over the rampLoad the functionality is needed but the access
            // is not
            Source::set(param, val, unitType);
        }
    }

    void randomSource::reset(reset_levels /*level*/)
    {
        opFlags.reset(triggered_flag);
        opFlags.reset(object_armed_flag);
        offset = 0.0;
    }

    void randomSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t /*flags*/)
    {
        reset();
        keyTime = time0;
        timeGenerator =
            std::make_unique<utilities::gridRandom>(timeDistribution, param1_t, param2_t);
        valGenerator = std::make_unique<utilities::gridRandom>(valDistribution, param1_L, param2_L);
        coreTime triggerTime = time0 + ntime();

        if (opFlags[interpolate_flag]) {
            nextStep(triggerTime);
        }
        nextUpdateTime = triggerTime;
        opFlags.set(object_armed_flag);
    }

    void randomSource::updateOutput(coreTime time)
    {
        if (time >= nextUpdateTime) {
            updateA(time);
        }

        rampSource::updateOutput(time);
    }

    void randomSource::updateA(coreTime time)
    {
        if (time < nextUpdateTime) {
            return;
        }

        lastUpdateTime = nextUpdateTime;
        opFlags.set(triggered_flag);
        auto triggerTime = lastUpdateTime + ntime();
        if (opFlags[interpolate_flag]) {
            rampSource::setState(nextUpdateTime, nullptr, nullptr, cLocalSolverMode);
            if (opFlags[repeated_flag]) {
                nextStep(triggerTime);
                nextUpdateTime = triggerTime;
                rampSource::setState(time, nullptr, nullptr, cLocalSolverMode);
            } else {
                rampSource::clearRamp();
                nextUpdateTime = maxTime;
                opFlags.set(object_armed_flag, false);
                prevTime = time;
                keyTime = time;
            }
        } else {
            double rval = nval();

            m_output = (opFlags[proportional_flag]) ? m_output + rval * m_output : m_output + rval;

            if (opFlags[repeated_flag]) {
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

    coreTime randomSource::ntime()
    {
        coreTime newTime = maxTime;
        do {
            newTime = timeGenerator->generate();
        } while (newTime < 0.0);

        return newTime;
    }

    double randomSource::nval()
    {
        double nextVal = valGenerator->generate();
        nextVal += computeBiasAdjust();

        offset = offset + nextVal;
        return nextVal;
    }

    void randomSource::nextStep(coreTime triggerTime)
    {
        double rval = nval();
        double nextVal =
            (opFlags[proportional_flag]) ? m_output + rval * m_output : m_output + rval;
        if (opFlags[interpolate_flag]) {
            mp_dOdt = (nextVal - m_output) / (triggerTime - keyTime);
        } else {
            mp_dOdt = 0.0;
        }
    }

    void randomSource::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
    {
        while (time >= nextUpdateTime) {
            updateA(nextUpdateTime);
        }

        rampSource::timestep(time, inputs, sMode);
    }

    void randomSource::timeParamUpdate()
    {
        if (opFlags[dyn_initialized]) {
            timeGenerator->setParameters(param1_t, param2_t);
        }
    }
    void randomSource::valParamUpdate()
    {
        if (opFlags[dyn_initialized]) {
            valGenerator->setParameters(param1_L, param2_L);
        }
    }

    double randomSource::computeBiasAdjust()
    {
        if (zbias == 0.0) {
            return 0.0;
        }
        double bias = 0.0;
        switch (valGenerator->getDistribution()) {
            case utilities::gridRandom::dist_type_t::uniform:
                bias = -(param2_L - param1_L) * zbias * (offset);
                break;
            case utilities::gridRandom::dist_type_t::exponential:  // load varies in a biexponential
                                                                   // pattern
                bias = offset / param1_L * zbias - 0.5;

                break;
            case utilities::gridRandom::dist_type_t::normal:
                bias = -param2_L * zbias * (offset);
                break;
            default:
                break;
        }
        return bias;
    }
}  // namespace sources
}  // namespace griddyn
