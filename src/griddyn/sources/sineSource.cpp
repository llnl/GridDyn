/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sineSource.h"

#include "core/coreObjectTemplates.hpp"
#include <cmath>
#include <string>

namespace griddyn::sources {
sineSource::sineSource(const std::string& objName, double startVal): pulseSource(objName, startVal)
{
}
CoreObject* sineSource::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<sineSource, pulseSource>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->Amp = Amp;
    nobj->frequency = frequency;
    nobj->phase = phase;
    nobj->lastCycle = lastCycle;
    nobj->sinePeriod = sinePeriod;
    nobj->dfdt = dfdt;
    nobj->dAdt = dAdt;
    return nobj;
}

void sineSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (frequency <= 0.0) {
        lastCycle = negTime;
    } else {
        lastCycle = time0 - phase / (frequency * 2.0 * kPI);
    }
    pulseSource::pFlowObjectInitializeA(time0, flags);
    updateOutput(time0);
}

double sineSource::computeOutput(coreTime time) const
{
    auto timeDelta = time - prevTime;
    if (timeDelta == timeZero) {
        return m_output;
    }
    // account for the frequency shift
    const double nextFrequency = frequency + dfdt * timeDelta;
    const double nextAmplitude = Amp + dAdt * timeDelta;
    // compute the sine wave component
    auto tdiff = time - lastCycle;
    const double addComponent = nextAmplitude * sin((2.0 * kPI * (nextFrequency * tdiff)) + phase);
    double mult = 1.0;

    if (opFlags[pulsed_flag]) {
        auto tdiff2 = time - cycleTime;
        if (tdiff2 > period) {
            tdiff2 = tdiff2 % period;
        }
        mult = pulseCalc(tdiff2);
    }

    return baseValue + (mult * addComponent);
}

void sineSource::updateOutput(coreTime time)
{
    auto timeDelta = time - prevTime;
    if (timeDelta == timeZero) {
        return;
    }
    auto tdiff = time - lastCycle;
    // account for the frequency shift
    frequency += dfdt * (time - prevTime);
    Amp += dAdt * (time - prevTime);
    // compute the sine wave component
    const double addComponent = Amp * sin((2.0 * kPI * (frequency * tdiff)) + phase);
    double mult = 1.0;
    while (tdiff > sinePeriod) {
        tdiff -= sinePeriod;
        lastCycle += sinePeriod;
    }
    if (opFlags[pulsed_flag]) {
        auto tdiff2 = time - cycleTime;
        while (tdiff2 > period) {
            cycleTime += period;
            tdiff2 -= period;
        }
        mult = pulseCalc(tdiff2);
    }

    m_output = baseValue + (mult * addComponent);
    m_tempOut = m_output;
    prevTime = time;
}

void sineSource::set(std::string_view param, std::string_view val)
{
    pulseSource::set(param, val);
}
void sineSource::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "a") || (param == "amplitude") || (param == "amp")) {
        Amp = val;
    } else if ((param == "frequency") || (param == "freq") || (param == "f")) {
        frequency = val;
        sinePeriod = 1.0 / frequency;
    } else if ((param == "period") || (param == "sineperiod")) {
        sinePeriod = val;
        frequency = 1.0 / val;
    } else if (param == "pulseperiod") {
        pulseSource::set("period", val, unitType);
    } else if (param == "pulseamplitude") {
        pulseSource::set("amplitude", val, unitType);
    } else if (param == "phase") {
        phase = val;
    } else if (param == "dfdt") {
        dfdt = val;
    } else if (param == "dadt") {
        dAdt = val;
    } else if (param == "pulsed") {
        if (val > 0.0) {
            if (!(opFlags[pulsed_flag])) {
                cycleTime = prevTime;
            }
            opFlags.set(pulsed_flag);
        } else {
            opFlags.reset(pulsed_flag);
        }
    } else {
        pulseSource::set(param, val, unitType);
    }
}
}  // namespace griddyn::sources

