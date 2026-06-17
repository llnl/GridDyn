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
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <string>

namespace griddyn::loads {
using units::unit;

FrequencySensitiveLoad::FrequencySensitiveLoad(const std::string& objName): GridLoad(objName) {}

CoreObject* FrequencySensitiveLoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FrequencySensitiveLoad, GridLoad>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    // nobj->Psched = Psched;

    nobj->dPdf = dPdf;
    nobj->M = M;
    nobj->H = H;

    return nobj;
}

void FrequencySensitiveLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    GridLoad::pFlowObjectInitializeA(time0, flags);
    if (subLoad != nullptr) {
        const auto psched = subLoad->getRealPower();
        dPdf = -H / 30.0 * psched;
    }
}

void FrequencySensitiveLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    GridLoad::dynObjectInitializeA(time0, flags);
}

void FrequencySensitiveLoad::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    if (subLoad != nullptr) {
        subLoad->timestep(time, inputs, sMode);
    }
    const double freq = (inputs.size() > 2) ? inputs[FREQUENCY_IN_LOCATION] : 1.0;

    updateOutputs(freq);
}

void FrequencySensitiveLoad::updateOutputs(double frequency)
{
    if (subLoad == nullptr) {
        Pout = GridLoad::getRealPower();
        Qout = GridLoad::getReactivePower();
        return;
    }

    Pout = subLoad->getRealPower();
    Pout += Pout * (frequency - 1.0) * M;
    Qout = subLoad->getReactivePower();
    Qout += Qout * (frequency - 1.0) * M;
}

static constexpr auto locNumStrings = std::array<std::string_view, 2>{"h", "m"};

static constexpr std::array<std::string_view, 0> locStrStrings{};

static constexpr std::array<std::string_view, 0> flagStrings{};

void FrequencySensitiveLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<FrequencySensitiveLoad, GridLoad>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void FrequencySensitiveLoad::setFlag(std::string_view flag, bool val)
{
    if (subLoad != nullptr) {
        subLoad->setFlag(flag, val);
    } else {
        GridLoad::setFlag(flag, val);
    }
}

// set properties
void FrequencySensitiveLoad::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        if (subLoad != nullptr) {
            subLoad->set(param, val);
        } else {
            GridLoad::set(param, val);
        }
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
    return (subLoad != nullptr) ? subLoad->get(param, unitType) : GridLoad::get(param, unitType);
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
        if (subLoad != nullptr) {
            subLoad->set(param, val, unitType);
        } else {
            GridLoad::set(param, val, unitType);
        }
    }
}

void FrequencySensitiveLoad::updateLocalCache(const IOdata& inputs,
                                              const StateData& stateData,
                                              const SolverMode& sMode)
{
    if (subLoad != nullptr) {
        subLoad->updateLocalCache(inputs, stateData, sMode);
    }
    const double freq = (inputs.size() >= FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    updateOutputs(freq);
}

void FrequencySensitiveLoad::setState(CoreTime time,
                                      const double state[],
                                      const double dstateDt[],
                                      const SolverMode& sMode)
{
    if (subLoad != nullptr) {
        subLoad->setState(time, state, dstateDt, sMode);
    }
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
                                            const StateData& stateData,
                                            const SolverMode& sMode) const
{
    if (subLoad == nullptr) {
        return GridLoad::getRealPower(inputs, stateData, sMode);
    }
    const double realPower = subLoad->getRealPower(inputs, stateData, sMode);
    const double freq = (inputs.size() >= FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    return realPower + (realPower * (freq - 1.0) * M);
}

double FrequencySensitiveLoad::getReactivePower(const IOdata& inputs,
                                                const StateData& stateData,
                                                const SolverMode& sMode) const
{
    if (subLoad == nullptr) {
        return GridLoad::getReactivePower(inputs, stateData, sMode);
    }
    const double reactivePower = subLoad->getReactivePower(inputs, stateData, sMode);
    const double freq = (inputs.size() >= FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    return reactivePower + (reactivePower * (freq - 1.0) * M);
}

double FrequencySensitiveLoad::getRealPower(double voltage) const
{
    if (subLoad == nullptr) {
        return GridLoad::getRealPower(voltage);
    }
    const double realPower = subLoad->getRealPower(voltage);
    const double freq = bus->getFreq();
    return realPower + (realPower * (freq - 1.0) * M);
}

double FrequencySensitiveLoad::getReactivePower(double voltage) const
{
    if (subLoad == nullptr) {
        return GridLoad::getReactivePower(voltage);
    }
    const double reactivePower = subLoad->getReactivePower(voltage);
    const double freq = bus->getFreq();
    return reactivePower + (reactivePower * (freq - 1.0) * M);
}

void FrequencySensitiveLoad::outputPartialDerivatives(const IOdata& inputs,
                                                      const StateData& stateData,
                                                      MatrixData<double>& matrixData,
                                                      const SolverMode& sMode)
{
    if (inputs.empty())  // we only have output derivatives if the input arguments are not counted
    {
        auto argsBus = bus->getOutputs(noInputs, stateData, sMode);
        auto inputLocs = bus->getOutputLocs(sMode);
        ioPartialDerivatives(argsBus, stateData, matrixData, inputLocs, sMode);
    }
}

count_t FrequencySensitiveLoad::outputDependencyCount(index_t num, const SolverMode& sMode) const
{
    return (subLoad != nullptr) ? subLoad->outputDependencyCount(num, sMode) : 0;
}

void FrequencySensitiveLoad::ioPartialDerivatives(const IOdata& /*inputs*/,
                                                  const StateData& /*sD*/,
                                                  MatrixData<double>& /*md*/,
                                                  const IOlocs& /*inputLocs*/,
                                                  const SolverMode& /*sMode*/)
{
}

}  // namespace griddyn::loads
