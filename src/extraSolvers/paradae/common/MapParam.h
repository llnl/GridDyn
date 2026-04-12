/*
 * LLNS Copyright Start
 * Copyright (c) 2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#pragma once

#include "def.h"
#include <cstring>
#include <map>
#include <mpi.h>
#include <string>

namespace griddyn {
namespace paradae {
    class SVector;

    class MapParam: public std::map<std::string, std::string> {
        MPI_Comm comm;
        int mpi_rank;

      public:
        MapParam() { mpi_rank = 0; };
        MapParam(MPI_Comm comm_);
        void ReadFile(const std::string& filename);
        Real GetRealParam(std::string key, Real default_val = -1.) const;
        int GetIntParam(std::string key, int default_val = -1) const;
        std::string GetStrParam(std::string key, std::string default_val = "") const;
        bool GetBoolParam(std::string key, bool default_val = false) const;
        SVector GetVectorParam(std::string key, SVector default_val) const;
        int GetMpiRank() const { return mpi_rank; };
    };
}  // namespace paradae
}  // namespace griddyn
