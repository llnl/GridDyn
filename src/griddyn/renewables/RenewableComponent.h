/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GridSubModel.h"
#include <array>
#include <span>

namespace griddyn {

/** Attachment roles are independent of the concrete dynamics-file model name. */
enum class RenewableRole: index_t {
    electrical = 0,
    electricalControl,
    plantControl,
    driveTrain,
    aerodynamics,
    pitchControl,
    torqueControl,
    rotorResistanceControl,
    count,
};

enum class RenewableSignal {
    terminalVoltage,
    terminalAngle,
    terminalFrequency,
    electricalPower,
    reactivePower,
    activeCurrentCommand,
    reactiveCurrentCommand,
    activeReference,
    reactiveReference,
    activeReferenceIncrement,
    reactiveReferenceIncrement,
    orderedPower,
    speedReference,
    mechanicalPower,
    generatorSpeed,
    turbineSpeed,
    pitchAngle,
    initialPitchAngle,
    rotorResistance,
};

enum class RenewableBase { none, system, machine, turbine };

struct RenewablePort {
    RenewableSignal signal;
    index_t ioIndex;
    RenewableBase base = RenewableBase::none;
    bool required = true;
};

/** Common connection contract for renewable submodels. */
class RenewableComponent: public GridSubModel {
  public:
    explicit RenewableComponent(const std::string& name = "renewableComponent_#"):
        GridSubModel(name)
    {
    }
    virtual RenewableRole role() const = 0;
    virtual std::span<const RenewablePort> inputPorts() const = 0;
    virtual std::span<const RenewablePort> outputPorts() const = 0;
};

/** The one mandatory grid-facing role: terminal generation P/Q on machine base. */
class TerminalElectricalModel: public RenewableComponent {
  public:
    explicit TerminalElectricalModel(const std::string& name = "renewableElectrical_#"):
        RenewableComponent(name)
    {
        m_outputSize = 2;
    }
    RenewableRole role() const final { return RenewableRole::electrical; }
};

}  // namespace griddyn
