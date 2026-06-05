/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiMELoad.h"

#include "../fmi_import/fmiObjects.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/GridBus.h"
#include <complex>
#include <string>

namespace griddyn::fmi {
FmiMELoad::FmiMELoad(const std::string& objName): FmiMEWrapper<GridLoad>(objName) {}

CoreObject* FmiMELoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FmiMELoad, FmiMEWrapper<GridLoad>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void FmiMELoad::updateLocalCache(const IOdata& inputs,
                                 const StateData& stateDataRef,
                                 const SolverMode& sMode)
{
    auto inputVector = inputs;
    const auto voltageComplex = std::polar(inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION]);
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
    setP(translatedOutput[POUT_LOCATION]);
    setQ(translatedOutput[QOUT_LOCATION]);
}

void FmiMELoad::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<GridLoad>::set(param, val);
    }
}
void FmiMELoad::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<GridLoad>::set(param, val, unitType);
    }
}

void FmiMELoad::setState(CoreTime time,
                         const double state[],
                         const double dstateDt[],
                         const SolverMode& sMode)
{
    FmiMEWrapper<GridLoad>::setState(time, state, dstateDt, sMode);
    auto out = fmisub->getOutputs(noInputs, emptyStateData, cLocalSolverMode);

    const IOdata voltageState = {bus->getVoltage(state, sMode), bus->getAngle(state, sMode)};
    auto translatedOutput = translateOutput(out, voltageState);
    setP(translatedOutput[POUT_LOCATION]);
    setQ(translatedOutput[QOUT_LOCATION]);
}

IOdata FmiMELoad::translateOutput(const IOdata& fmiOutput, const IOdata& busV)
{
    auto busVoltage = std::complex<double>(busV[VOLTAGE_IN_LOCATION], busV[ANGLE_IN_LOCATION]);
    IOdata powers(2);
    if (opFlags[CURRENT_OUTPUT]) {
        if (opFlags[COMPLEX_OUTPUT]) {
            auto currentValue = std::complex<double>(fmiOutput[0], fmiOutput[1]);
            auto actualPower = busVoltage * std::conj(currentValue);
            powers[POUT_LOCATION] = actualPower.real();
            powers[QOUT_LOCATION] = actualPower.imag();
        } else {
            auto currentValue = std::polar(fmiOutput[0], fmiOutput[1] * kPI / 180.0);
            auto actualPower = busVoltage * std::conj(currentValue);
            powers[POUT_LOCATION] = actualPower.real();
            powers[QOUT_LOCATION] = actualPower.imag();
        }
    } else {
        if (opFlags[COMPLEX_OUTPUT]) {
            powers[POUT_LOCATION] = fmiOutput[POUT_LOCATION];
            powers[QOUT_LOCATION] = fmiOutput[QOUT_LOCATION];
        } else {
            auto power = std::polar(fmiOutput[0], fmiOutput[1]);
            powers[POUT_LOCATION] = power.real();
            powers[QOUT_LOCATION] = power.imag();
        }
    }
    return powers;
}
}  // namespace griddyn::fmi
