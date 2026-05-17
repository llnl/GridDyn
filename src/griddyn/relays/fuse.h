/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <cstdint>
#include <string>
namespace griddyn::relays {
/** fuse implements a standard power system fuse which can blow on time or using and I^2t
 * calculation
 */
class fuse: public Relay {
  public:
    /** flags for fuses*/
    enum FuseFlags {
        OVERLIMIT_FLAG = object_flag10,  //!< flag indicating the current is over the limit
        FUSE_BLOWN_FLAG = object_flag11,  //!< flag indicting the fuse was blown
        NONLINK_SOURCE_FLAG = object_flag12,  //!< flag indicating the source is a not a link
    };

  protected:
    index_t m_terminal = 1;  //!< line terminal  (typically 1 or 2)
    // 4 byte gap here to use for something if need be
    model_parameter limit = kBigNum;  //!<[puA] maximum current
    model_parameter mp_I2T = 0.0;  //!<[puA^2*s] I squared t characteristic of fuse 1 in puA^2*s
    coreTime minBlowTime =
        0.001;  //!<[s] the minimum time required to for the fuse to blow only used if I2T>0;
  private:
    double cI2T = 0.0;  //!< calculated I2t value for fuse
    double Vbase = 120;  //!<[kV] Voltage base for bus
    gridBus* bus = nullptr;  //!< storage for a bus which the line terminal or other object
    bool& useI2T;  //!< internal flag for using the i^2t functionality
  public:
    explicit fuse(const std::string& objName = "fuse_$");
    virtual CoreObject* clone(CoreObject* obj) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    // dynamic functions for evaluation with a limit exceeded
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const solverMode& sMode) override;
    virtual void converge(coreTime time,
                          double state[],
                          double dstate_dt[],
                          const solverMode& sMode,
                          converge_mode = converge_mode::high_error_only,
                          double tol = 0.01) override;
    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix) const override;

  protected:
    virtual void conditionTriggered(index_t conditionNum, coreTime triggerTime) override;
    /** function to setup the numerical calculations associated with the fuse*/
    change_code setupFuseEvaluation();

  private:
    /** helper calculation function to compute the i^2 t value based on a current*/
    double I2Tequation(double current);
    /** actually blow the fuse
@return a change_code associated with the action to match a function signature*/
    change_code blowFuse();
};
}  // namespace griddyn::relays

