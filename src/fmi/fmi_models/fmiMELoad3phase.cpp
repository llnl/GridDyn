/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiMELoad3phase.h"

#include "../fmi_import/fmiObjects.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridBus.h"
#include "utilities/ThreePhaseFunctions.h"
#include <string>
#include <vector>

namespace griddyn::fmi {
FmiMELoad3phase::FmiMELoad3phase(const std::string& objName):
    FmiMEWrapper<loads::ThreePhaseLoad>(objName)
{
    loads::ThreePhaseLoad::setFlag("three_phase_input");
    loads::ThreePhaseLoad::setFlag("three_phase_output");
}

CoreObject* FmiMELoad3phase::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FmiMELoad3phase, FmiMEWrapper<loads::ThreePhaseLoad>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    return nobj;
}

void FmiMELoad3phase::setFlag(std::string_view flag, bool val)
{
    if (flag == "current_output") {
        opFlags[CURRENT_OUTPUT] = val;
    } else if ((flag == "complex_output") || (flag == "complex_current_output")) {
        opFlags[COMPLEX_CURRENT_OUTPUT] = val;
        if (val) {
            opFlags[CURRENT_OUTPUT] = true;
        }
    } else if (flag == "complex_voltage") {
        opFlags[COMPLEX_VOLTAGE] = val;
    } else if ((flag == "ignore_voltage_angle") || (flag == "ignore_angle")) {
        opFlags[IGNORE_VOLTAGE_ANGLE] = val;
    } else {
        FmiMEWrapper<loads::ThreePhaseLoad>::setFlag(flag, val);
    }
}

void FmiMELoad3phase::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<loads::ThreePhaseLoad>::set(param, val);
    }
}
void FmiMELoad3phase::set(std::string_view param, double val, units::unit unitType)
{
    if (param.empty()) {
    } else {
        FmiMEWrapper<loads::ThreePhaseLoad>::set(param, val, unitType);
    }
}

void FmiMELoad3phase::setState(coreTime time,
                               const double state[],
                               const double dstate_dt[],
                               const solverMode& sMode)
{
    fmisub->setState(time, state, dstate_dt, sMode);
    auto out = fmisub->getOutputs(noInputs, emptyStateData, cLocalSolverMode);
    setP(out[PoutLocation]);
    setQ(out[QoutLocation]);
}

void FmiMELoad3phase::updateLocalCache(const IOdata& inputs,
                                       const stateData& stateDataRef,
                                       const solverMode& sMode)
{
    auto inputVector =
        opFlags[COMPLEX_VOLTAGE] ? generate3PhaseVector(inputs) : generate3PhasePolarVector(inputs);
    inputVector[1] *= 180.0 / k_PI;
    inputVector[3] *= 180.0 / k_PI;
    inputVector[5] *= 180.0 / k_PI;
    fmisub->updateLocalCache(inputVector, stateDataRef, sMode);
    auto outputCurrent = fmisub->getOutputs(inputVector, stateDataRef, sMode);
    // printf("V[%f,%f,%f,%f,%f,%f], I[%f,%f,%f,%f,%f,%f]\n", V[0], V[1], V[2], V[3], V[4],
    // V[5], I[0], I[1], I[2], I[3], I[4], I[5]);
    auto powerValues =
        ThreePhasePowerPolar(inputVector, outputCurrent);  // TODO(phlpt): Make this conditional.
    setPa(powerValues[0]);
    setPb(powerValues[2]);
    setPc(powerValues[4]);
    setQa(powerValues[1]);
    setQb(powerValues[3]);
    setQc(powerValues[5]);
}
namespace {
    const std::vector<stringVec>& inputNamesStr3phaseVoltageOnly()
    {
        static const auto* names = new std::vector<stringVec>{
            {"voltage_a", "v_a", "volt_a", "vmag_a", "voltage", "v"},
            {"voltage_b", "v_b", "volt_b", "vmag_b"},
            {"voltage_c", "v_c", "volt_c", "vmag_c"},
        };
        return *names;
    }

    const std::vector<stringVec>& inputNamesStr3phaseComplexVoltage()
    {
        static const auto* names = new std::vector<stringVec>{
            {"v_real_a", "voltage_real_a"},
            {"v_imag_a", "voltage_imag_a"},
            {"v_real_b", "voltage_real_b"},
            {"v_imag_b", "voltage_imag_b"},
            {"v_real_c", "voltage_real_d"},
            {"v_imag_c", "voltage_imag_c"},
        };
        return *names;
    }
}  // namespace

/*
ignore_voltage_angle = object_flag8,
complex_voltage = object_flag9,
current_output = object_flag10,
complex_output = object_flag11,
*/

const std::vector<stringVec>& FmiMELoad3phase::getFmiInputNames() const
{
    if (opFlags[IGNORE_VOLTAGE_ANGLE]) {
        return inputNamesStr3phaseVoltageOnly();
    }
    if (opFlags[COMPLEX_VOLTAGE]) {
        return inputNamesStr3phaseComplexVoltage();
    }
    return inputNames();
}

namespace {
    const std::vector<stringVec>& outputNamesStrCurrentOutput()
    {
        static const auto* names = new std::vector<stringVec>{
            {"i_a", "current_a", "imag_a"},
            {"i_angle_a", "current_angle_a"},
            {"i_b", "current_b", "imag_b"},
            {"i_angle_b", "current_angle_b"},
            {"i_c", "current_c", "imag_c"},
            {"i_angle_c", "current_angle_c"},
        };
        return *names;
    }

    const std::vector<stringVec>& outputNamesStrComplexCurrentOutput()
    {
        static const auto* names = new std::vector<stringVec>{
            {"i_a", "current_a", "i_real_a", "current_real_a"},
            {"i_imag_a", "current_imag_a"},
            {"i_b", "current_b", "i_real_b", "current_real_b"},
            {"i_imag_b", "current_imag_b"},
            {"i_c", "current_c", "i_real_c", "current_real_c"},
            {"i_imag_c", "current_imag_c"},
        };
        return *names;
    }
}  // namespace

const std::vector<stringVec>& FmiMELoad3phase::getFmiOutputNames() const
{
    if (opFlags[CURRENT_OUTPUT]) {
        return (opFlags[COMPLEX_CURRENT_OUTPUT]) ? outputNamesStrComplexCurrentOutput() :
                                                   outputNamesStrCurrentOutput();
    }

    return outputNames();
}

}  // namespace griddyn::fmi

