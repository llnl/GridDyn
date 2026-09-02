/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "utilities/indexTypes.hpp"
#include <array>
#include <vector>

namespace griddyn {
/** Machine quantities exposed to generator controllers.
 *
 * Values use the generator model's machine base and GridDyn's established dq
 * convention.  For GENROU, positive Id and Vd are the negatives of the ANDES
 * Id and vd convention; Iq and Vq have the same sign. Terminal electrical
 * power, air-gap electrical torque, and XadIfd are scalar machine-base
 * per-unit quantities. GENROU supplies its
 * full-order XadIfd equation. Reduced-order synchronous models use their
 * transient q-axis state where available; the classical model uses excitation
 * voltage as a coupled field-current proxy because it has no field-winding
 * state. Non-synchronous models leave unsupported signals at kNullVal.
 */
enum class MachineControllerSignal : index_t {
    ID = 0,
    IQ,
    VD,
    VQ,
    ELECTRICAL_POWER,
    ELECTRICAL_TORQUE,
    XADIFD,
    COUNT,
};

inline constexpr count_t machineControllerSignalCount =
    static_cast<count_t>(MachineControllerSignal::COUNT);

struct MachineSignalDerivative {
    index_t location;
    double value;
};

using MachineSignalDerivativeData =
    std::array<std::vector<MachineSignalDerivative>, machineControllerSignalCount>;
}  // namespace griddyn
