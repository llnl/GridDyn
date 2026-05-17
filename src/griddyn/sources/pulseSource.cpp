/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pulseSource.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cmath>
#include <string>

namespace griddyn::sources {
using units::unit;

pulseSource::pulseSource(const std::string& objName, double startVal):
    Source(objName, startVal), baseValue(startVal)
{
}

CoreObject* pulseSource::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<pulseSource, Source>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->ptype = ptype;
    nobj->dutyCycle = dutyCycle;
    nobj->Amplitude = Amplitude;
    nobj->cycleTime = cycleTime;
    nobj->baseValue = baseValue;
    nobj->shift = shift;
    nobj->period = period;
    return nobj;
}

void pulseSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t /*flags*/)
{
    cycleTime =
        time0 - shift * period - period;  // subtract a period so it cycles properly the first time
    updateOutput(time0);
}

void pulseSource::updateOutput(coreTime time)
{
    if ((time == prevTime) || (period == maxTime)) {
        return;
    }

    coreTime tdiff = time - cycleTime;
    if (tdiff > period) {
        cycleTime += period;
        tdiff -= period;
        if (tdiff > period) {
            cycleTime += period * (std::floor(tdiff / period));
            tdiff = tdiff % period;
        }
    }

    const double pcalc = pulseCalc(static_cast<double>(tdiff));

    m_output = baseValue + pcalc;
    // printf("at %f setting output to %f\n", static_cast<double>(time), m_output);
    prevTime = time;
}

double pulseSource::computeOutput(coreTime time) const
{
    if ((time == prevTime) || (period == maxTime)) {
        return m_output;
    }
    auto tdiff = (time - cycleTime) % period;

    const double pcalc = pulseCalc(static_cast<double>(tdiff));
    // printf("%d, output =%f at %f, td=%f\n", getUserID(),baseValue + pcalc,
    // static_cast<double>(time), static_cast<double>(tdiff));
    return baseValue + pcalc;
}

double pulseSource::getDoutdt(const IOdata& /*inputs*/,
                              const stateData& stateData,
                              const solverMode& /*sMode*/,
                              index_t /*num*/) const
{
    double output1;
    double output2;
    if (!stateData.empty()) {
        output1 = computeOutput(stateData.time - 0.0001);
        output2 = computeOutput(stateData.time);
    } else {
        output1 = computeOutput(lastTime - 0.0001);
        output2 = m_tempOut;
    }
    return ((output2 - output1) / 0.0001);
}

void pulseSource::set(std::string_view param, std::string_view val)
{
    if ((param == "type") || (param == "pulsetype")) {
        auto vtype = gmlc::utilities::convertToLowerCase(val);
        if (vtype == "square") {
            ptype = PulseType::SQUARE;
        } else if (vtype == "triangle") {
            ptype = PulseType::TRIANGLE;
        } else if (vtype == "gaussian") {
            ptype = PulseType::GAUSSIAN;
        } else if (vtype == "exponential") {
            ptype = PulseType::EXPONENTIAL;
        } else if (vtype == "biexponential") {
            ptype = PulseType::BIEXPONENTIAL;
        } else if (vtype == "monocycle") {
            ptype = PulseType::MONOCYCLE;
        } else if ((vtype == "sine") || (vtype == "cosine")) {
            ptype = PulseType::COSINE;
        }

        cycleTime -= period;
    } else {
        Source::set(param, val);
    }
}

void pulseSource::setLevel(double val)
{
    baseValue = val;
    m_output = m_tempOut = val;
    cycleTime = cycleTime - period;
}

void pulseSource::set(std::string_view param, double val, unit unitType)
{
    if ((param == "a") || (param == "amplitude") || (param == "amp")) {
        Amplitude = val;
        // done to ensure a new value is computed at the next update
        cycleTime -= period;
    } else if (param == "period") {
        period = val;
    } else if (param == "frequency") {
        period = 1.0 / val;
    } else if (param == "dutycycle") {
        dutyCycle = val;
        // done to ensure a new value is computed at the next update
        cycleTime -= period;
    } else if (param == "shift") {
        cycleTime = cycleTime + (shift - val) * period;
        shift = val;
    } else if (param == "base") {
        baseValue = val;
        cycleTime -= period;
    } else if (param == "invert") {
        if (val > 0) {
            opFlags.set(invert_flag);
        } else {
            opFlags.reset(invert_flag);
        }
        cycleTime -= period;
    } else {
        Source::set(param, val, unitType);
    }
}

double pulseSource::pulseCalc(double timeDelta) const
{
    double mult = 1.0;
    const double cycleLocation = timeDelta / period;
    const double prop = (cycleLocation - (dutyCycle / 2)) / dutyCycle;
    if ((prop < 0) || (prop >= 1.0)) {
        return (opFlags[invert_flag]) ? Amplitude : 0.0;
    }

    // calculate the multiplier
    if (prop < 0.05) {
        mult = 20.0 * prop;
    } else if (prop > 0.95) {
        mult = 20.0 * (1 - prop);
    }

    double pamp = 0.0;
    switch (ptype) {
        case PulseType::SQUARE:
            pamp = Amplitude;
            break;
        case PulseType::TRIANGLE:
            pamp = 2.0 * Amplitude * ((prop < 0.5) ? prop : (1.0 - prop));
            break;
        case PulseType::GAUSSIAN:
            pamp = mult * Amplitude * exp((prop - 0.5) * (prop - 0.5) * 25.0);
            break;
        case PulseType::MONOCYCLE:
            pamp = mult * Amplitude * 11.6583 * (prop - 0.5) * exp(-(prop - 0.5) * (prop - 0.5));
            break;
        case PulseType::BIEXPONENTIAL:
            if (prop < 0.5) {
                pamp = mult * Amplitude * exp(-(0.5 - prop) * 12.0);
            } else {
                pamp = mult * Amplitude * exp(-(prop - 0.5) * 12.0);
            }
            break;
        case PulseType::EXPONENTIAL:
            if (prop < 0.5) {
                mult = 1.0;
            }
            pamp = mult * Amplitude * exp(-prop * 6.0);
            break;
        case PulseType::COSINE:

            pamp = Amplitude * sin(prop * kPI);
            break;
        case PulseType::FLATTOP:
            if (prop < 0.25) {
                pamp = Amplitude / 2.0 * (-cos(kPI * prop * 4.0) + 1.0);
            } else if (prop > 0.75) {
                pamp = Amplitude / 2.0 * cos((kPI * ((1.0 - prop) * 4)) + 1.0);
            } else {
                pamp = Amplitude;
            }
            break;
        default:
            break;
    }
    if (opFlags[invert_flag]) {
        pamp = Amplitude - pamp;
    }
    return pamp;
}
}  // namespace griddyn::sources
