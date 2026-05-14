/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <string>
namespace griddyn::relays {
/** relay implementing a overcurrent breaker for a transmission line
 */
class breaker: public Relay {
  public:
    enum BreakerFlags {
        NONDIRECTIONAL_FLAG = object_flag8,  //!< flag indicating that the detection should not
                                             //!< be based on direction
        OVERLIMIT_FLAG = object_flag9,  //!< flag indicating that the current is over the limit
        BREAKER_TRIPPED_FLAG = object_flag10,  //!< flag indicating that the breaker has tripped
        NONLINK_SOURCE_FLAG =
            object_flag11,  //!< flag indicating that the source is not a transmission line
    };

  protected:
    coreTime mMinClearingTime = timeZero;  //!<[s] minimum clearing time for from bus breaker
    coreTime mRecloseTime1 = timeOneSecond;  //!<[s] first reclose time
    coreTime mRecloseTime2 = 5.0;  //!<[s] second reclose time
    model_parameter mRecloserTap = 0.0;  //!< From side tap multiplier
    model_parameter mLimit = 1.0;  //!<[puA] maximum current in puA
    coreTime mLastRecloseTime = negTime;  //!<[s] last reclose time
    coreTime mRecloserResetTime =
        coreTime(60.0);  //!<[s] time the breaker has to be on before the recloser count resets
    std::uint16_t mMaxRecloseAttempts = 0;  //!< total number of recloses
  private:
    std::uint16_t mRecloseAttempts = 0;  //!< reclose attempt counter
  protected:
    index_t m_terminal = 1;  //!< link terminal
    gridBus* mBus = nullptr;

  private:
    double mCti = 0.0;  //!< storage for the current integral
    double mVoltageBase = 120.0;  //!< Voltage base for bus1
    bool& mUseCti;  //!< internal flag to use the CTI stuff link to a coreObject extra boolean
  public:
    /** constructor with object name*/
    explicit breaker(const std::string& objName = "breaker_$");
    virtual coreObject* clone(coreObject* obj) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void updateA(coreTime time) override;

    // dynamic state functions
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& stateDataRef,
                                  matrixData<double>& jacobian,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const stateData& stateDataRef,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const solverMode& sMode) override;
    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix) const override;

  protected:
    virtual void conditionTriggered(index_t conditionNum, coreTime triggeredTime) override;
    /** trip the breaker
@param[in] time current time
*/
    void tripBreaker(coreTime time);
    /** reset the breaker
@param[in] time current time
*/
    void resetBreaker(coreTime time);
};

}  // namespace griddyn::relays
