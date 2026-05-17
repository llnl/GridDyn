/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Exciter.h"

#include "../Generator.h"
#include "../gridComponentHelperClasses.h"
#include "../gridDynDefinitions.hpp"
#include "../gridPrimary.h"
#include "ExciterDC1A.h"
#include "ExciterDC2A.h"
#include "ExciterIEEEtype1.h"
#include "ExciterIEEEtype2.h"
#include "ExciterSEXS.h"
#include "core/coreDefinitions.hpp"
#include "core/coreObject.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "solvers/solverMode.hpp"
#include "units/units.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace griddyn {
namespace exciters {
    namespace {
        // setup the object factories
        static childTypeFactory<ExciterDC1A, Exciter> gfeDc1a("exciter", "dc1a");  // NOLINT
        static childTypeFactory<ExciterDC2A, Exciter> gfeDc2a("exciter", "dc2a");  // NOLINT
        static childTypeFactory<ExciterIEEEtype1, Exciter> gfeType1("exciter", "type1");  // NOLINT
        static typeFactory<Exciter> gfeDefault(  // NOLINT
            "exciter",
            stringVec{"basic", "fast"},
            "type1");  // setup type 1 as the default
        static childTypeFactory<ExciterIEEEtype2, Exciter> gfeType2("exciter", "type2");  // NOLINT
        static childTypeFactory<ExciterSEXS, Exciter> gfeSexs("exciter", "sexs");  // NOLINT
    }  // namespace
}  // namespace exciters

Exciter::Exciter(const std::string& objName): GridSubModel(objName)
{
    m_inputSize = 4;
    m_outputSize = 1;
}

CoreObject* Exciter::clone(CoreObject* obj) const
{
    auto* gdE = cloneBase<Exciter, GridSubModel>(this, obj);
    if (gdE == nullptr) {
        return obj;
    }

    gdE->Ka = Ka;
    gdE->Ta = Ta;
    gdE->Vrmin = Vrmin;
    gdE->Vrmax = Vrmax;
    gdE->Vref = Vref;
    gdE->vBias = vBias;
    gdE->limitState = limitState;
    return gdE;
}

void Exciter::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 1;
    offsets.local().local.jacSize = 4;
    checkForLimits();
}

void Exciter::checkForLimits()
{
    if ((Vrmin > -21) || (Vrmax < 21)) {
        offsets.local().local.algRoots = 1;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Exciter::dynObjectInitializeB(const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    auto* stateValues = m_state.data();
    const double voltage = inputs[voltageInLocation];
    if (desiredOutput.empty() || (desiredOutput[0] == kNullVal)) {
        stateValues[0] = (Vref + vBias - voltage) / Ka;
        fieldSet[0] = stateValues[0];
    } else {
        stateValues[0] = desiredOutput[0];

        vBias = voltage - Vref + (stateValues[0] / Ka);
        fieldSet[exciterVsetInLocation] = Vref;
    }
}

void Exciter::residual(const IOdata& inputs,
                       const stateData& stateData,
                       double resid[],
                       const solverMode& solverMode)
{
    if (isAlgebraicOnly(solverMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(solverMode);
    const auto* exciterState = stateData.state + offset;
    const auto* exciterStateDerivatives = stateData.dstate_dt + offset;
    auto* residualValues = resid + offset;
    if (opFlags[outside_vlim]) {
        residualValues[0] = -exciterStateDerivatives[0];
    } else {
        residualValues[0] =
            (((-exciterState[0]) + (Ka * (Vref + vBias - inputs[voltageInLocation]))) / Ta) -
            exciterStateDerivatives[0];
    }
}

void Exciter::derivative(const IOdata& inputs,
                         const stateData& stateData,
                         double deriv[],
                         const solverMode& solverMode)
{
    auto locations = offsets.getLocations(stateData, deriv, solverMode, this);
    const auto* exciterState = locations.diffStateLoc;
    auto* derivatives = locations.destDiffLoc;
    if (opFlags[outside_vlim]) {
        derivatives[0] = 0.0;
    } else {
        derivatives[0] =
            ((-exciterState[0]) + (Ka * (Vref + vBias - inputs[voltageInLocation]))) / Ta;
    }
}

void Exciter::jacobianElements(const IOdata& /*inputs*/,
                               const stateData& stateData,
                               matrixData<double>& matrix,
                               const IOlocs& inputLocs,
                               const solverMode& solverMode)
{
    if (isAlgebraicOnly(solverMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(solverMode);

    if (opFlags[outside_vlim]) {
        matrix.assign(offset, offset, -stateData.cj);
    } else {
        matrix.assign(offset, offset, (-1.0 / Ta) - stateData.cj);
        matrix.assignCheckCol(offset, inputLocs[voltageInLocation], -Ka / Ta);
    }
}

void Exciter::rootTest(const IOdata& inputs,
                       const stateData& stateData,
                       double root[],
                       const solverMode& solverMode)
{
    auto offset = offsets.getDiffOffset(solverMode);
    const auto rootOffset = offsets.getRootOffset(solverMode);
    const double eField = stateData.state[offset];

    if (opFlags[outside_vlim]) {
        root[rootOffset] = Vref + vBias - inputs[voltageInLocation];
    } else {
        root[rootOffset] = std::min(Vrmax - eField, eField - Vrmin) + 0.0001;
        if (eField > Vrmax) {
            opFlags.set(etrigger_high);
        }
    }
}

void Exciter::rootTrigger(coreTime time,
                          const IOdata& inputs,
                          const std::vector<int>& rootMask,
                          const solverMode& solverMode)
{
    const auto rootOffset = offsets.getRootOffset(solverMode);
    if (rootMask[rootOffset] != 0) {
        if (opFlags[outside_vlim]) {
            logging::normal(this, "root trigger back in bounds");
            alert(this, JAC_COUNT_INCREASE);
            opFlags.reset(outside_vlim);
            opFlags.reset(etrigger_high);
        } else {
            opFlags.set(outside_vlim);
            if (opFlags[etrigger_high]) {
                logging::normal(this, "root trigger above bounds");
                m_state[limitState] -= 0.0001;
            } else {
                logging::normal(this, "root trigger below bounds");
                m_state[limitState] += 0.0001;
            }
            alert(this, JAC_COUNT_DECREASE);
        }
        const stateData stateData(time, m_state.data());

        derivative(inputs, stateData, m_dstate_dt.data(), cLocalSolverMode);
    }
}

change_code Exciter::rootCheck(const IOdata& inputs,
                               const stateData& /*stateData*/,
                               const solverMode& /*solverMode*/,
                               check_level_t /*level*/)
{
    const double eField = m_state[0];
    change_code ret = change_code::no_change;
    if (opFlags[outside_vlim]) {
        const double test = Vref + vBias - inputs[voltageInLocation];
        if (opFlags[etrigger_high]) {
            if (test < 0) {
                opFlags.reset(outside_vlim);
                opFlags.reset(etrigger_high);
                alert(this, JAC_COUNT_INCREASE);
                ret = change_code::jacobian_change;
            }
        } else {
            if (test > 0) {
                opFlags.reset(outside_vlim);
                alert(this, JAC_COUNT_INCREASE);
                ret = change_code::jacobian_change;
            }
        }
    } else {
        if (eField > Vrmax + 0.0001) {
            opFlags.set(etrigger_high);
            opFlags.set(outside_vlim);
            m_state[0] = Vrmax;
            alert(this, JAC_COUNT_DECREASE);
            ret = change_code::jacobian_change;
        } else if (eField < Vrmin - 0.0001) {
            opFlags.set(outside_vlim);
            m_state[0] = Vrmin;
            alert(this, JAC_COUNT_DECREASE);
            ret = change_code::jacobian_change;
        }
    }
    return ret;
}

stringVec Exciter::localStateNames() const
{
    return {"ef"};
}

void Exciter::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}

void Exciter::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "vref") {
        Vref = val;
    } else if (param == "ka") {
        Ka = val;
    } else if (param == "ta") {
        Ta = val;
    } else if ((param == "vrmax") || (param == "urmax")) {
        Vrmax = val;
    } else if ((param == "vrmin") || (param == "urmin")) {
        Vrmin = val;
    } else if (param == "vbias") {
        vBias = val;
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

const std::vector<stringVec>& Exciter::inputNames() const
{
    static const std::vector<stringVec> inputNamesStr{
        {"voltage", "v", "volt"},
        {"vset", "setpoint", "voltageset"},
        {"pmech", "power", "mechanicalpower"},
        {"omega", "frequency", "w", "f"},
    };  // NOLINT(bugprone-throwing-static-initialization)
    return inputNamesStr;
}

const std::vector<stringVec>& Exciter::outputNames() const
{
    static const std::vector<stringVec> outputNamesStr{
        {"e", "field", "exciter"},
    };  // NOLINT(bugprone-throwing-static-initialization)
    return outputNamesStr;
}

}  // namespace griddyn

