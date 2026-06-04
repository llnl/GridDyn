/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterIEEEtype1.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::exciters {
ExciterIEEEtype1::ExciterIEEEtype1(const std::string& objName): Exciter(objName)
{
    // default values that are different from inherited default values
    Ka = 20;
    Ta = 0.04;

    limitState = 1;
}

// cloning function
CoreObject* ExciterIEEEtype1::clone(CoreObject* obj) const
{
    auto* gdE = cloneBase<ExciterIEEEtype1, Exciter>(this, obj);
    if (gdE == nullptr) {
        return obj;
    }
    gdE->Ke = Ke;
    gdE->Te = Te;
    gdE->Kf = Kf;
    gdE->Tf = Tf;
    gdE->Aex = Aex;
    gdE->Bex = Bex;
    return gdE;
}

void ExciterIEEEtype1::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 14;
    checkForLimits();
}

// initial conditions
void ExciterIEEEtype1::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    Exciter::dynObjectInitializeB(inputs,
                                  desiredOutput,
                                  fieldSet);  // this will dynInitializeB the field state if need be
    double* gs = m_state.data();
    gs[1] = (Ke + Aex * exp(Bex * gs[0])) * gs[0];  // Vr
    gs[2] = gs[0] * Kf / Tf;  // Rf
    vBias = inputs[VOLTAGE_IN_LOCATION] + gs[1] / Ka - Vref;
    fieldSet[1] = Vref;
    m_dstate_dt[0] = 0.0;
    m_dstate_dt[1] = 0.0;
    m_dstate_dt[2] = 0.0;
}

// residual
void ExciterIEEEtype1::residual(const IOdata& inputs,
                                const StateData& sD,
                                double resid[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);
    const double* es = sD.state + offset;
    const double* esp = sD.dstate_dt + offset;
    double* rv = resid + offset;
    rv[0] = (-(Ke + Aex * exp(Bex * es[0])) * es[0] + es[1]) / Te - esp[0];
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        if (opFlags[TRIGGER_HIGH]) {
            rv[1] = esp[1];
        } else {
            rv[1] = esp[1];
        }
    } else {
        rv[1] = (-es[1] + Ka * es[2] - es[0] * Ka * Kf / Tf +
                 Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION])) /
                Ta -
            esp[1];
    }
    rv[2] = (-es[2] + es[0] * Kf / Tf) / Tf - esp[2];
}

void ExciterIEEEtype1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    double dt = time - prevTime;  // convert from a CoreTime
    m_state[0] += dt * m_dstate_dt[0];
    m_state[1] += dt * m_dstate_dt[1];
    m_state[2] += dt * m_dstate_dt[2];
    prevTime = time;
}

void ExciterIEEEtype1::derivative(const IOdata& inputs,
                                  const StateData& sD,
                                  double deriv[],
                                  const SolverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    const double* es = Loc.diffStateLoc;
    auto d = Loc.destDiffLoc;
    d[0] = (-(Ke + Aex * exp(Bex * es[0])) * es[0] + es[1]) / Te;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        d[1] = 0;
    } else {
        d[1] = (-es[1] + Ka * es[2] - es[0] * Ka * Kf / Tf +
                Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION])) /
            Ta;
    }
    d[2] = (-es[2] + es[0] * Kf / Tf) / Tf;
}

// Jacobian
void ExciterIEEEtype1::jacobianElements(const IOdata& /*inputs*/,
                                        const StateData& sD,
                                        MatrixData<double>& md,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto offset = offsets.getDiffOffset(sMode);

    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)

    // Ef
    double temp1 =
        -(Ke + Aex * exp(Bex * sD.state[offset]) * (1.0 + Bex * sD.state[offset])) / Te - sD.cj;
    md.assign(offset, offset, temp1);
    md.assign(offset, offset + 1, 1 / Te);
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        md.assign(offset + 1, offset + 1, sD.cj);
    } else {
        // Vr

        md.assignCheckCol(offset + 1, inputLocs[VOLTAGE_IN_LOCATION], -Ka / Ta);
        md.assign(offset + 1, offset, -Ka * Kf / (Tf * Ta));
        md.assign(offset + 1, offset + 1, -1.0 / Ta - sD.cj);
        md.assign(offset + 1, offset + 2, Ka / Ta);
    }

    // Rf
    md.assign(offset + 2, offset, Kf / (Tf * Tf));
    md.assign(offset + 2, offset + 2, -1.0 / Tf - sD.cj);

    // printf("%f\n",sD.cj);
}

void ExciterIEEEtype1::rootTest(const IOdata& inputs,
                                const StateData& sD,
                                double roots[],
                                const SolverMode& sMode)
{
    auto offset = offsets.getDiffOffset(sMode);
    auto rootOffset = offsets.getRootOffset(sMode);
    const double* es = sD.state + offset;

    // printf("t=%f V=%f\n", time, inputs[VOLTAGE_IN_LOCATION]);

    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        roots[rootOffset] = es[2] - es[0] * Kf / Tf + (Vref + vBias - inputs[VOLTAGE_IN_LOCATION]) -
            es[1] / Ka + 0.001 * es[1] / Ka / Ta;
    } else {
        roots[rootOffset] = std::min(Vrmax - es[1], es[1] - Vrmin) + 0.00001;
        if (es[1] >= Vrmax) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

ChangeCode ExciterIEEEtype1::rootCheck(const IOdata& inputs,
                                       const StateData& /*sD*/,
                                       const SolverMode& /*sMode*/,
                                       CheckLevel /*level*/)
{
    const double* es = m_state.data();
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        double test =
            es[2] - es[0] * Kf / Tf + (Vref + vBias - inputs[VOLTAGE_IN_LOCATION]) - es[1] / Ka;

        if (opFlags[TRIGGER_HIGH]) {
            if (test < -0.001 * es[1] / Ka / Ta) {
                ret = ChangeCode::JACOBIAN_CHANGE;

                logging::debug(this, "root change V={}", inputs[VOLTAGE_IN_LOCATION]);
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                opFlags.reset(TRIGGER_HIGH);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > -0.001 * es[1] / Ka / Ta) {
                logging::debug(this, "root change V={}", inputs[VOLTAGE_IN_LOCATION]);
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (es[1] > Vrmax + 0.00001) {
            logging::debug(this, "root toggle V={}", inputs[VOLTAGE_IN_LOCATION]);
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            m_state[1] = Vrmax;
            m_dstate_dt[1] = 0.0;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (es[1] < Vrmin - 0.00001) {
            logging::debug(this, "root toggle V={}", inputs[VOLTAGE_IN_LOCATION]);

            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            m_state[1] = Vrmin;
            m_dstate_dt[1] = 0.0;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

static const stringVec ieeeType1Fields{"ef", "vr", "rf"};

stringVec ExciterIEEEtype1::localStateNames() const
{
    return ieeeType1Fields;
}
void ExciterIEEEtype1::set(std::string_view param, std::string_view val)
{
    return Exciter::set(param, val);
}

// set parameters
void ExciterIEEEtype1::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "ke") {
        Ke = val;
    } else if (param == "te") {
        Te = val;
    } else if (param == "kf") {
        Kf = val;
    } else if (param == "tf") {
        Tf = val;
    } else if (param == "aex") {
        Aex = val;
    } else if (param == "bex") {
        Bex = val;
    } else if (param == "limiter") {
        if (val > 0.1) {
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        } else if (val < -0.1) {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        } else {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
        }
    } else {
        Exciter::set(param, val, unitType);
    }
}

}  // namespace griddyn::exciters
