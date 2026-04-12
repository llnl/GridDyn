/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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

namespace griddyn {
namespace fmi {
    fmiMELoad3phase::fmiMELoad3phase(const std::string& objName):
        fmiMEWrapper<loads::ThreePhaseLoad>(objName)
    {
        loads::ThreePhaseLoad::setFlag("three_phase_input");
        loads::ThreePhaseLoad::setFlag("three_phase_output");
    }

    coreObject* fmiMELoad3phase::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<fmiMELoad3phase, fmiMEWrapper<loads::ThreePhaseLoad>>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        return nobj;
    }

    void fmiMELoad3phase::setFlag(const std::string& flag, bool val)
    {
        if (flag == "current_output") {
            opFlags[current_output] = val;
        } else if ((flag == "complex_output") || (flag == "complex_current_output")) {
            opFlags[complex_current_output] = val;
            if (val) {
                opFlags[current_output] = true;
            }
        } else if (flag == "complex_voltage") {
            opFlags[complex_voltage] = val;
        } else if ((flag == "ignore_voltage_angle") || (flag == "ignore_angle")) {
            opFlags[ignore_voltage_angle] = val;
        } else {
            fmiMEWrapper<loads::ThreePhaseLoad>::setFlag(flag, val);
        }
    }

    void fmiMELoad3phase::set(const std::string& param, const std::string& val)
    {
        if (param.empty()) {
        } else {
            fmiMEWrapper<loads::ThreePhaseLoad>::set(param, val);
        }
    }
    void fmiMELoad3phase::set(const std::string& param, double val, units::unit unitType)
    {
        if (param.empty()) {
        } else {
            fmiMEWrapper<loads::ThreePhaseLoad>::set(param, val, unitType);
        }
    }

    void fmiMELoad3phase::setState(coreTime time,
                                   const double state[],
                                   const double dstate_dt[],
                                   const solverMode& sMode)
    {
        fmisub->setState(time, state, dstate_dt, sMode);
        auto out = fmisub->getOutputs(noInputs, emptyStateData, cLocalSolverMode);
        setP(out[PoutLocation]);
        setQ(out[QoutLocation]);
    }

    void fmiMELoad3phase::updateLocalCache(const IOdata& inputs,
                                           const stateData& sD,
                                           const solverMode& sMode)
    {
        auto V = opFlags[complex_voltage] ? generate3PhaseVector(inputs) :
                                            generate3PhasePolarVector(inputs);
        V[1] *= 180.0 / k_PI;
        V[3] *= 180.0 / k_PI;
        V[5] *= 180.0 / k_PI;
        fmisub->updateLocalCache(V, sD, sMode);
        auto I = fmisub->getOutputs(V, sD, sMode);
        // printf("V[%f,%f,%f,%f,%f,%f], I[%f,%f,%f,%f,%f,%f]\n", V[0], V[1], V[2], V[3], V[4],
        // V[5], I[0], I[1], I[2], I[3], I[4], I[5]);
        auto PQ = ThreePhasePowerPolar(V, I);  // TODO(phlpt): Make this conditional.
        setPa(PQ[0]);
        setPb(PQ[2]);
        setPc(PQ[4]);
        setQa(PQ[1]);
        setQb(PQ[3]);
        setQc(PQ[5]);
    }
    static const std::vector<stringVec> inputNamesStr3phaseVoltageOnly{
        {"voltage_a", "v_a", "volt_a", "vmag_a", "voltage", "v"},
        {"voltage_b", "v_b", "volt_b", "vmag_b"},
        {"voltage_c", "v_c", "volt_c", "vmag_c"},
    };

    static const std::vector<stringVec> inputNamesStr3phaseComplexVoltage{
        {"v_real_a", "voltage_real_a"},
        {"v_imag_a", "voltage_imag_a"},
        {"v_real_b", "voltage_real_b"},
        {"v_imag_b", "voltage_imag_b"},
        {"v_real_c", "voltage_real_d"},
        {"v_imag_c", "voltage_imag_c"},
    };

    /*
ignore_voltage_angle = object_flag8,
complex_voltage = object_flag9,
current_output = object_flag10,
complex_output = object_flag11,
*/

    const std::vector<stringVec>& fmiMELoad3phase::fmiInputNames() const
    {
        if (opFlags[ignore_voltage_angle]) {
            return inputNamesStr3phaseVoltageOnly;
        }
        if (opFlags[complex_voltage]) {
            return inputNamesStr3phaseComplexVoltage;
        }
        return inputNames();
    }

    static const std::vector<stringVec> outputNamesStrCurrentOutput{
        {"i_a", "current_a", "imag_a"},
        {"i_angle_a", "current_angle_a"},
        {"i_b", "current_b", "imag_b"},
        {"i_angle_b", "current_angle_b"},
        {"i_c", "current_c", "imag_c"},
        {"i_angle_c", "current_angle_c"},
    };

    static const std::vector<stringVec> outputNamesStrComplexCurrentOutput{
        {"i_a", "current_a", "i_real_a", "current_real_a"},
        {"i_imag_a", "current_imag_a"},
        {"i_b", "current_b", "i_real_b", "current_real_b"},
        {"i_imag_b", "current_imag_b"},
        {"i_c", "current_c", "i_real_c", "current_real_c"},
        {"i_imag_c", "current_imag_c"},
    };

    const std::vector<stringVec>& fmiMELoad3phase::fmiOutputNames() const
    {
        if (opFlags[current_output]) {
            return (opFlags[complex_current_output]) ? outputNamesStrComplexCurrentOutput :
                                                       outputNamesStrCurrentOutput;
        }

        return outputNames();
    }

}  // namespace fmi
}  // namespace griddyn
