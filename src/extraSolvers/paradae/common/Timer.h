/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../math/SVector.h"
#include "def.h"
#include "mpi.h"
#include <cstring>
#include <map>
#include <string>

namespace griddyn::paradae {

#ifndef TIMER_INTRUSION  // 0->global 1->init,run,out 2->feval,jaceval,facto,solve
#    define TIMER_INTRUSION 1
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_COMMON_TIMER_H_

#ifdef TIMER_BRAID
#    if TIMER_INTRUSION < 1
#        undef TIMER_INTRUSION
#        define TIMER_INTRUSION 1
#    endif
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_COMMON_TIMER_H_

#ifdef TIMER_GRIDDYN
#    if TIMER_INTRUSION < 1
#        undef TIMER_INTRUSION
#        define TIMER_INTRUSION 1
#    endif
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_COMMON_TIMER_H_

    const int MAX_NTIME = 100;

    class TimedElem {
        std::string name;
        bool is_started;
        Real start_time;
        Real cumul_time;

      public:
        TimedElem(): name(""), is_started(false), start_time(0), cumul_time(0) {};
        TimedElem(std::string name_):
            name(name_), is_started(false), start_time(0), cumul_time(0) {};
        void Start(std::string name_ = "");
        void Stop();
        Real GetTime() { return cumul_time + ((is_started) ? MPI_Wtime() - start_time : 0); };
        std::string GetName() { return name; };
    };

    class Timer: public std::map<std::string, int> {
        MPI_Comm comm;
        int mpi_rank;
        int mpi_size;
        int nb_elems;
        TimedElem elems[MAX_NTIME];
        int depends_on[MAX_NTIME];
        int depth[MAX_NTIME];
        int max_depth;
        SVector min_t, avg_t, max_t;
        unsigned int sstr;

        void Gather();

      public:
        Timer(): mpi_rank(0), mpi_size(1), nb_elems(0), sstr(0) {};
        Timer(MPI_Comm comm_);
        void SetComm(MPI_Comm comm_);
        void Start(std::string key, std::string output = "", std::string dep = "");
        void Stop(std::string key, bool barrier = false);
        void StopAll();
        Real GetCurrentTime(std::string key);
        void ShowStats(int deps = -1);
    };

    extern Timer global_timer;

}  // namespace griddyn::paradae
