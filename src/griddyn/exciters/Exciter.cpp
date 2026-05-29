/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Exciter.h"

#include "../Generator.h"
#include "../GridComponentHelperClasses.h"
#include "../GridPrimary.h"
#include "../gridDynDefinitions.hpp"
#include "ExciterDC1A.h"
#include "ExciterDC2A.h"
#include "ExciterIEEEtype1.h"
#include "ExciterIEEEtype2.h"
#include "ExciterSEXS.h"
#include "core/CoreObject.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/coreDefinitions.hpp"
#include "solvers/SolverMode.hpp"
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
        static ChildTypeFactory<ExciterDC1A, Exciter> gfeDc1a("exciter", "dc1a");  // NOLINT
        static ChildTypeFactory<ExciterDC2A, Exciter> gfeDc2a("exciter", "dc2a");  // NOLINT
        static ChildTypeFactory<ExciterIEEEtype1, Exciter> gfeType1("exciter", "type1");  // NOLINT
        static TypeFactory<Exciter> gfeDefault(  // NOLINT
            "exciter",
            stringVec{"basic", "fast"},
            "type1");  // setup type 1 as the default
        static ChildTypeFactory<ExciterIEEEtype2, Exciter> gfeType2("exciter", "type2");  // NOLINT
        static ChildTypeFactory<ExciterSEXS, Exciter> gfeSexs("exciter", "sexs");  // NOLINT
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
                       const SolverMode& solverMode)
{
    if (isAlgebraicOnly(solverMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(solverMode);
    const auto* exciterState = stateData.state + offset;
    const auto* exciterStateDerivatives = stateData.dstate_dt + offset;
    auto* residualValues = resid + offset;
    if (opFlags[outsideVoltageLimits]) {
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
                         const SolverMode& solverMode)
{
    auto locations = offsets.getLocations(stateData, deriv, solverMode, this);
    const auto* exciterState = locations.diffStateLoc;
    auto* derivatives = locations.destDiffLoc;
    if (opFlags[outsideVoltageLimits]) {
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
                               const SolverMode& solverMode)
{
    if (isAlgebraicOnly(solverMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(solverMode);

    if (opFlags[outsideVoltageLimits]) {
        matrix.assign(offset, offset, -stateData.cj);
    } else {
        matrix.assign(offset, offset, (-1.0 / Ta) - stateData.cj);
        matrix.assignCheckCol(offset, inputLocs[voltageInLocation], -Ka / Ta);
    }
}

void Exciter::rootTest(const IOdata& inputs,
                       const stateData& stateData,
                       double root[],
                       const SolverMode& solverMode)
{
    auto offset = offsets.getDiffOffset(solverMode);
    const auto rootOffset = offsets.getRootOffset(solverMode);
    const double eField = stateData.state[offset];

    if (opFlags[outsideVoltageLimits]) {
        root[rootOffset] = Vref + vBias - inputs[voltageInLocation];
    } else {
        root[rootOffset] = std::min(Vrmax - eField, eField - Vrmin) + 0.0001;
        if (eField > Vrmax) {
            opFlags.set(triggerHigh);
        }
    }
}

void Exciter::rootTrigger(coreTime time,
                          const IOdata& inputs,
                          const std::vector<int>& rootMask,
                          const SolverMode& solverMode)
{
    const auto rootOffset = offsets.getRootOffset(solverMode);
    if (rootMask[rootOffset] != 0) {
        if (opFlags[outsideVoltageLimits]) {
            logging::normal(this, "root trigger back in bounds");
            alert(this, JAC_COUNT_INCREASE);
            opFlags.reset(outsideVoltageLimits);
            opFlags.reset(triggerHigh);
        } else {
            opFlags.set(outsideVoltageLimits);
            if (opFlags[triggerHigh]) {
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

ChangeCode Exciter::rootCheck(const IOdata& inputs,
                              const stateData& /*stateData*/,
                              const SolverMode& /*solverMode*/,
                              CheckLevel /*level*/)
{
    const double eField = m_state[0];
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[outsideVoltageLimits]) {
        const double test = Vref + vBias - inputs[voltageInLocation];
        if (opFlags[triggerHigh]) {
            if (test < 0) {
                opFlags.reset(outsideVoltageLimits);
                opFlags.reset(triggerHigh);
                alert(this, JAC_COUNT_INCREASE);
                ret = ChangeCode::JACOBIAN_CHANGE;
            }
        } else {
            if (test > 0) {
                opFlags.reset(outsideVoltageLimits);
                alert(this, JAC_COUNT_INCREASE);
                ret = ChangeCode::JACOBIAN_CHANGE;
            }
        }
    } else {
        if (eField > Vrmax + 0.0001) {
            opFlags.set(triggerHigh);
            opFlags.set(outsideVoltageLimits);
            m_state[0] = Vrmax;
            alert(this, JAC_COUNT_DECREASE);
            ret = ChangeCode::JACOBIAN_CHANGE;
        } else if (eField < Vrmin - 0.0001) {
            opFlags.set(outsideVoltageLimits);
            m_state[0] = Vrmin;
            alert(this, JAC_COUNT_DECREASE);
            ret = ChangeCode::JACOBIAN_CHANGE;
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
    };
    return inputNamesStr;
}

const std::vector<stringVec>& Exciter::outputNames() const
{
    static const std::vector<stringVec> outputNamesStr{
        {"e", "field", "exciter"},
    };
    return outputNamesStr;
}

}  // namespace griddyn
