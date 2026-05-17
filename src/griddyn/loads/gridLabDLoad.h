/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "rampLoad.h"
#include <string>
#include <vector>

namespace griddyn {
class GhostSwingBusManager;
// to set up a dummy load function we need this header file
#ifndef HAVE_MPI
// forward declaration of voltage and current messages
struct VoltageMessage;
struct CurrentMessage;
#endif

namespace loads {
    /** @brief load model defining the interactions with a gridlabD simulation through the Ghost
     * Swing Bus Manager*/
    class gridLabDLoad: public rampLoad {
      public:
        gridLabDLoad(const std::string& objName = "gridlabDLoad_$");

        virtual ~gridLabDLoad();

        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void pFlowObjectInitializeB() override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        virtual void
            preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode) override;

        virtual void updateA(coreTime time) override;
        virtual coreTime updateB() override;

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void
            set(std::string_view param, double val, units::unit unitType = units::defunit) override;
        virtual void add(coreObject* obj) override;

        virtual void rootTest(const IOdata& inputs,
                              const stateData& sD,
                              double roots[],
                              const solverMode& sMode) override;
        virtual void rootTrigger(coreTime time,
                                 const IOdata& inputs,
                                 const std::vector<int>& rootMask,
                                 const solverMode& sMode) override;
        virtual change_code rootCheck(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode,
                                      check_level_t level) override;
        /** @brief return a count of the number of MPI objects the load requires*/
        int mpiCount() const;
        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;

      private:
        // double abstime;
        double m_mult = 1.0;  //!< a load multiplier
        enum class coupling_mode_t { none, interval, trigger, full };
        enum class coupling_detail_t { single, VDep, triple };
        double spread = 0.01;  //!< the voltage spread to use when calculating the parameters
        double Vprev = 0.0;  //!< storage for recent voltage call
        double Thprev = 0.0;  //!< storage for recent phase call (phase is not really used yet)
        double triggerBound = 1.5;  //!< the bounds on the voltage in terms of the spread
                                    //!< determining when to generate a new calculation
        coreTime m_lastCallTime = negTime;
        void gridLabDInitialize(void);
        void runGridLabA(coreTime time, const IOdata& inputs);
        std::vector<double> runGridLabB(bool unbalancedAlert);
        void run2GridLabA(coreTime time, const IOdata& inputs);
        std::vector<double> run2GridLabB(bool unbalancedAlert);
        void run3GridLabA(coreTime time, const IOdata& inputs);
        std::vector<double> run3GridLabB(bool unbalancedAlert);

        stringVec gridlabDfile;  //!< the file to run in gridlabd
        stringVec workdir;  //!< working directory for the gridlabd task

        std::vector<int> task_id;  //!< the taskid of the remote task
        std::vector<int> forward_task_id;  //!< task id of the forward task
        enum gridlabd_flags {
            file_sent_flag = object_flag6,
            uses_bounds_flag = object_flag7,
            waiting_flag = object_flag8,
            dual_mode_flag = object_flag9,
            linearize_triple = object_flag10,
        };

        coupling_mode_t pFlowCoupling = coupling_mode_t::trigger;  //!< the coupling pflow mode
        coupling_mode_t dynCoupling = coupling_mode_t::trigger;  //!< the coupling dynamic mode
        coupling_detail_t cDetail = coupling_detail_t::triple;  //!< the detail of the check
        index_t lastSeqID = kNullLocation;
        std::vector<std::unique_ptr<Load>> dummy_load;  //!< a dummy load for testing without MPI
        std::vector<std::unique_ptr<Load>>
            dummy_load_forward;  //!< the dummy load for forward projection
#ifndef HAVE_MPI
        void run_dummy_load(index_t kk, VoltageMessage* vm, CurrentMessage* cm);
        void run_dummy_load_forward(index_t kk, VoltageMessage* vm, CurrentMessage* cm);
#endif
    };
}  // namespace loads
}  // namespace griddyn
