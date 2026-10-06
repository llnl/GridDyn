/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ControllerSignals.h"
#include "GridSubModel.h"
#include <string>

namespace griddyn {
inline constexpr int voltageCompensatorVoltageInLocation = 0;
inline constexpr int voltageCompensatorMachineSignalBase = 1;
inline constexpr int voltageCompensatorIdInLocation =
    voltageCompensatorMachineSignalBase + static_cast<int>(MachineControllerSignal::ID);
inline constexpr int voltageCompensatorIqInLocation =
    voltageCompensatorMachineSignalBase + static_cast<int>(MachineControllerSignal::IQ);
inline constexpr int voltageCompensatorVdInLocation =
    voltageCompensatorMachineSignalBase + static_cast<int>(MachineControllerSignal::VD);
inline constexpr int voltageCompensatorVqInLocation =
    voltageCompensatorMachineSignalBase + static_cast<int>(MachineControllerSignal::VQ);
inline constexpr count_t voltageCompensatorInputCount =
    voltageCompensatorMachineSignalBase + machineControllerSignalCount;

/** Base class for algebraic voltage-sensing compensators attached to a generator. */
class VoltageCompensator: public GridSubModel {
  public:
    explicit VoltageCompensator(const std::string& objName = "vcomp_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    const std::vector<stringVec>& inputNames() const override;
    const std::vector<stringVec>& outputNames() const override;
};
}  // namespace griddyn
