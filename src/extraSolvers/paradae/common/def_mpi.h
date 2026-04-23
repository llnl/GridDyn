/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <print>

#ifdef CHECK_TIMING

#    include "mpi.h"

Real t1_MACRO, t2_MACRO;
Real dt_MACRO, *alldt_MACRO;
Real global_time_MACRO;
Real min_MACRO, max_MACRO, avg_MACRO;
MPI_Comm comm_MACRO = MPI_COMM_WORLD;
int rk_MACRO, sz_MACRO;

#    define MPI_TIMER_INIT()                                                                       \
        global_time_MACRO = MPI_Wtime();                                                           \
        MPI_Comm_rank(comm_MACRO, &rk_MACRO);                                                      \
        MPI_Comm_size(comm_MACRO, &sz_MACRO);                                                      \
        if (rk_MACRO == 0) {                                                                       \
            alldt_MACRO = new Real[sz_MACRO];                                                      \
        }

#    define MPI_TIMER_1() t1_MACRO = MPI_Wtime();

#    define MPI_TIMER_2(str)                                                                       \
        t2_MACRO = MPI_Wtime();                                                                    \
        dt_MACRO = t2_MACRO - t1_MACRO;                                                            \
        MPI_Gather(&dt_MACRO, 1, MPI_DOUBLE, alldt_MACRO, 1, MPI_DOUBLE, 0, comm_MACRO);           \
        if (rk_MACRO == 0) {                                                                       \
            min_MACRO = max_MACRO = avg_MACRO = alldt_MACRO[0];                                    \
            for (int i_MACRO = 1; i_MACRO < sz_MACRO; i_MACRO++) {                                 \
                min_MACRO = (min_MACRO < alldt_MACRO[i_MACRO]) ? min_MACRO : alldt_MACRO[i_MACRO]; \
                max_MACRO = (max_MACRO > alldt_MACRO[i_MACRO]) ? max_MACRO : alldt_MACRO[i_MACRO]; \
                avg_MACRO += alldt_MACRO[i_MACRO];                                                 \
            }                                                                                      \
            std::println("CHECK_TIMING ({:8.3f}) -- {:>20} : {:8.3f} | {:8.3f} | {:8.3f}",         \
                         MPI_Wtime() - global_time_MACRO,                                          \
                         str,                                                                      \
                         min_MACRO,                                                                \
                         avg_MACRO / sz_MACRO,                                                     \
                         max_MACRO);                                                               \
        }
#    define MPI_TIMER_CLEAN()                                                                      \
        if (rk_MACRO == 0) {                                                                       \
            delete[] alldt_MACRO;                                                                  \
        }

#else

#    define MPI_TIMER_INIT()                                                                       \
        do {                                                                                       \
        } while (0);
#    define MPI_TIMER_1()                                                                          \
        do {                                                                                       \
        } while (0);
#    define MPI_TIMER_2(str)                                                                       \
        do {                                                                                       \
        } while (0);
#    define MPI_TIMER_CLEAN()                                                                      \
        do {                                                                                       \
        } while (0);

#endif
