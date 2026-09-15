/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ShuntTD.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::loads {
ShuntTD::ShuntTD(const std::string& objName): ZipLoad(objName) {}

ShuntTD::ShuntTD(double realPower, double reactivePower, const std::string& objName):
    ZipLoad(realPower, reactivePower, objName)
{
}

CoreObject* ShuntTD::clone(CoreObject* obj) const
{
    return cloneBase<ShuntTD, ZipLoad>(this, obj);
}

double ShuntTD::phaseVoltage(const IOdata& inputs,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             double phaseOffset) const
{
    const double voltage = (inputs.size() > VOLTAGE_IN_LOCATION) ?
        inputs[VOLTAGE_IN_LOCATION] :
        bus->getVoltage(stateData, sMode);
    const double angle = (inputs.size() > ANGLE_IN_LOCATION) ? inputs[ANGLE_IN_LOCATION] :
                                                               bus->getAngle(stateData, sMode);
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    const double time = stateData.empty() ? currentTime() : stateData.time;

    // GridDyn stores the system base frequency in rad/s.  ANDES uses Hz in
    // 2*pi*f*t, so multiplying by the per-unit frequency gives the same angle.
    const double electricalAngle = (systemBaseFrequency * frequency * time) + angle + phaseOffset;
    return voltage / std::sqrt(3.0) * std::cos(electricalAngle);
}

double ShuntTD::getOutput(const IOdata& inputs,
                          const StateData& stateData,
                          const SolverMode& sMode,
                          index_t outputNum) const
{
    if (outputNum < VTA_OUTPUT) {
        return GridSecondary::getOutput(inputs, stateData, sMode, outputNum);
    }
    switch (outputNum) {
        case VTA_OUTPUT:
            return phaseVoltage(inputs, stateData, sMode, 0.0);
        case VTB_OUTPUT:
            return phaseVoltage(inputs, stateData, sMode, -2.0 * std::acos(-1.0) / 3.0);
        case VTC_OUTPUT:
            return phaseVoltage(inputs, stateData, sMode, 2.0 * std::acos(-1.0) / 3.0);
        default:
            return kNullVal;
    }
}

double ShuntTD::getOutput(index_t outputNum) const
{
    return getOutput(noInputs, emptyStateData, cLocalSolverMode, outputNum);
}

IOdata ShuntTD::getOutputs(const IOdata& inputs,
                           const StateData& stateData,
                           const SolverMode& sMode) const
{
    IOdata outputs(5);
    for (index_t ii = 0; ii < static_cast<index_t>(outputs.size()); ++ii) {
        outputs[ii] = getOutput(inputs, stateData, sMode, ii);
    }
    return outputs;
}

const std::vector<stringVec>& ShuntTD::outputNames() const
{
    static const std::vector<stringVec> names{
        {"real", "P", "p", "output"},
        {"reactive", "Q", "q", "output1"},
        {"vta", "va", "phase_a_voltage", "output2"},
        {"vtb", "vb", "phase_b_voltage", "output3"},
        {"vtc", "vc", "phase_c_voltage", "output4"},
    };
    return names;
}

units::unit ShuntTD::outputUnits(index_t outputNum) const
{
    if (outputNum >= VTA_OUTPUT && outputNum <= VTC_OUTPUT) {
        return units::puV;
    }
    return GridSecondary::outputUnits(outputNum);
}
}  // namespace griddyn::loads
