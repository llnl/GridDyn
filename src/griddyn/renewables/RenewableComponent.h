/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GridSubModel.h"
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace griddyn {

/** Attachment roles are independent of the concrete dynamics-file model name. */
enum class RenewableRole : index_t {
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
    measuredAngle,
    terminalFrequency,
    regulationVoltage,
    electricalPower,
    reactivePower,
    activeCurrentCommand,
    reactiveCurrentCommand,
    /** Type-3 DFIG converter reactive-voltage command (WEQCMD/Eqcmd). */
    reactiveVoltageCommand,
    /** Initialization-only WT3G command supplied to the WT3 electrical control. */
    initialActiveCurrentCommand,
    /** Initialization-only WT3G reactive-voltage command supplied to WT3E. */
    initialReactiveVoltageCommand,
    activeReference,
    reactiveReference,
    activeReferenceIncrement,
    reactiveReferenceIncrement,
    orderedPower,
    speedReference,
    mechanicalPower,
    generatorSpeed,
    synchronousSpeed,
    turbineSpeed,
    frequencyDeviation,
    rateOfChangeOfFrequency,
    pitchAngle,
    initialPitchAngle,
    rotorResistance,
    stateOfCharge,
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
    explicit RenewableComponent(const std::string& objName = "renewableComponent_#"):
        GridSubModel(objName)
    {
    }
    virtual RenewableRole role() const = 0;
    virtual std::span<const RenewablePort> inputPorts() const = 0;
    virtual std::span<const RenewablePort> outputPorts() const = 0;
    virtual std::string_view sourceName(RenewableSignal) const { return {}; }
};

/** The one mandatory grid-facing role: terminal generation P/Q on machine base. */
class TerminalElectricalModel: public RenewableComponent {
  public:
    explicit TerminalElectricalModel(const std::string& objName = "renewableElectrical_#"):
        RenewableComponent(objName)
    {
        m_outputSize = 2;
    }
    RenewableRole role() const final { return RenewableRole::electrical; }
};

}  // namespace griddyn
