/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModelGENSAL.h"
#include <string>

namespace griddyn::genmodels {
/** PSS/E GENSAE salient-pole synchronous-machine model.
 *
 * GENSAE shares GENSAL's five-state salient-pole realization, but uses the
 * PSS/E exponential saturation curve evaluated at the air-gap flux magnitude
 * \f$\psi=\sqrt{\psi_{d}^{\prime\prime2}+\psi_{q}^{\prime\prime2}}\f$:
 * \f[
 * S_e(\psi)=S_{10}\psi^A,\qquad
 * A=\frac{\log(S_{12}/S_{10})}{\log(1.2)}.
 * \f]
 * In contrast to GENSAL, this term appears in both the field-current relation
 * and q-axis subtransient equation:
 * \f[
 * \begin{aligned}
 * X_{ad}I_{fd}&=E'_q+K_{1d}[E'_q-\psi_{kd}+(x'_d-x_l)I_d]
 * -(x_d-x'_d)I_d+S_e(\psi)\psi''_d,\\
 * T''_{q0}\dot\psi''_q&=-\psi''_q-(x_q-x''_q)I_q
 * -\frac{x_q-x_l}{x_d-x_l}S_e(\psi)\psi''_q.
 * \end{aligned}
 * \f]
 * GridDyn's direct and quadrature current signs follow its existing GENSAL
 * convention.  The remaining stator, swing, and rotor equations are shared
 * with GENSAL.
 *
 * @par Equation sources
 * - OpenIPSL `Electrical.Machines.PSSE.GENSAE`, commit 8155c73f.
 * - PowerDynamics.jl `PSSE_GENSALIENT`/`PSSE_GENSAE`, commit
 *   908306c67b85fb24249955277b97e2e4f3d9b837.
 */
class GenModelGENSAE final: public GenModelGENSAL {
  public:
    explicit GenModelGENSAE(const std::string& objName = "gensae_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

  protected:
    bool usesExponentialSaturation() const override;
};

}  // namespace griddyn::genmodels
