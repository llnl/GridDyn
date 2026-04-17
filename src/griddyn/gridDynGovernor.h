/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridSubModel.h"
#include "submodels/controlBlocks/controlBlock.h"
#include "submodels/controlBlocks/deadbandBlock.h"
#include "submodels/controlBlocks/delayBlock.h"
#include <string>

namespace griddyn {
const int govOmegaInLocation = 0;
const int govpSetInLocation = 1;
/** @brief class defining the interface for a governor
 the governor class is a really basic governor it includes two time constants
and takes as input the frequency and power setting*/
class Governor: public gridSubModel {
  public:
    /** @brief flags for governor control*/
    enum governor_flags {
        ignore_deadband = object_flag2,  //!< indicator that the deadband block should be ignored
        ignore_filter = object_flag3,  //!< indicator that the filter block should be ignored
        ignore_throttle = object_flag4,  //!< indicator that the delay block should be ignored
        p_limited = object_flag5,  //!< indicator that power level was limited
        p_limit_high = object_flag6,  //!< indicator that the throttle level was at the high limit
        uses_plimits = object_flag7,  //!< indicator that the governor uses limits
        uses_ramplimits = object_flag8,  //!< indicator that the governor had ramp limits
    };

  protected:
    double K = 16.667;  //!< [pu] droop gain (1/R)
    double T1 = 0.1;  //!< [s]   droop control time constant 1
    double T2 = 0.0;  //!< [s]   droop control  time constant 2
    double T3 = 0.0;  //!< [s]  throttle response
    double Pmax = kBigNum;  //!< [pu] maximum turbine output
    double Pmin = -kBigNum;  //!< [pu] minimum turbine output
    double Pset = 0.0;  //!< [pu] Set point and initial Pm
    double Wref = -kBigNum;  //!<[rad]  reference frequency
    double deadbandHigh = -kBigNum;  //!< upper threshold on the deadband;
    double deadbandLow = kBigNum;  //!< lower threshold on the deadband;
    double machineBasePower = 100.0;  //!< the machine base of the generator;
    blocks::deadbandBlock dbb;  //!< block managing the deadband
    blocks::controlBlock cb;  //!< block managing the filtering functions on the frequency response
    blocks::delayBlock delay;  //!< block managing the throttle filter
  public:
    /** @brief constructor*/
    explicit Governor(const std::string& objName = "gov_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    /** @brief destructor*/
    virtual ~Governor();
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void set(const std::string& param,
                     double val,
                     gridUnits::units_t unitType = gridUnits::defUnit) override;
    virtual void setFlag(const std::string& flag, bool val) override;
    virtual double get(const std::string& param,
                       gridUnits::units_t unitType = gridUnits::defUnit) const override;
    virtual index_t findIndex(const std::string& field, const solverMode& sMode) const override;
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode) override;

  protected:
    // virtual void setTime(coreTime time){prevTime=time;};
};

}  // namespace griddyn
