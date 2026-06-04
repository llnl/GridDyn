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
class Breaker: public Relay {
  public:
    enum BreakerFlags {
        NONDIRECTIONAL_FLAG = OBJECT_FLAG8,  //!< flag indicating that the detection should not
                                             //!< be based on direction
        OVERLIMIT_FLAG = OBJECT_FLAG9,  //!< flag indicating that the current is over the limit
        BREAKER_TRIPPED_FLAG = OBJECT_FLAG10,  //!< flag indicating that the breaker has tripped
        NONLINK_SOURCE_FLAG =
            OBJECT_FLAG11,  //!< flag indicating that the source is not a transmission line
    };

  protected:
    CoreTime mMinClearingTime = timeZero;  //!<[s] minimum clearing time for from bus breaker
    CoreTime mRecloseTime1 = timeOneSecond;  //!<[s] first reclose time
    CoreTime mRecloseTime2 = 5.0;  //!<[s] second reclose time
    model_parameter mRecloserTap = 0.0;  //!< From side tap multiplier
    model_parameter mLimit = 1.0;  //!<[puA] maximum current in puA
    CoreTime mLastRecloseTime = negTime;  //!<[s] last reclose time
    CoreTime mRecloserResetTime =
        CoreTime(60.0);  //!<[s] time the breaker has to be on before the recloser count resets
    std::uint16_t mMaxRecloseAttempts = 0;  //!< total number of recloses
  private:
    std::uint16_t mRecloseAttempts = 0;  //!< reclose attempt counter
  protected:
    index_t m_terminal = 1;  //!< link terminal
    GridBus* mBus = nullptr;

  private:
    double mCti = 0.0;  //!< storage for the current integral
    double mVoltageBase = 120.0;  //!< Voltage base for bus1
    bool& mUseCti;  //!< internal flag to use the CTI stuff link to a CoreObject extra boolean
  public:
    /** constructor with object name*/
    explicit Breaker(const std::string& objName = "breaker_$");
    virtual CoreObject* clone(CoreObject* obj) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void updateA(CoreTime time) override;

    // dynamic state functions
    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataRef,
                                  MatrixData<double>& jacobian,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataRef,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void guessState(CoreTime time,
                            double state[],
                            double dstateDt[],
                            const SolverMode& sMode) override;
    virtual StateSizes localStateSizes(const SolverMode& sMode) const override;

    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const override;

  protected:
    virtual void conditionTriggered(index_t conditionNum, CoreTime triggeredTime) override;
    /** trip the breaker
@param[in] time current time
*/
    void tripBreaker(CoreTime time);
    /** reset the breaker
@param[in] time current time
*/
    void resetBreaker(CoreTime time);
};

}  // namespace griddyn::relays
