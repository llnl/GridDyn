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
/*
enum pulse_type_t{ square = 0, triangle = 1, gaussian = 2, biexponential = 3, exponential = 4 };
pulse_type_t ptype;
protected:
double period;
double duty_cylce;
double A;
double nextCycleTime;*/
using units::unit;

pulseSource::pulseSource(const std::string& objName, double startVal):
    Source(objName, startVal), baseValue(startVal)
{
}

coreObject* pulseSource::clone(coreObject* obj) const
{
    auto nobj = cloneBase<pulseSource, Source>(this, obj);
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

    double pcalc = pulseCalc(static_cast<double>(tdiff));

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
                              const stateData& sD,
                              const solverMode& /*sMode*/,
                              index_t /*num*/) const
{
    double o1, o2;
    if (!sD.empty()) {
        o1 = computeOutput(sD.time - 0.0001);
        o2 = computeOutput(sD.time);
    } else {
        o1 = computeOutput(lastTime - 0.0001);
        o2 = m_tempOut;
    }
    return ((o2 - o1) / 0.0001);
}

void pulseSource::set(const std::string& param, const std::string& val)
{
    if ((param == "type") || (param == "pulsetype")) {
        auto vtype = gmlc::utilities::convertToLowerCase(val);
        if (vtype == "square") {
            ptype = pulse_type_t::square;
        } else if (vtype == "triangle") {
            ptype = pulse_type_t::triangle;
        } else if (vtype == "gaussian") {
            ptype = pulse_type_t::gaussian;
        } else if (vtype == "exponential") {
            ptype = pulse_type_t::exponential;
        } else if (vtype == "biexponential") {
            ptype = pulse_type_t::biexponential;
        } else if (vtype == "monocycle") {
            ptype = pulse_type_t::monocycle;
        } else if ((vtype == "sine") || (vtype == "cosine")) {
            ptype = pulse_type_t::cosine;
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

void pulseSource::set(const std::string& param, double val, unit unitType)
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

double pulseSource::pulseCalc(double td) const
{
    double mult = 1.0;
    double cloc = td / period;
    double prop = (cloc - dutyCycle / 2) / dutyCycle;
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
        case pulse_type_t::square:
            pamp = Amplitude;
            break;
        case pulse_type_t::triangle:
            pamp = 2.0 * Amplitude * ((prop < 0.5) ? prop : (1.0 - prop));
            break;
        case pulse_type_t::gaussian:
            pamp = mult * Amplitude * exp((prop - 0.5) * (prop - 0.5) * 25.0);
            break;
        case pulse_type_t::monocycle:
            pamp = mult * Amplitude * 11.6583 * (prop - 0.5) * exp(-(prop - 0.5) * (prop - 0.5));
            break;
        case pulse_type_t::biexponential:
            if (prop < 0.5) {
                pamp = mult * Amplitude * exp(-(0.5 - prop) * 12.0);
            } else {
                pamp = mult * Amplitude * exp(-(prop - 0.5) * 12.0);
            }
            break;
        case pulse_type_t::exponential:
            if (prop < 0.5) {
                mult = 1.0;
            }
            pamp = mult * Amplitude * exp(-prop * 6.0);
            break;
        case pulse_type_t::cosine:

            pamp = Amplitude * sin(prop * kPI);
            break;
        case pulse_type_t::flattop:
            if (prop < 0.25) {
                pamp = Amplitude / 2.0 * (-cos(kPI * prop * 4.0) + 1.0);
            } else if (prop > 0.75) {
                pamp = Amplitude / 2.0 * cos(kPI * ((1.0 - prop) * 4) + 1.0);
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
