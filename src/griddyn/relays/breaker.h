/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_RELAYS_BREAKER_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_RELAYS_BREAKER_H_

#include "../Relay.h"
#include <string>
namespace griddyn {
namespace relays {
    /** relay implementing a overcurrent breaker for a transmission line
     */
    class breaker: public Relay {
      public:
        enum breaker_flags {
            nondirectional_flag = object_flag8,  //!< flag indicating that the detection should not
                                                 //!< be based on direction
            overlimit_flag = object_flag9,  //!< flag indicating that the current is over the limit
            breaker_tripped_flag = object_flag10,  //!< flag indicating that the breaker has tripped
            nonlink_source_flag =
                object_flag11,  //!< flag indicating that the source is not a transmission line
        };

      protected:
        coreTime minClearingTime = timeZero;  //!<[s] minimum clearing time for from bus breaker
        coreTime recloseTime1 = timeOneSecond;  //!<[s] first reclose time
        coreTime recloseTime2 = 5.0;  //!<[s] second reclose time
        model_parameter recloserTap = 0.0;  //!< From side tap multiplier
        model_parameter limit = 1.0;  //!<[puA] maximum current in puA
        coreTime lastRecloseTime = negTime;  //!<[s] last reclose time
        coreTime recloserResetTime =
            coreTime(60.0);  //!<[s] time the breaker has to be on before the recloser count resets
        std::uint16_t maxRecloseAttempts = 0;  //!< total number of recloses
      private:
        std::uint16_t recloseAttempts = 0;  //!< reclose attempt counter
      protected:
        index_t m_terminal = 1;  //!< link terminal
        gridBus* bus = nullptr;

      private:
        double cTI = 0.0;  //!< storage for the current integral
        double Vbase = 120.0;  //!< Voltage base for bus1
        bool& useCTI;  //!< internal flag to use the CTI stuff link to a coreObject extra boolean
      public:
        /** constructor with object name*/
        explicit breaker(const std::string& objName = "breaker_$");
        virtual coreObject* clone(coreObject* obj) const override;
        virtual void setFlag(const std::string& flag, bool val = true) override;
        virtual void set(const std::string& param, const std::string& val) override;

        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void updateA(coreTime time) override;

        // dynamic state functions
        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
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

}  // namespace relays
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_RELAYS_BREAKER_H_
