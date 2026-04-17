/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorTgov1.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace griddyn::governors {
using units::unit;

GovernorTgov1::GovernorTgov1(const std::string& objName): GovernorIeeeSimple(objName)
{
    // default values
    K = 16.667;
    // K = 0.5;
    T1 = 0.5;
    T2 = 1.0;
    T3 = 1.0;
    offsets.local().local.diffSize = 2;
    offsets.local().local.algSize = 1;
    offsets.local().local.jacSize = 10;
    opFlags.set(ignore_deadband);
    opFlags.set(ignore_filter);
    opFlags.set(ignore_throttle);
}

coreObject* GovernorTgov1::clone(coreObject* obj) const
{
    auto* gov = cloneBase<GovernorTgov1, GovernorIeeeSimple>(this, obj);
    if (gov == nullptr) {
        return obj;
    }
    gov->Dt = Dt;
    return gov;
}

// destructor
GovernorTgov1::~GovernorTgov1() = default;

// initial conditions
void GovernorTgov1::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    m_state[2] = desiredOutput[PoutLocation];
    m_state[1] = desiredOutput[PoutLocation];
    m_state[0] = desiredOutput[PoutLocation];
    fieldSet[govpSetInLocation] = desiredOutput[PoutLocation];
}

// residual
void GovernorTgov1::residual(const IOdata& inputs,
                             const stateData& sD,
                             double resid[],
                             const solverMode& sMode)
{
    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];
    auto Loc = offsets.getLocations(sD, resid, sMode, this);
    Loc.destLoc[0] = Loc.algStateLoc[0] - Loc.diffStateLoc[0] + Dt * (omega - 1.0);

    if (isAlgebraicOnly(sMode)) {
        return;
    }

    derivative(inputs, sD, resid, sMode);

    Loc.destDiffLoc[0] -= Loc.dstateLoc[0];
    Loc.destDiffLoc[1] -= Loc.dstateLoc[1];
}

void GovernorTgov1::derivative(const IOdata& inputs,
                               const stateData& sD,
                               double deriv[],
                               const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);

    const double* gs = Loc.diffStateLoc;
    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];

    if (opFlags[p_limited]) {
        Loc.destDiffLoc[1] = 0.0;
    } else {
        Loc.destDiffLoc[1] = (-gs[1] + inputs[govpSetInLocation] - K * (omega - 1.0)) / T1;
        //  LOG_WARNING(std::string("gov set =") + std::to_string(K * (omega - 1.0)));
    }

    Loc.destDiffLoc[0] = (Loc.diffStateLoc[1] - Loc.diffStateLoc[0] - T2 * Loc.destDiffLoc[1]) / T3;
}

void GovernorTgov1::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    GovernorTgov1::derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    double dt = time - prevTime;
    m_state[1] += dt * m_dstate_dt[1];
    m_state[2] += dt * m_dstate_dt[2];
    // double omega = getControlFrequency (inputs);
    double omega = inputs[govOmegaInLocation];
    m_state[0] = m_state[1] - Dt * (omega - 1.0);

    prevTime = time;
}

void GovernorTgov1::jacobianElements(const IOdata& /*inputs*/,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, nullptr, sMode, this);

    int refI = Loc.diffOffset;
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)

    bool linkOmega = (inputLocs[govOmegaInLocation] != kNullLocation);

    /*
if (opFlags.test (uses_deadband))
  {
    if (!opFlags.test (outside_deadband))
      {
        linkOmega = false;
      }
  }
      */
    // Loc.destLoc[0] = Loc.algStateLoc[0] - Loc.diffStateLoc[0] + Dt*(omega -
    // Wref) / systemBaseFrequency;
    // Pm
    if (linkOmega) {
        md.assign(Loc.algOffset, inputLocs[govOmegaInLocation], Dt);
    }

    md.assign(Loc.algOffset, Loc.algOffset, 1);
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    md.assign(Loc.algOffset, refI, -1);

    if (opFlags[p_limited]) {
        md.assign(refI + 1, refI + 1, -sD.cj);
        md.assign(refI, refI, 1 / T3);
        md.assign(refI, refI + 1, -1 / T3 - sD.cj);
    } else {
        md.assignCheckCol(refI + 1, inputLocs[govpSetInLocation], 1 / T1);
        md.assign(refI + 1, refI + 1, -1 / T1 - sD.cj);
        if (linkOmega) {
            md.assign(refI + 1, inputLocs[govOmegaInLocation], -K / (T1));
        }

        md.assign(refI, refI + 1, (1 + T2 / T1) / T3);
        md.assignCheckCol(refI, inputLocs[govpSetInLocation], -T2 / T1 / T3);
        if (linkOmega) {
            md.assign(refI, inputLocs[govOmegaInLocation], K * T2 / (T1) / T3);
        }
        md.assign(refI, refI, -1 / T3 - sD.cj);
    }

    // Loc.destDiffLoc[0] = (Loc.diffStateLoc[1] - Loc.diffStateLoc[0] - T2 *
    // (-gs[1] + inputs[govpSetInLocation] -
    // K * (omega - Wref) / systemBaseFrequency) / T1) / T3;
}

void GovernorTgov1::rootTest(const IOdata& inputs,
                             const stateData& sD,
                             double roots[],
                             const solverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    /* if (opFlags.test (uses_deadband))
   {
     Governor::rootTest (inputs, sD, roots, sMode);
     ++rootOffset;
   }*/
    if (opFlags[uses_plimits]) {
        auto Loc = offsets.getLocations(sD, nullptr, sMode, this);

        double Pmech = Loc.diffStateLoc[1];

        if (opFlags[p_limited]) {
            // double omega = getControlFrequency (inputs);
            double omega = inputs[govOmegaInLocation];
            roots[rootOffset] = (-Pmech + inputs[govpSetInLocation] + K * (omega - 1.0)) / T1;
        } else {
            roots[rootOffset] = std::min(Pmax - Pmech, Pmech - Pmin);
            if (Pmech > Pmax) {
                opFlags.set(p_limit_high);
            }
        }
        ++rootOffset;
    }
}

void GovernorTgov1::rootTrigger(coreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const solverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    /*if (opFlags.test (uses_deadband))
  {
    if (rootMask[rootOffset])
      {
        Governor::rootTrigger (time, inputs, rootMask, sMode);
      }
    ++rootOffset;
  }
      */
    if (opFlags[uses_plimits]) {
        if (rootMask[rootOffset] != 0) {
            if (opFlags[p_limited]) {
                opFlags.reset(p_limited);
                opFlags.reset(p_limit_high);
                alert(this, JAC_COUNT_INCREASE);
                LOG_DEBUG("at max power limit");
            } else {
                if (opFlags[p_limit_high]) {
                    LOG_DEBUG("at max power limit");
                } else {
                    LOG_DEBUG("at min power limit");
                }
                opFlags.set(p_limited);
                alert(this, JAC_COUNT_DECREASE);
            }
            derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
        }
        ++rootOffset;
    }
}

index_t GovernorTgov1::findIndex(const std::string& field, const solverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if ((field == "pm") || (field == "pmech")) {
        ret = offsets.getAlgOffset(sMode);
    } else if (field == "v1") {
        ret = offsets.getDiffOffset(sMode);
    } else if (field == "v2") {
        ret = offsets.getAlgOffset(sMode);
        ret = (ret != kNullLocation) ? ret + 1 : ret;
    }
    return ret;
}

// set parameters
void GovernorTgov1::set(const std::string& param, const std::string& val)
{
    GovernorIeeeSimple::set(param, val);
}

void GovernorTgov1::set(const std::string& param, double val, unit unitType)
{
    // param   = gridDynSimulation::toLower(param);
    if (param == "dt") {
        Dt = val;
    } else {
        GovernorIeeeSimple::set(param, val, unitType);
    }
}
}  // namespace griddyn::governors
