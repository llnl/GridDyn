/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"

namespace griddyn::links {

/**
 * @brief PowerModels-compatible representation of a PSS/E RAW DC line.
 *
 * PSS/E RAW DC records are translated by PowerModels into the ``dcline``
 * abstraction.  It is not a physical DC network: active terminal powers are
 * scheduled, and a terminal on a PQ AC bus receives an independent reactive
 * power variable together with a voltage-magnitude setpoint.  This class
 * supplies those AC-terminal equations without altering GridDyn's physical
 * DcBus, DcLink, AcDcConverter, VSCShunt, or Hvdc models.
 *
 * A GridDyn PV/slack bus already eliminates its reactive balance equation, so
 * the compatibility reactive variable is only required on a PQ terminal.  A
 * reader must select at most one voltage controller for any PQ bus; additional
 * RAW DC terminals at that bus retain their specified initial reactive flow.
 */
class RawDcLine final: public Link {
  private:
    double fromVoltageTarget = 1.0;
    double toVoltageTarget = 1.0;
    double fromReactivePower = 0.0;
    double toReactivePower = 0.0;
    bool controlFromVoltage = false;
    bool controlToVoltage = false;

    index_t fromReactiveOffset(const SolverMode& sMode) const;
    index_t toReactiveOffset(const SolverMode& sMode) const;

  public:
    explicit RawDcLine(const std::string& objName = "rawdcline_$");

    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param,
               units::unit unitType = units::defunit) const override;

    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    StateSizes localStateSizes(const SolverMode& sMode) const override;
    count_t localJacobianCount(const SolverMode& sMode) const override;

    void updateLocalCache() override;
    void updateLocalCache(const IOdata& inputs,
                          const StateData& stateDataValue,
                          const SolverMode& sMode) override;
    using Link::outputPartialDerivatives;
    void outputPartialDerivatives(id_type_t busId,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const SolverMode& sMode) override;
    count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateDataValue,
                          MatrixData<double>& matrixDataValue,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void residual(const IOdata& inputs,
                  const StateData& stateDataValue,
                  double resid[],
                  const SolverMode& sMode) override;
    void setState(CoreTime time,
                  const double state[],
                  const double dstateDt[],
                  const SolverMode& sMode) override;
    void guessState(CoreTime time,
                    double state[],
                    double dstateDt[],
                    const SolverMode& sMode) override;
    void getStateName(stringVec& stNames,
                      const SolverMode& sMode,
                      const std::string& prefix = "") const override;
};

}  // namespace griddyn::links
