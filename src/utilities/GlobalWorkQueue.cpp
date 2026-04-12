/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#include "GlobalWorkQueue.hpp"

#include "griddyn/griddyn-config.h"
#include <memory>

namespace griddyn {
const std::shared_ptr<gmlc::containers::WorkQueue>& getGlobalWorkQueue(int threads)
{
    static std::shared_ptr<gmlc::containers::WorkQueue> p_instance =
        std::make_shared<gmlc::containers::WorkQueue>(threads);
    return p_instance;
}
}  // namespace griddyn
