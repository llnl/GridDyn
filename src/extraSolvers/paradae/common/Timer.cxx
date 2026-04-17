/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Timer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

using std::cout;
using std::endl;
using std::ostringstream;
using std::printf;
using std::string;

namespace griddyn::paradae {

    void TimedElem::Start(string name_)
    {
        if (!is_started) {
            if (strcmp(name_.c_str(), "")) name = name_;
            is_started = true;
            start_time = MPI_Wtime();
        }
    }

    void TimedElem::Stop()
    {
        if (is_started) {
            is_started = false;
            cumul_time += MPI_Wtime() - start_time;
            start_time = 0;
        }
    }

    Timer::Timer(MPI_Comm comm_)
    {
        comm = comm_;
        MPI_Comm_size(comm, &mpi_size);
        MPI_Comm_rank(comm, &mpi_rank);
        nb_elems = 0;
        sstr = 0;
    }

    void Timer::SetComm(MPI_Comm comm_)
    {
        comm = comm_;
        MPI_Comm_size(comm, &mpi_size);
        MPI_Comm_rank(comm, &mpi_rank);
        MPI_Barrier(comm);
    }

    void Timer::Start(string key, string output, string dep)
    {
        auto it_value = this->find(key);
        if (it_value == this->end()) {
            if (nb_elems < MAX_NTIME) {
                if (output.empty()) output = key;

                this->insert(pair<string, int>(key, nb_elems));
                elems[nb_elems].Start(output);
                depends_on[nb_elems] = -1;
                depth[nb_elems] = 0;
                if (!dep.empty()) {
                    auto it_value2 = this->find(dep);
                    if (it_value2 != this->end()) {
                        depends_on[nb_elems] = it_value2->second;
                        depth[nb_elems] = depth[it_value2->second] + 1;
                    }
                }
                sstr = (sstr > 3 * depth[nb_elems] + output.length()) ?
                    sstr :
                    (3 * depth[nb_elems] + output.length());
                max_depth = (depth[nb_elems] > max_depth) ? depth[nb_elems] : max_depth;
                nb_elems++;
            } else if (mpi_rank == 0)
                cout << "Too many timers... " << endl;
        } else {
            elems[it_value->second].Start();
        }
    }

    void Timer::Stop(string key, bool barrier)
    {
        auto it_value = this->find(key);
        if (it_value != this->end()) {
            elems[it_value->second].Stop();
            if (barrier) MPI_Barrier(comm);
        }
    }

    void Timer::StopAll()
    {
        for (int i = 0; i < nb_elems; i++)
            elems[i].Stop();
    }

    Real Timer::GetCurrentTime(string key)
    {
        auto it_value = this->find(key);
        if (it_value != this->end()) return elems[it_value->second].GetTime();
        return -1;
    }

    void Timer::Gather()
    {
        this->StopAll();
        if (mpi_rank == 0) {
            min_t.Resize(nb_elems);
            avg_t.Resize(nb_elems);
            max_t.Resize(nb_elems);
        }
        Real val, mval, aval, Mval;
        for (int i = 0; i < nb_elems; i++) {
            val = elems[i].GetTime();
            MPI_Reduce(&val, &mval, 1, MPI_DOUBLE, MPI_MIN, 0, comm);
            MPI_Reduce(&val, &aval, 1, MPI_DOUBLE, MPI_SUM, 0, comm);
            MPI_Reduce(&val, &Mval, 1, MPI_DOUBLE, MPI_MAX, 0, comm);
            if (mpi_rank == 0) {
                min_t(i) = mval;
                avg_t(i) = aval / mpi_size;
                max_t(i) = Mval;
            }
        }
    }

    void Timer::ShowStats(int deps)
    {
        if (deps == -1) {
            this->Gather();
        }
        if (mpi_rank == 0) {
            for (int i = 0; i < nb_elems; i++) {
                if (depends_on[i] == deps) {
                    ostringstream str;
                    str << "Timer -- ";
                    if (deps > -1) {
                        for (int j = 0; j < depth[i] - 1; j++)
                            str << "   ";
                        str << "\342\224\224> ";
                    }
                    str << "%-" << sstr - 3 * depth[i] << "s : %8.3f | %8.3f | %8.3f\n";

                    printf(str.str().c_str(),
                           elems[i].GetName().c_str(),
                           min_t(i),
                           avg_t(i),
                           max_t(i));
                    this->ShowStats(i);
                }
            }
        }
    }

    Timer global_timer;

}  // namespace griddyn::paradae
