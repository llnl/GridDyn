/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Source.h"

#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "otherSources.h"
#include "sourceTypes.h"
#include <string>

namespace griddyn {
// setup the load object factories
static typeFactory<Source> glf("source",
                               std::to_array<std::string_view>({"basic", "constant", "simple"}),
                               "constant");  // set constant to the default
namespace sources {
    static childTypeFactory<pulseSource, Source> glfp("source", "pulse");
    static childTypeFactory<sineSource, Source>
        cfgsl("source", std::to_array<std::string_view>({"sine", "oscillatory"}));
    static childTypeFactory<rampSource, Source> glfr("source", "ramp");
    static childTypeFactory<randomSource, Source>
        glfrand("source", std::to_array<std::string_view>({"random", "rand"}));
    static childTypeFactory<fileSource, Source> glfld("source", "file");
    static childTypeFactory<grabberSource, Source>
        grbsrc("source", std::to_array<std::string_view>({"grabber", "data"}));
}  // namespace sources

Source::Source(const std::string& objName, double startVal):
    GridSubModel(objName), m_tempOut(startVal)
{
    m_output = startVal;
    m_inputSize = 0;
    m_outputSize = 1;
    opFlags.set(pflow_init_required);
}

CoreObject* Source::clone(CoreObject* obj) const
{
    auto gS = cloneBase<Source, GridSubModel>(this, obj);
    if (gS == nullptr) {
        return obj;
    }
    gS->m_tempOut = m_tempOut;
    gS->lastTime = lastTime;
    gS->purpose_ = purpose_;
    gS->outputUnits_ = outputUnits_;
    return gS;
}

void Source::set(std::string_view param, std::string_view val)
{
    if (param == "purpose") {
        purpose_ = val;
    } else if ((param == "units") || (param == "outputunits")) {
        outputUnits_ = units::unit_cast_from_string(std::string{val});
    } else {
        GridSubModel::set(param, val);
    }
}

void Source::setLevel(double newLevel)
{
    m_tempOut = m_output = newLevel;
}
void Source::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "val") || (param == "setval") || (param == "level") || (param == "value") ||
        (param == "output")) {
        setLevel(units::convert(val, unitType, outputUnits_, systemBasePower, localBaseVoltage));
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

void Source::setState(coreTime time,
                      const double state[],
                      const double dstate_dt[],
                      const solverMode& sMode)
{
    updateOutput(time);
    GridComponent::setState(time, state, dstate_dt, sMode);
    m_tempOut = m_output;
    lastTime = time;
}

void Source::updateOutput(coreTime time)
{
    m_tempOut = computeOutput(time);
    m_output = m_tempOut;
    prevTime = time;
    lastTime = time;
}

void Source::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& /*sMode*/)
{
    if (time != prevTime) {
        updateOutput(time);
        m_tempOut = m_output;
        prevTime = time;
    }
}

count_t Source::outputDependencyCount(index_t /*outputNum*/, const solverMode& /*sMode*/) const
{
    return 0;
}
IOdata Source::getOutputs(const IOdata& /*inputs*/,
                          const stateData& /*sD*/,
                          const solverMode& /*sMode*/) const
{
    return {m_tempOut};
}

double Source::getOutput(const IOdata& /*inputs*/,
                         const stateData& /*sD*/,
                         const solverMode& /*sMode*/,
                         index_t outputNum) const
{
    return (outputNum == 0) ? m_tempOut : kNullVal;
}

double Source::getOutput(index_t outputNum) const
{
    return (outputNum == 0) ? m_tempOut : kNullVal;
}

units::unit Source::outputUnits(index_t outputNum) const
{
    return (outputNum == 0) ? outputUnits_ : units::defunit;
}

index_t Source::getOutputLoc(const solverMode& /*sMode*/, index_t /*outputNum*/) const
{
    return kNullLocation;
}
void Source::updateLocalCache(const IOdata& /*inputs*/,
                              const stateData& sD,
                              const solverMode& /*sMode*/)
{
    if ((prevTime != sD.time) && (sD.time > timeZero)) {
        m_tempOut = computeOutput(sD.time);
        lastTime = sD.time;
    }
}

double Source::computeOutput(coreTime /*time*/) const
{
    return m_output;
}
}  // namespace griddyn

