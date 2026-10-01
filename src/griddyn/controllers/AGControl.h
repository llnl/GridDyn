/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/GridSubModel.h"
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
class SchedulerReg;
class Sensor;

/** Area generation control updated only at its configured sampling interval. */
class AGControl: public GridSubModel {
  protected:
    double ki = 0.005;
    double kp = 1.0;
    double bias = -8.0;  //!< MW per 0.1 Hz, negative by convention
    double deadband = 0.0;  //!< MW of ACE
    double tf = 8.0;
    double tr = 15.0;
    double ace = 0.0;  //!< MW, positive means excess export
    double filteredAce = 0.0;  //!< MW
    double integralAce = 0.0;  //!< MW seconds
    double requestedReg = 0.0;  //!< pu on system base
    double reg = 0.0;  //!< dispatched pu on system base
    double regUpAvailable = 0.0;
    double regDownAvailable = 0.0;
    double fixedFrequency = 1.0;  //!< pu; can be set by a Player
    double targetFrequency = 1.0;  //!< pu
    std::string frequencySensorName;
    Sensor* frequencySensor = nullptr;
    index_t frequencyOutput = 0;
    bool frequencyIsDeviation = false;
    bool frequencyOutputExplicit = false;
    bool frequencyDeviationExplicit = false;
    bool initialized = false;
    std::vector<SchedulerReg*> schedList;

  public:
    explicit AGControl(const std::string& objName = "AGC_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    ~AGControl() override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void updateA(CoreTime time) override;
    CoreTime updateB() override;

    double getOutput(const IOdata& inputs,
                     const StateData& sD,
                     const SolverMode& sMode,
                     index_t num = 0) const override;
    double getOutput(index_t num = 0) const override;
    void add(CoreObject* obj) override;
    void add(SchedulerReg* sched);
    void remove(CoreObject* obj) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    double getACE() const { return ace; }
    double getfACE() const { return filteredAce; }
    double getRegulation() const { return reg; }
    void regChange();

  private:
    void addInterchangeSlackParticipant();
    double measuredFrequency() const;
    double measuredACE() const;
    void dispatch();
};
}  // namespace griddyn
