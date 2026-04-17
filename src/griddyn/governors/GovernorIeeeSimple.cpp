/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorIeeeSimple.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactory.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace griddyn::governors {
    using units::puMW;
    using units::unit;

    GovernorIeeeSimple::GovernorIeeeSimple(const std::string& objName): Governor(objName)
    {
        // default values
        K = 16.667;
        T1 = 0.1;
        T2 = 0.15;
        T3 = 0.05;
        offsets.local().local.algSize = 0;
        offsets.local().local.diffSize = 2;
        offsets.local().local.jacSize = 6;
        opFlags.set(ignore_deadband);
        opFlags.set(ignore_filter);
        opFlags.set(ignore_throttle);
    }

    coreObject* GovernorIeeeSimple::clone(coreObject* obj) const
    {
        auto* gov = cloneBase<GovernorIeeeSimple, Governor>(this, obj);
        if (gov == nullptr) {
            return obj;
        }
        gov->T3 = T3;
        gov->Pup = Pup;
        gov->Pdown = Pdown;
        return gov;
    }

    // destructor
    GovernorIeeeSimple::~GovernorIeeeSimple() = default;
    void GovernorIeeeSimple::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        Governor::dynObjectInitializeA(time0, flags);
        if ((Pmax < 5000) || (Pmin > -5000)) {
            offsets.local().local.diffRoots++;
            opFlags.set(uses_plimits);
        }
    }

    // initial conditions
    void GovernorIeeeSimple::dynObjectInitializeB(const IOdata& /*inputs*/,
                                                  const IOdata& desiredOutput,
                                                  IOdata& fieldSet)
    {
        if (Wref < 0) {
            Wref = systemBaseFrequency;
        }
        m_state[1] = 0;
        m_state[0] = desiredOutput[0];
        fieldSet[1] = desiredOutput[0];
    }

    // residual
    void GovernorIeeeSimple::residual(const IOdata& inputs,
                                      const stateData& sD,
                                      double resid[],
                                      const solverMode& sMode)
    {
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        auto Loc = offsets.getLocations(sD, resid, sMode, this);
        derivative(inputs, sD, resid, sMode);

        Loc.destDiffLoc[0] -= Loc.dstateLoc[0];
        Loc.destDiffLoc[1] -= Loc.dstateLoc[1];
    }

    void GovernorIeeeSimple::derivative(const IOdata& inputs,
                                        const stateData& sD,
                                        double deriv[],
                                        const solverMode& sMode)
    {
        auto Loc = offsets.getLocations(sD, deriv, sMode, this);

        const double* gs = Loc.diffStateLoc;
        // double omega = getControlFrequency (inputs);
        double omega = inputs[govOmegaInLocation];
        if (opFlags[p_limited]) {
            Loc.destDiffLoc[0] = 0;
        } else {
            Loc.destDiffLoc[0] =
                (inputs[govpSetInLocation] - gs[0] - K * gs[1] - K * T2 * (omega - 1.0) / T1) / T3;
        }

        Loc.destDiffLoc[1] = (-gs[1] + (1 - T2 / T1) * (omega - 1.0)) / T1;
    }

    void GovernorIeeeSimple::timestep(coreTime time,
                                      const IOdata& inputs,
                                      const solverMode& /*sMode*/)
    {
        GovernorIeeeSimple::derivative(inputs,
                                       emptyStateData,
                                       m_dstate_dt.data(),
                                       cLocalSolverMode);
        double dt = time - prevTime;
        m_state[0] += dt * m_dstate_dt[0];
        m_state[1] += dt * m_dstate_dt[1];
        if (opFlags[p_limited]) {
        } else {
            if (m_state[0] > Pmax) {
                opFlags.set(p_limited);
                opFlags.set(p_limit_high);
                m_state[0] = Pmax;
            }
        }

        prevTime = time;
    }

    void GovernorIeeeSimple::jacobianElements(const IOdata& /*inputs*/,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              const IOlocs& inputLocs,
                                              const solverMode& sMode)
    {
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        auto Loc = offsets.getLocations(sD, nullptr, sMode, this);

        int refI = Loc.diffOffset;
        // use the md.assign Macro defined in basicDefs
        // md.assign(arrayIndex, RowIndex, ColIndex, value)
        bool linkOmega = true;
        if (inputLocs[govOmegaInLocation] == kNullLocation) {
            linkOmega = false;
        }
        /*
    if (opFlags.test (uses_deadband))
      {
        if (!opFlags.test (outside_deadband))
          {
            linkOmega = false;
          }
      }
          */
        // Pm
        if (linkOmega) {
            if (!opFlags[p_limited]) {
                md.assign(refI, inputLocs[govOmegaInLocation], -K * T2 / (T1 * T3));
            }
            md.assign(refI + 1, inputLocs[govOmegaInLocation], (T1 - T2) / (T1 * T1));
        }
        if (opFlags[p_limited]) {
            md.assign(refI, refI, sD.cj);
        } else {
            md.assign(refI, refI, -1 / T3 - sD.cj);
            md.assign(refI, refI + 1, -K / T3);

            md.assignCheck(refI, inputLocs[govpSetInLocation], 1 / T3);
        }
        md.assign(refI + 1, refI + 1, -1 / T1 - sD.cj);
    }

    index_t GovernorIeeeSimple::findIndex(const std::string& field, const solverMode& sMode) const
    {
        index_t ret = kInvalidLocation;
        if (field == "pm") {
            ret = offsets.getDiffOffset(sMode);
        } else if (field == "x") {
            ret = offsets.getDiffOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 1 : ret;
        }
        return ret;
    }

    void GovernorIeeeSimple::rootTest(const IOdata& inputs,
                                      const stateData& sD,
                                      double roots[],
                                      const solverMode& sMode)
    {
        int rootOffset = offsets.getRootOffset(sMode);
        /*if (opFlags.test (uses_deadband))
      {
        Governor::rootTest (inputs, sD, roots, sMode);
        ++rootOffset;
      }
          */
        if (opFlags[uses_plimits]) {
            auto Loc = offsets.getLocations(sD, nullptr, sMode, this);

            double Pmech = Loc.diffStateLoc[0];

            if (!opFlags[p_limited]) {
                roots[rootOffset] = std::min(Pmax - Pmech, Pmech - Pmin);
                if (Pmech > Pmax) {
                    opFlags.set(p_limit_high);
                }
            } else {
                // double omega = getControlFrequency (inputs);
                double omega = inputs[govOmegaInLocation];
                roots[rootOffset] = (inputs[govpSetInLocation] - Pmech - K * Loc.diffStateLoc[1] -
                                     K * T2 * (omega - 1.0) / T1) /
                    T3;
            }
            ++rootOffset;
        }
    }

    void GovernorIeeeSimple::rootTrigger(coreTime /*time*/,
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
        if (opFlags.test(uses_plimits)) {
            if (rootMask[rootOffset] != 0) {
                if (opFlags.test(p_limited)) {
                    opFlags.reset(p_limited);
                    opFlags.reset(p_limit_high);
                    alert(this, JAC_COUNT_INCREASE);
                } else {
                    opFlags.set(p_limited);
                    alert(this, JAC_COUNT_DECREASE);
                }

                derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
            }
            ++rootOffset;
        }
    }

    // set parameters
    void GovernorIeeeSimple::set(const std::string& param, const std::string& val)
    {
        Governor::set(param, val);
    }

    void GovernorIeeeSimple::set(const std::string& param, double val, unit unitType)
    {
        // param   = gridDynSimulation::toLower(param);
        if (param == "t3") {
            T3 = val;
        } else if (param == "pup") {
            Pup = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "pdown") {
            Pdown = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "ramplimit") {
            Pup = convert(val, unitType, puMW, systemBasePower);
            Pdown = convert(val, unitType, puMW, systemBasePower);
        } else {
            Governor::set(param, val, unitType);
        }
    }

}  // namespace griddyn::governors
