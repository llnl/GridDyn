/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "def.h"
#include <cstring>
#include <map>
#include <mpi.h>
#include <string>

namespace griddyn::paradae {
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
}  // namespace griddyn::paradae
