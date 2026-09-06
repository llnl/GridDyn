/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModelGENROU.h"
#include <string>

namespace griddyn::genmodels {
/**
 * @brief Sixth-order PSS/E GENROE round-rotor synchronous-machine model.
 *
 * GENROE has the same electrical states, initialization, and differential
 * equations as GENROU.  Its saturation characteristic is instead the PSS/E
 * exponential fit through S(1.0) and S(1.2):
 * \f[
 * S_e(\psi)=S_{10}\psi^A,\qquad
 * A=\frac{\log(S_{12}/S_{10})}{\log(1.2)}.
 * \f]
 * The inherited GENROU equations use this curve in rotor-angle
 * initialization, field-voltage initialization, both transient-axis
 * equations, and their analytic Jacobian.  Keeping this as a separate type
 * prevents the materially different saturation law from being silently
 * substituted when a GENROE DYR record is read.
 *
 * @par Equation sources
 * - OpenIPSL GENROE and `SE_exp`, commit
 *   8155c73f51ceeec935eb158247c7c043eb697ff5.
 * - PowerDynamics.jl `PSSE_GENROUND`/`PSSE_GENROE` and `EXP_SE`, commit
 *   908306c67b85fb24249955277b97e2e4f3d9b837.
 */
class GenModelGENROE final: public GenModelGENROU {
  public:
    explicit GenModelGENROE(const std::string& objName = "genroe_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};

}  // namespace griddyn::genmodels
