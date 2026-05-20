/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FrequencySensitiveLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <complex>
#include <iostream>
#include <string>

namespace griddyn::loads {
using units::unit;

FrequencySensitiveLoad::FrequencySensitiveLoad(const std::string& objName): GridLoad(objName) {}

CoreObject* FrequencySensitiveLoad::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<FrequencySensitiveLoad, GridLoad>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    // nobj->Psched = Psched;

    nobj->dPdf = dPdf;
    nobj->M = M;
    nobj->H = H;

    return nobj;
}

void FrequencySensitiveLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridLoad::pFlowObjectInitializeA(time0, flags);
    auto Psched = subLoad->getRealPower();
    dPdf = -H / 30.0 * Psched;
}

void FrequencySensitiveLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridLoad::dynObjectInitializeA(time0, flags);
}

void FrequencySensitiveLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    subLoad->timestep(time, inputs, sMode);
    double freq = (inputs.size() > 2) ? inputs[frequencyInLocation] : 1.0;

    updateOutputs(freq);
}

void FrequencySensitiveLoad::updateOutputs(double frequency)
{
    Pout = subLoad->getRealPower();
    Pout += Pout * (frequency - 1.0) * M;
    Qout = subLoad->getReactivePower();
    Qout += Qout * (frequency - 1.0) * M;
}

static const stringVec locNumStrings{"h", "m"};

static const stringVec locStrStrings{};

static const stringVec flagStrings{};

void FrequencySensitiveLoad::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<FrequencySensitiveLoad, GridLoad>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void FrequencySensitiveLoad::setFlag(std::string_view flag, bool val)
{
    subLoad->setFlag(flag, val);
}

// set properties
void FrequencySensitiveLoad::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        subLoad->set(param, val);
    }
}

double FrequencySensitiveLoad::get(std::string_view param, unit unitType) const
{
    if (param == "dpdf") {
        return dPdf;
    }
    if (param == "h") {
        return H;
    }
    if (param == "m") {
        return M;
    }
    return subLoad->get(param, unitType);
}

void FrequencySensitiveLoad::set(std::string_view param, double val, unit unitType)
{
    if (param == "dpdf") {
        dPdf = val;
    } else if (param == "h") {
        H = val;
    } else if (param == "m") {
        M = val;
    } else {
        subLoad->set(param, val, unitType);
    }
}

void FrequencySensitiveLoad::updateLocalCache(const IOdata& inputs,
                                              const stateData& sD,
                                              const solverMode& sMode)
{
    subLoad->updateLocalCache(inputs, sD, sMode);
    double freq = (inputs.size() >= frequencyInLocation) ? inputs[frequencyInLocation] :
                                                           bus->getFreq(sD, sMode);
    updateOutputs(freq);
}

void FrequencySensitiveLoad::setState(coreTime time,
                                      const double state[],
                                      const double dstate_dt[],
                                      const solverMode& sMode)
{
    subLoad->setState(time, state, dstate_dt, sMode);
    updateOutputs(bus->getFreq());
    prevTime = time;
}

double FrequencySensitiveLoad::getRealPower() const
{
    return Pout;
}

double FrequencySensitiveLoad::getReactivePower() const
{
    return Qout;
}

double FrequencySensitiveLoad::getRealPower(const IOdata& inputs,
                                            const stateData& sD,
                                            const solverMode& sMode) const
{
    double Pr = subLoad->getRealPower(inputs, sD, sMode);
    double freq = (inputs.size() >= frequencyInLocation) ? inputs[frequencyInLocation] :
                                                           bus->getFreq(sD, sMode);
    return Pr + Pr * (freq - 1.0) * M;
}

double FrequencySensitiveLoad::getReactivePower(const IOdata& inputs,
                                                const stateData& sD,
                                                const solverMode& sMode) const
{
    double Qr = subLoad->getReactivePower(inputs, sD, sMode);
    double freq = (inputs.size() >= frequencyInLocation) ? inputs[frequencyInLocation] :
                                                           bus->getFreq(sD, sMode);
    return Qr + Qr * (freq - 1.0) * M;
}

double FrequencySensitiveLoad::getRealPower(double voltage) const
{
    double Pr = subLoad->getRealPower(voltage);
    double freq = bus->getFreq();
    return Pr + Pr * (freq - 1.0) * M;
}

double FrequencySensitiveLoad::getReactivePower(double voltage) const
{
    double Qr = subLoad->getReactivePower(voltage);
    double freq = bus->getFreq();
    return Qr + Qr * (freq - 1.0) * M;
}

void FrequencySensitiveLoad::outputPartialDerivatives(const IOdata& inputs,
                                                      const stateData& sD,
                                                      matrixData<double>& md,
                                                      const solverMode& sMode)
{
    if (inputs.empty())  // we only have output derivatives if the input arguments are not counted
    {
        auto argsBus = bus->getOutputs(noInputs, sD, sMode);
        auto inputLocs = bus->getOutputLocs(sMode);
        ioPartialDerivatives(argsBus, sD, md, inputLocs, sMode);
    }
}

count_t FrequencySensitiveLoad::outputDependencyCount(index_t num, const solverMode& sMode) const
{
    return subLoad->outputDependencyCount(num, sMode);
}

void FrequencySensitiveLoad::ioPartialDerivatives(const IOdata& /*inputs*/,
                                                  const stateData& /*sD*/,
                                                  matrixData<double>& /*md*/,
                                                  const IOlocs& /*inputLocs*/,
                                                  const solverMode& /*sMode*/)
{
}

}  // namespace griddyn::loads
