/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Exciter.h"
#include "../Generator.h"
#include "../gridPrimary.h"
#include "ExciterDC1A.h"
#include "ExciterDC2A.h"
#include "ExciterIEEEtype1.h"
#include "ExciterIEEEtype2.h"
#include "ExciterSEXS.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
#include <vector>

// note that there is only 1 dynamic state since V_R = E_f

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

Exciter::Exciter(const std::string& objName): gridSubModel(objName)
{
    m_inputSize = 4;
    m_outputSize = 1;
}
// cloning function
coreObject* Exciter::clone(coreObject* obj) const
{
    auto* gdE = cloneBase<Exciter, gridSubModel>(this, obj);
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
// initial conditions
void Exciter::dynObjectInitializeB(const IOdata& inputs,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    double* gs = m_state.data();
    double V = inputs[voltageInLocation];
    if (desiredOutput.empty() || (desiredOutput[0] == kNullVal)) {
        gs[0] = (Vref + vBias - V) / Ka;
        fieldSet[0] = gs[0];
    } else {
        gs[0] = desiredOutput[0];

        vBias = V - Vref + gs[0] / Ka;
        fieldSet[exciterVsetInLocation] = Vref;
    }
}

// residual
void Exciter::residual(const IOdata& inputs,
                       const stateData& sD,
                       double resid[],
                       const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);
    const double* es = sD.state + offset;
    const double* esp = sD.dstate_dt + offset;
    double* rv = resid + offset;
    if (opFlags[outside_vlim]) {
        rv[0] = -esp[0];
    } else {
        rv[0] = (-es[0] + Ka * (Vref + vBias - inputs[voltageInLocation])) / Ta - esp[0];
    }
}

void Exciter::derivative(const IOdata& inputs,
                         const stateData& sD,
                         double deriv[],
                         const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    const double* es = Loc.diffStateLoc;
    double* d = Loc.destDiffLoc;
    if (opFlags[outside_vlim]) {
        d[0] = 0.0;
    } else {
        d[0] = (-es[0] + Ka * (Vref + vBias - inputs[voltageInLocation])) / Ta;
    }
}

// Jacobian
void Exciter::jacobianElements(const IOdata& /*inputs*/,
                               const stateData& sD,
                               matrixData<double>& md,
                               const IOlocs& inputLocs,
                               const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);

    // Ef (Vr)
    if (opFlags[outside_vlim]) {
        md.assign(offset, offset, -sD.cj);
    } else {
        md.assign(offset, offset, -1.0 / Ta - sD.cj);
        md.assignCheckCol(offset, inputLocs[voltageInLocation], -Ka / Ta);
    }

    // printf("%f\n",sD.cj);
}

void Exciter::rootTest(const IOdata& inputs,
                       const stateData& sD,
                       double root[],
                       const solverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    int rootOffset = offsets.getRootOffset(sMode);
    double Efield = sD.state[offset];

    if (opFlags[outside_vlim]) {
        root[rootOffset] = Vref + vBias - inputs[voltageInLocation];
    } else {
        root[rootOffset] = std::min(Vrmax - Efield, Efield - Vrmin) + 0.0001;
        if (Efield > Vrmax) {
            opFlags.set(etrigger_high);
        }
    }
}

void Exciter::rootTrigger(coreTime time,
                          const IOdata& inputs,
                          const std::vector<int>& rootMask,
                          const solverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    if (rootMask[rootOffset] != 0) {
        if (opFlags[outside_vlim]) {
            LOG_NORMAL("root trigger back in bounds");
            alert(this, JAC_COUNT_INCREASE);
            opFlags.reset(outside_vlim);
            opFlags.reset(etrigger_high);
        } else {
            opFlags.set(outside_vlim);
            if (opFlags[etrigger_high]) {
                LOG_NORMAL("root trigger above bounds");
                m_state[limitState] -= 0.0001;
            } else {
                LOG_NORMAL("root trigger below bounds");
                m_state[limitState] += 0.0001;
            }
            alert(this, JAC_COUNT_DECREASE);
        }
        stateData sD(time, m_state.data());

        derivative(inputs, sD, m_dstate_dt.data(), cLocalSolverMode);
    }
}

change_code Exciter::rootCheck(const IOdata& inputs,
                               const stateData& /*sD*/,
                               const solverMode& /*sMode*/,
                               check_level_t /*level*/)
{
    double Efield = m_state[0];
    change_code ret = change_code::no_change;
    if (opFlags[outside_vlim]) {
        double test = Vref + vBias - inputs[voltageInLocation];
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
        if (Efield > Vrmax + 0.0001) {
            opFlags.set(etrigger_high);
            opFlags.set(outside_vlim);
            m_state[0] = Vrmax;
            alert(this, JAC_COUNT_DECREASE);
            ret = change_code::jacobian_change;
        } else if (Efield < Vrmin - 0.0001) {
            opFlags.set(outside_vlim);
            m_state[0] = Vrmin;
            alert(this, JAC_COUNT_DECREASE);
            ret = change_code::jacobian_change;
        }
    }
    return ret;
}

static const stringVec exciterFields{"ef"};

stringVec Exciter::localStateNames() const
{
    return exciterFields;
}
void Exciter::set(const std::string& param, const std::string& val)
{
    coreObject::set(param, val);
}
// set parameters
void Exciter::set(const std::string& param, double val, units::unit unitType)
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
        gridSubModel::set(param, val, unitType);
    }
}

static const std::vector<stringVec> inputNamesStr{
    {"voltage", "v", "volt"},
    {"vset", "setpoint", "voltageset"},
    {"pmech", "power", "mechanicalpower"},
    {"omega", "frequency", "w", "f"},
};

const std::vector<stringVec>& Exciter::inputNames() const
{
    return inputNamesStr;
}

static const std::vector<stringVec> outputNamesStr{
    {"e", "field", "exciter"},
};

const std::vector<stringVec>& Exciter::outputNames() const
{
    return outputNamesStr;
}

}  // namespace griddyn
