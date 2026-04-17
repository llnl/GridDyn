/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorReheat.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactory.hpp"
#include "utilities/matrixData.hpp"
#include <string>

namespace griddyn::governors {
using units::unit;

GovernorReheat::GovernorReheat(const std::string& objName): Governor(objName)
{
    // default values
    K = 16.667;
    T1 = 0.1;
    T2 = 0.45;
    T3 = 0.05;
    T4 = 12.0;
    T5 = 50.0;
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 12;
}

coreObject* GovernorReheat::clone(coreObject* obj) const
{
    auto* gov = cloneBase<GovernorReheat, Governor>(this, obj);
    if (gov == nullptr) {
        return obj;
    }

    gov->T3 = T3;
    gov->T4 = T4;
    gov->T5 = T5;

    return gov;
}

// destructor
GovernorReheat::~GovernorReheat() = default;

// initial conditions
void GovernorReheat::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (Wref < 0) {
        Wref = systemBaseFrequency;
    }

    double P = desiredOutput[0];
    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];
    if (P > Pmax) {
        P = Pmax;
    } else if (P < Pmin) {
        P = Pmin;
    }
    m_state[3] = P;
    m_state[2] = (1.0 - T3 / T2) * P;
    m_state[1] = (1.0 - T4 / T5) * (m_state[2] + T3 / T2 * m_state[3]);
    m_state[0] = P;

    fieldSet.resize(2);
    fieldSet[1] = P - K * (Wref - omega);  // Pset
}

// residual
void GovernorReheat::residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode);
    const double* gsp = sD.dstate_dt + offset;
    if (isAlgebraicOnly(sMode)) {
        gsp = sD.dstate_dt + offsets.getAlgOffset(solverMode(4));
        resid[offset + 0] = gsp[1] + T4 / T5 * (gsp[2] + T3 / T2 * gsp[3]) - sD.state[offset];
        return;
    }
    const double* gs = sD.state + offset;

    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];

    double Tin = inputs[govpSetInLocation] + K * (Wref - omega);
    if (Tin > Pmax) {
        Tin = Pmax;
    } else if (Tin < Pmin) {
        Tin = Pmin;
    }
    resid[offset + 3] = (Tin - gs[3]) / T1 - gsp[3];
    resid[offset + 2] = ((1 - T3 / T2) * gs[3] - gs[2]) / T2 - gsp[2];
    resid[offset + 1] = ((1.0 - T4 / T5) * (gs[2] + T3 / T2 * gs[3]) - gs[1]) / T5 - gsp[1];
    resid[offset + 0] = gs[1] + T4 / T5 * (gs[2] + T3 / T2 * gs[3]) - gs[0];
}

void GovernorReheat::derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode);
    const double* gs = sD.state + offset;
    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];
    double Tin = inputs[govpSetInLocation] + K * (Wref - omega);
    if (Tin > Pmax) {
        Tin = Pmax;
    } else if (Tin < Pmin) {
        Tin = Pmin;
    }
    deriv[offset + 3] = (Tin - gs[3]) / T1;
    deriv[offset + 2] = ((1 - T3 / T2) * gs[3] - gs[2]) / T2;
    deriv[offset + 1] = ((1.0 - T4 / T5) * (gs[2] + T3 / T2 * gs[3]) - gs[1]) / T5;
}

void GovernorReheat::jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode)
{
    auto offset = offsets.getAlgOffset(sMode);
    if (isAlgebraicOnly(sMode)) {
        md.assign(offset, offset, -1);
        return;
    }

    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];
    bool limitTin = false;
    double Tin = inputs[govpSetInLocation] + K * (Wref - omega);
    if (Tin > Pmax) {
        Tin = Pmax;
        limitTin = true;
    } else if (Tin < Pmin) {
        Tin = Pmin;
        limitTin = true;
    }
    int refI = offset;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)
    bool linkOmega = (inputLocs[govOmegaInLocation] != kNullLocation);

    /* if (opFlags.test (uses_deadband))
   {
     if (!opFlags.test (outside_deadband))
       {
         linkOmega = false;
       }
   }
       */
    if (limitTin) {
        md.assign(refI + 3, refI + 3, -1.0 / T1 - sD.cj);
    } else {
        if (linkOmega) {
            md.assign(refI + 3, inputLocs[govOmegaInLocation], K / T1);
        }
        if (inputLocs[govpSetInLocation] != kNullLocation) {
            md.assign(refI + 3, inputLocs[govpSetInLocation], 1.0 / T1);
        }
        md.assign(refI + 3, refI + 3, -1.0 / T1 - sD.cj);
    }
    // tg2

    md.assign(refI + 2, refI + 3, (1.0 - T3 / T2) * 1.0 / T2);
    md.assign(refI + 2, refI + 2, -1.0 / T2 - sD.cj);
    // Tg3
    md.assign(refI + 1, refI + 2, (1.0 - T4 / T5) / T5);
    md.assign(refI + 1, refI + 3, ((1.0 - T4 / T5) * T3 / T2) / T5);
    md.assign(refI + 1, refI + 1, -1.0 / T5 - sD.cj);
    // Tout
    md.assign(refI, refI, -1);
    md.assign(refI, refI + 1, 1);
    md.assign(refI, refI + 2, T4 / T5);
    md.assign(refI, refI + 3, T4 / T5 * (T3 / T2));
}

index_t GovernorReheat::findIndex(const std::string& field, const solverMode& /*sMode*/) const
{
    index_t ret = kInvalidLocation;
    if (field == "pm") {
        ret = 0;
    } else if (field == "tg1") {
        ret = 3;
    } else if (field == "tg2") {
        ret = 2;
    } else if (field == "tg3") {
        ret = 1;
    }
    return ret;
}

// set parameters
void GovernorReheat::set(const std::string& param, const std::string& val)
{
    Governor::set(param, val);
}

void GovernorReheat::set(const std::string& param, double val, unit unitType)
{
    // param   = gridDynSimulation::toLower(param);
    if ((param == "ts") || (param == "t1")) {
        T1 = val;
    } else if ((param == "tc") || (param == "t2")) {
        T2 = val;
    } else if (param == "t3") {
        T3 = val;
    } else if (param == "t4") {
        T4 = val;
    } else if (param == "t5") {
        T5 = val;
    } else {
        Governor::set(param, val, unitType);
    }
}
}  // namespace griddyn::governors
