/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiMELoad.h"

#include "../fmi_import/fmiObjects.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridBus.h"
#include <complex>
#include <string>

namespace griddyn::fmi {
FmiMELoad::FmiMELoad(const std::string& objName): FmiMEWrapper<Load>(objName) {}

CoreObject* FmiMELoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FmiMELoad, FmiMEWrapper<Load>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void FmiMELoad::updateLocalCache(const IOdata& inputs,
                                 const stateData& stateDataRef,
                                 const solverMode& sMode)
{
    auto inputVector = inputs;
    const auto voltageComplex = std::polar(inputs[voltageInLocation], inputs[angleInLocation]);
    if (opFlags[COMPLEX_VOLTAGE]) {
        inputVector[0] = voltageComplex.real();
        inputVector[1] = voltageComplex.imag();
    }
    inputVector[1] *= 180.0 / kPI;
    fmisub->updateLocalCache(inputVector, stateDataRef, sMode);
    auto res = fmisub->getOutputs(inputVector, stateDataRef, sMode);
    // printf("V[%f,%f,%f,%f,%f,%f], I[%f,%f,%f,%f,%f,%f]\n", V[0], V[1], V[2], V[3], V[4],
    // V[5], I[0], I[1], I[2], I[3], I[4], I[5]);

    auto translatedOutput = translateOutput(res, inputs);
    setP(translatedOutput[PoutLocation]);
    setQ(translatedOutput[QoutLocation]);
}

void FmiMELoad::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<Load>::set(param, val);
    }
}
void FmiMELoad::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<Load>::set(param, val, unitType);
    }
}

void FmiMELoad::setState(coreTime time,
                         const double state[],
                         const double dstate_dt[],
                         const solverMode& sMode)
{
    FmiMEWrapper<Load>::setState(time, state, dstate_dt, sMode);
    auto out = fmisub->getOutputs(noInputs, emptyStateData, cLocalSolverMode);

    const IOdata voltageState = {bus->getVoltage(state, sMode), bus->getAngle(state, sMode)};
    auto translatedOutput = translateOutput(out, voltageState);
    setP(translatedOutput[PoutLocation]);
    setQ(translatedOutput[QoutLocation]);
}

IOdata FmiMELoad::translateOutput(const IOdata& fmiOutput, const IOdata& busV)
{
    auto busVoltage = std::complex<double>(busV[voltageInLocation], busV[angleInLocation]);
    IOdata powers(2);
    if (opFlags[CURRENT_OUTPUT]) {
        if (opFlags[COMPLEX_OUTPUT]) {
            auto currentValue = std::complex<double>(fmiOutput[0], fmiOutput[1]);
            auto actualPower = busVoltage * std::conj(currentValue);
            powers[PoutLocation] = actualPower.real();
            powers[QoutLocation] = actualPower.imag();
        } else {
            auto currentValue = std::polar(fmiOutput[0], fmiOutput[1] * kPI / 180.0);
            auto actualPower = busVoltage * std::conj(currentValue);
            powers[PoutLocation] = actualPower.real();
            powers[QoutLocation] = actualPower.imag();
        }
    } else {
        if (opFlags[COMPLEX_OUTPUT]) {
            powers[PoutLocation] = fmiOutput[PoutLocation];
            powers[QoutLocation] = fmiOutput[QoutLocation];
        } else {
            auto power = std::polar(fmiOutput[0], fmiOutput[1]);
            powers[PoutLocation] = power.real();
            powers[QoutLocation] = power.imag();
        }
    }
    return powers;
}
}  // namespace griddyn::fmi

