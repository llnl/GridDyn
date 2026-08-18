/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "AcDcConverter.h"
#include <queue>
#include <string>

namespace griddyn {
class DcBus;

namespace links {
    /**
     * @brief ANDES-compatible shunt voltage-source converter.
     *
     * This is a static AC/DC three-terminal link: terminal 1 is the AC bus and
     * terminals 2 and 3 are the positive and negative DC nodes.  Its algebraic
     * equations follow ANDES' ``VSCShunt`` power-flow model.
     */
    class VSCShunt final: public AcDcConverter {
      public:
        enum class Control { PQ = 0, PV = 1, VQ = 2, VV = 3 };

      private:
        DcBus* dcReference = nullptr;
        Control control = Control::PQ;
        double v0 = 1.0;
        double p0 = 0.0;
        double q0 = 0.0;
        double vdc0 = 1.0;
        double k0 = 0.0;
        double k1 = 0.0;
        double k2 = 0.0;
        double droop = 0.0;
        double droopK = 0.0;
        double vhigh = 9999.0;
        double vlow = 0.0;
        double vshmax = 1.1;
        double vshmin = 0.9;
        double ishmax = 2.0;
        bool currentBalance = false;

        // Saved algebraic variables: ash, vsh, psh, qsh, and pdc.
        double ash = 0.0;
        double vsh = 1.0;
        double psh = 0.0;
        double qsh = 0.0;
        double pdc = 0.0;

        double dcVoltageDifference() const;
        void updateFlows(double dcVoltage, double dcReferenceVoltage);

      public:
        explicit VSCShunt(const std::string& objName = "vscshunt_$");

        CoreObject* clone(CoreObject* obj = nullptr) const override;
        void updateBus(GridBus* bus, index_t busnumber) override;
        count_t terminalCount() const override { return 3; }
        bool isConnected() const override;
        GridBus* getBus(index_t busInd) const override;
        void followNetwork(int network, std::queue<GridBus*>& stk) override;

        void
            set(std::string_view param, double val, units::unit unitType = units::defunit) override;
        void set(std::string_view param, std::string_view val) override;

        void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
        void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
        StateSizes localStateSizes(const SolverMode& sMode) const override;
        count_t localJacobianCount(const SolverMode& sMode) const override;

        void updateLocalCache() override;
        void updateLocalCache(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode) override;
        void ioPartialDerivatives(id_type_t busId,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
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
        double getRealPower(id_type_t busId = 0) const override;
        double getReactivePower(id_type_t busId = 0) const override;
    };

}  // namespace links
}  // namespace griddyn
