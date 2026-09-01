/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModel5.h"

namespace griddyn::genmodels {
/** PSS/E GENSAL salient-pole synchronous-machine model.
 *
 * The algebraic states are \f$[I_d,I_q]\f$ and the differential states are
 * \f$[\delta,\omega,E'_q,\psi_{kd},\psi''_q]\f$.  GridDyn uses the same
 * terminal-power convention as its GENROU model; its direct-axis current and
 * voltage have the opposite sign to OpenIPSL.  With
 * \f$\psi''_d=K_{3d}E'_q+K_{4d}\psi_{kd}\f$, the stator equations are
 * \f[
 * 0=V_d+r_aI_d+x''_qI_q-\psi''_q,\qquad
 * 0=V_q+r_aI_q-x''_dI_d-\psi''_d.
 * \f]
 * The rotor circuits are
 * \f[
 * \begin{aligned}
 * T'_{d0}\dot E'_q&=E_f-X_{ad}I_{fd},\\
 * T''_{d0}\dot\psi_{kd}&=E'_q-\psi_{kd}+(x'_d-x_l)I_d,\\
 * T''_{q0}\dot\psi''_q&=-\psi''_q-(x_q-x''_q)I_q,
 * \end{aligned}
 * \f]
 * where
 * \f[
 * X_{ad}I_{fd}=K_{1d}[E'_q-\psi_{kd}+(x'_d-x_l)I_d]
 * -(x_d-x'_d)I_d+[1+S(E'_q)]E'_q.
 * \f]
 * Quadratic saturation and initialization follow OpenIPSL GENSAL.  The
 * parameter order accepted by the DYR reader follows the PSS/E schema frozen
 * in ANDES: TD10, TD20, TQ20, H, D, XD, XQ, XD1, XD2, XL, S10, S12.
 *
 * @see OpenIPSL.Electrical.Machines.PSSE.GENSAL
 */
class GenModelGENSAL final: public GenModel5 {
  public:
    explicit GenModelGENSAL(const std::string& objName = "gensal_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    stringVec localStateNames() const override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    IOdata getMachineControllerSignals(const IOdata& inputs,
                                       const StateData& stateData,
                                       const SolverMode& sMode) const override;
    MachineSignalDerivativeData
        getMachineControllerSignalDerivatives(const IOdata& inputs,
                                              const StateData& stateData,
                                              const IOlocs& inputLocs,
                                              const SolverMode& sMode) const override;
};
}  // namespace griddyn::genmodels
