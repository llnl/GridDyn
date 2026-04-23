/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "NewtonStats.h"

#include <climits>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <list>
#include <print>
#include <utility>

namespace griddyn::paradae {

NewtonStats newton_stats;

using namespace std;

Real NewtonStats::GlobalStats_Seq()
{
    Real avg = 0;
    int nb = 0;
    for (map<int, list<int>>::const_iterator it = map1.begin(); it != map1.end(); it++) {
        for (list<int>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
            avg += *it2;
            nb++;
        }
    }
    avg /= nb;
    return avg;
}

Real NewtonStats::GlobalStats_Braid()
{
    Real avg = 0;
    int nb = 0;
    for (map<pair<int, int>, list<int>>::const_iterator it = map2.begin(); it != map2.end(); it++) {
        for (list<int>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
            avg += *it2;
            nb++;
        }
    }

    MPI_Allreduce(MPI_IN_PLACE, &avg, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(MPI_IN_PLACE, &nb, 1, MPI_INT, MPI_SUM, comm);

    avg /= nb;
    return avg;
}

void NewtonStats::Add(int nbiter)
{
    if (type == sequential)
        map1[step].push_back(nbiter);
    else
        map2[make_pair(iter, level)].push_back(nbiter);
}

Real NewtonStats::GlobalStats()
{
    if (type == sequential)
        return this->GlobalStats_Seq();
    else
        return this->GlobalStats_Braid();
}

void NewtonStats::PerStepStats(SVector& min_, SVector& avg_, SVector& max_)
{
    int nbsteps = map1.size();
    if (nbsteps == 0) return;
    min_.Resize(nbsteps, INT_MAX);
    avg_.Resize(nbsteps, 0);
    max_.Resize(nbsteps, 0);

    for (map<int, list<int>>::const_iterator it = map1.begin(); it != map1.end(); it++) {
        int key = it->first;
        for (list<int>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
            avg_(key) += *it2;
            min_(key) = (min_(key) < *it2) ? min_(key) : *it2;
            max_(key) = (max_(key) > *it2) ? max_(key) : *it2;
        }
        avg_(key) /= it->second.size();
    }
}

void NewtonStats::SetIL(int i, int lvl)
{
    iter = i;
    level = lvl;
    max_iter = (i > max_iter) ? i : max_iter;
    max_level = (lvl > max_level) ? lvl : max_level;
}

void NewtonStats::PerIterStats(SVector& min_, SVector& avg_, SVector& max_, SVector& nbcalls)
{
    int mi;
    MPI_Allreduce(&max_iter, &mi, 1, MPI_INT, MPI_MAX, comm);
    if (mi == 0) return;
    nbcalls.Resize(mi + 1, 0);
    min_.Resize(mi + 1, INT_MAX);
    avg_.Resize(mi + 1, 0);
    max_.Resize(mi + 1, 0);

    for (map<pair<int, int>, list<int>>::const_iterator it = map2.begin(); it != map2.end(); it++) {
        int key = it->first.first;
        for (list<int>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
            avg_(key) += *it2;
            min_(key) = (min_(key) < *it2) ? min_(key) : *it2;
            max_(key) = (max_(key) > *it2) ? max_(key) : *it2;
            nbcalls(key)++;
        }
    }

    MPI_Allreduce(MPI_IN_PLACE, nbcalls.GetData(), mi + 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(MPI_IN_PLACE, avg_.GetData(), mi + 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(MPI_IN_PLACE, min_.GetData(), mi + 1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(MPI_IN_PLACE, max_.GetData(), mi + 1, MPI_DOUBLE, MPI_MAX, comm);

    for (int i = 0; i < mi + 1; i++)
        avg_(i) /= nbcalls(i);
}

void NewtonStats::PerLevelStats(SVector& min_, SVector& avg_, SVector& max_, SVector& nbcalls)
{
    int ml;
    MPI_Allreduce(&max_level, &ml, 1, MPI_INT, MPI_MAX, comm);
    if (ml == 0) return;
    nbcalls.Resize(ml + 1, 0);
    min_.Resize(ml + 1, INT_MAX);
    avg_.Resize(ml + 1, 0);
    max_.Resize(ml + 1, 0);

    for (map<pair<int, int>, list<int>>::const_iterator it = map2.begin(); it != map2.end(); it++) {
        int key = it->first.second;
        for (list<int>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
            avg_(key) += *it2;
            min_(key) = (min_(key) < *it2) ? min_(key) : *it2;
            max_(key) = (max_(key) > *it2) ? max_(key) : *it2;
            nbcalls(key)++;
        }
    }

    MPI_Allreduce(MPI_IN_PLACE, nbcalls.GetData(), ml + 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(MPI_IN_PLACE, avg_.GetData(), ml + 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(MPI_IN_PLACE, min_.GetData(), ml + 1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(MPI_IN_PLACE, max_.GetData(), ml + 1, MPI_DOUBLE, MPI_MAX, comm);

    for (int i = 0; i < ml + 1; i++)
        avg_(i) /= nbcalls(i);
}

void NewtonStats::ShowStats()
{
    int rank;
    MPI_Comm_rank(comm, &rank);
    Real avg = this->GlobalStats();
    if (rank == 0) {
        cout << endl << "### Statistics from the Newton solver" << endl;
        cout << endl << "Global average of # Newton iterations : " << avg << endl;
    }
    SVector min_, avg_, max_, nbcalls;
    if (type == sequential) {
        if (rank == 0) {
            this->PerStepStats(min_, avg_, max_);
            ofstream file("newton_stats_per_step.dat");
            cout << "Stats per time steps of # Newton iterations (min,avg,max): see file." << endl;
            for (int i = 0; i < avg_.GetM(); i++)
                file << i << " " << min_(i) << " " << avg_(i) << " " << max_(i) << endl;
            file.close();
        }
    } else {
        this->PerIterStats(min_, avg_, max_, nbcalls);
        if (rank == 0) {
            cout << endl << "Stats Braid iter of # Newton iterations (min,avg,max):" << endl;
            for (int i = 0; i < avg_.GetM() - 1; i++)
                std::println("Iter  {:2} : {:2} | {:5.2f} | {:3}   ({:10} calls)",
                             i,
                             int(min_(i)),
                             avg_(i),
                             int(max_(i)),
                             int(nbcalls(i)));
            std::println("Last     : {:2} | {:5.2f} | {:3}   ({:10} calls)",
                         int(min_(avg_.GetM() - 1)),
                         avg_(avg_.GetM() - 1),
                         int(max_(avg_.GetM() - 1)),
                         int(nbcalls(avg_.GetM() - 1)));
        }
        this->PerLevelStats(min_, avg_, max_, nbcalls);
        if (rank == 0) {
            cout << endl << "Stats per grid level of # Newton iterations (min,avg,max):" << endl;
            for (int i = 0; i < avg_.GetM(); i++)
                std::println("Level {:2} : {:2} | {:5.2f} | {:3}   ({:10} calls)",
                             i,
                             int(min_(i)),
                             avg_(i),
                             int(max_(i)),
                             int(nbcalls(i)));
        }
    }
}
}  // namespace griddyn::paradae
