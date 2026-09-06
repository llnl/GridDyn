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
#include "ExciterESAC1A.h"
#include "ExciterESAC6A.h"
#include "ExciterESST1A.h"
#include "ExciterESST3A.h"
#include "ExciterESST4B.h"
#include "ExciterEXAC1.h"
#include "ExciterEXAC2.h"
#include "ExciterEXAC4.h"
#include "ExciterEXPIC1.h"
#include "ExciterEXST1.h"
#include "ExciterIEEEX1.h"
#include "ExciterIEEEtype1.h"
#include "ExciterIEEEtype2.h"
#include "ExciterSCRX.h"
#include "ExciterSEXS.h"
#include "core/CoreObject.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/coreDefinitions.hpp"
#include "solvers/SolverMode.hpp"
#include "units/units.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
namespace exciters {
    namespace {
        // setup the object factories
        static ChildTypeFactory<ExciterDC1A, Exciter> gfeDc1a("exciter", "dc1a");  // NOLINT
        static ChildTypeFactory<ExciterDC2A, Exciter> gfeDc2a("exciter", "dc2a");  // NOLINT
        static ChildTypeFactory<ExciterDC1A, Exciter> gfeEsdc1a("exciter", "esdc1a");  // NOLINT
        static ChildTypeFactory<ExciterDC2A, Exciter> gfeEsdc2a("exciter", "esdc2a");  // NOLINT
        static ChildTypeFactory<ExciterDC2A, Exciter> gfeExdc2("exciter", "exdc2");  // NOLINT
        static ChildTypeFactory<ExciterESST1A, Exciter> gfeEsst1a("exciter", "esst1a");  // NOLINT
        static ChildTypeFactory<ExciterESST3A, Exciter> gfeEsst3a("exciter", "esst3a");  // NOLINT
        static ChildTypeFactory<ExciterESST4B, Exciter> gfeEsst4b("exciter", "esst4b");  // NOLINT
        static ChildTypeFactory<ExciterEXPIC1, Exciter> gfeExpic1("exciter", "expic1");  // NOLINT
        static ChildTypeFactory<ExciterEXAC1, Exciter> gfeExac1("exciter", "exac1");  // NOLINT
        static ChildTypeFactory<ExciterESAC1A, Exciter> gfeEsac1a("exciter", "esac1a");  // NOLINT
        static ChildTypeFactory<ExciterESAC6A, Exciter> gfeEsac6a("exciter", "esac6a");  // NOLINT
        static ChildTypeFactory<ExciterEXAC2, Exciter> gfeExac2("exciter", "exac2");  // NOLINT
        static ChildTypeFactory<ExciterEXAC4, Exciter> gfeExac4("exciter", "exac4");  // NOLINT
        static ChildTypeFactory<ExciterEXST1, Exciter> gfeExst1("exciter", "exst1");  // NOLINT
        static ChildTypeFactory<ExciterIEEEtype1, Exciter> gfeType1("exciter", "type1");  // NOLINT
        ChildTypeFactory<ExciterIEEEtype1, Exciter> gFeIeeet1("exciter", "ieeet1");  // NOLINT
        ChildTypeFactory<ExciterIEEEX1, Exciter> gFeIeeex1("exciter", "ieeex1");  // NOLINT
        static TypeFactory<Exciter> gfeDefault(  // NOLINT
            "exciter",
            stringVec{"basic", "fast"},
            "type1");  // setup type 1 as the default
        static ChildTypeFactory<ExciterIEEEtype2, Exciter> gfeType2("exciter", "type2");  // NOLINT
        static ChildTypeFactory<ExciterSEXS, Exciter> gfeSexs("exciter", "sexs");  // NOLINT
        static ChildTypeFactory<ExciterSCRX, Exciter> gfeScrx("exciter", "scrx");  // NOLINT
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

void Exciter::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
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
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
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
                       const StateData& stateData,
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
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        residualValues[0] = -exciterStateDerivatives[0];
    } else {
        residualValues[0] =
            (((-exciterState[0]) + (Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION]))) / Ta) -
            exciterStateDerivatives[0];
    }
}

void Exciter::derivative(const IOdata& inputs,
                         const StateData& stateData,
                         double deriv[],
                         const SolverMode& solverMode)
{
    auto locations = offsets.getLocations(stateData, deriv, solverMode, this);
    const auto* exciterState = locations.diffStateLoc;
    auto* derivatives = locations.destDiffLoc;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        derivatives[0] = 0.0;
    } else {
        derivatives[0] =
            ((-exciterState[0]) + (Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION]))) / Ta;
    }
}

void Exciter::jacobianElements(const IOdata& /*inputs*/,
                               const StateData& stateData,
                               MatrixData<double>& matrix,
                               const IOlocs& inputLocs,
                               const SolverMode& solverMode)
{
    if (isAlgebraicOnly(solverMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(solverMode);

    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        matrix.assign(offset, offset, -stateData.cj);
    } else {
        matrix.assign(offset, offset, (-1.0 / Ta) - stateData.cj);
        matrix.assignCheckCol(offset, inputLocs[VOLTAGE_IN_LOCATION], -Ka / Ta);
    }
}

void Exciter::rootTest(const IOdata& inputs,
                       const StateData& stateData,
                       double root[],
                       const SolverMode& solverMode)
{
    auto offset = offsets.getDiffOffset(solverMode);
    const auto rootOffset = offsets.getRootOffset(solverMode);
    const double eField = stateData.state[offset];

    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        root[rootOffset] = Vref + vBias - inputs[VOLTAGE_IN_LOCATION];
    } else {
        root[rootOffset] = std::min(Vrmax - eField, eField - Vrmin) + 0.0001;
        if (eField > Vrmax) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

void Exciter::rootTrigger(CoreTime time,
                          const IOdata& inputs,
                          const std::vector<int>& rootMask,
                          const SolverMode& solverMode)
{
    const auto rootOffset = offsets.getRootOffset(solverMode);
    if (rootMask[rootOffset] != 0) {
        if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
            logging::normal(this, "root trigger back in bounds");
            alert(this, JAC_COUNT_INCREASE);
            opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
            opFlags.reset(TRIGGER_HIGH);
        } else {
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            if (opFlags[TRIGGER_HIGH]) {
                logging::normal(this, "root trigger above bounds");
                m_state[limitState] -= 0.0001;
            } else {
                logging::normal(this, "root trigger below bounds");
                m_state[limitState] += 0.0001;
            }
            alert(this, JAC_COUNT_DECREASE);
        }
        const StateData stateData(time, m_state.data());

        derivative(inputs, stateData, m_dstate_dt.data(), cLocalSolverMode);
    }
}

ChangeCode Exciter::rootCheck(const IOdata& inputs,
                              const StateData& /*StateData*/,
                              const SolverMode& /*solverMode*/,
                              CheckLevel /*level*/)
{
    const double eField = m_state[0];
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double test = Vref + vBias - inputs[VOLTAGE_IN_LOCATION];
        if (opFlags[TRIGGER_HIGH]) {
            if (test < 0) {
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                opFlags.reset(TRIGGER_HIGH);
                alert(this, JAC_COUNT_INCREASE);
                ret = ChangeCode::JACOBIAN_CHANGE;
            }
        } else {
            if (test > 0) {
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                alert(this, JAC_COUNT_INCREASE);
                ret = ChangeCode::JACOBIAN_CHANGE;
            }
        }
    } else {
        if (eField > Vrmax + 0.0001) {
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            m_state[0] = Vrmax;
            alert(this, JAC_COUNT_DECREASE);
            ret = ChangeCode::JACOBIAN_CHANGE;
        } else if (eField < Vrmin - 0.0001) {
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
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
        {"id", "directaxiscurrent"},
        {"iq", "quadratureaxiscurrent"},
        {"vd", "directaxisvoltage"},
        {"vq", "quadratureaxisvoltage"},
        {"te", "electricaltorque"},
        {"xadifd", "fieldcurrent"},
        {"vss", "stabilizersignal"},
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
